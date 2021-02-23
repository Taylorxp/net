
#include "net.h"
#include "common.h"
#include "configs.h"
#include "msg.h"

enum net_state_e net_state = net_wait_cable;

extern configs_t cfg;

int32_t CmdTask_Start(void);
int32_t CmdTask_Stop(void);

int GetSysDns(struct network_info_st* ninfo);

int32_t Net_SetUp(void)
{
    system("ifconfig eth0 up");
    return 0;
}

int32_t Net_SetDown(void)
{
    system("ifconfig eth0 down");
    system("killall udhcpc");
    return 0;
}

int32_t Net_CheckCable(void)
{
    char  rets[16] = {0};
    FILE* fp;
    // fp = fopen("/sys/class/net/eth0/operstate", "r");
    fp = fopen("/sys/class/net/eth0/carrier", "r");
    
    if (fp == NULL)
    {
        PRT_ERROR("cann`t get cable info!");
    }
    else
    {  // printf("cat /sys/class/net/eth0/operstate\n");
        if (fgets(rets, sizeof(rets), fp) != NULL)
        {
            // printf("%s\n", rets);
            // if (strstr(rets, "up") != NULL)
            if (strstr(rets, "1") != NULL)
            {
                fclose(fp);
                return 1;
            }
        }
    }
    fclose(fp);
    return 0;
}

int32_t Net_SetMac(struct network_info_st* net)
{
    char shcmd[64];
    memset(shcmd, '\0', sizeof(shcmd));
    sprintf(shcmd, "ifconfig eth0 hw ether %s", mac2str(net->mac));
    PRT_LOG("%s", shcmd);
    system(shcmd);
    return 0;
}

int32_t Net_SetStaticIp(struct network_info_st* net)
{
    char shcmd[64];

    system("killall udhcpc");

    memset(shcmd, '\0', sizeof(shcmd));
    sprintf(shcmd, "ifconfig eth0 %s netmask ", ip2str(net->ip));
    strcat(shcmd, ip2str(net->netmask));
    PRT_LOG("%s", shcmd);
    system(shcmd);

    memset(shcmd, '\0', sizeof(shcmd));
    if ((net->gateway & 0xFF) == 0)
    {
        net->gateway = (net->ip & 0xFFFFFF00) | 0x01;
    }
    sprintf(shcmd, "route add default gw %s", ip2str(net->gateway));
    PRT_LOG("%s", shcmd);
    system(shcmd);

    struct network_info_st netinfo;
    GetSysDns(&netinfo);
    if (memcmp(netinfo.dns, net->dns, 8))
    {
        PRT_LOG("write new dns to \"/etc/resolv.conf\"");
        int fd = open("/etc/resolv.conf", O_RDWR | O_APPEND | O_CREAT);
        ftruncate(fd, 0);
        lseek(fd, 0, SEEK_SET);  // SEEK_SET，SEEK_CUR，SEEK_END

        char str_dns[50] = {0};
        memset(str_dns, '\0', sizeof(str_dns));
        sprintf(str_dns, "nameserver %s\n", ip2str(net->dns[0]));
        write(fd, str_dns, strlen(str_dns));

        memset(str_dns, '\0', sizeof(str_dns));
        sprintf(str_dns, "nameserver %s\n", ip2str(net->dns[1]));
        write(fd, str_dns, strlen(str_dns));
        close(fd);
    }

    return 0;
}

int32_t Net_SetDhcp(void)
{
    system("killall udhcpc");
    system("udhcpc -b");
    return 0;
}

int32_t Net_Reset(void)
{
    PRT_LOG("reset net!");
    CmdTask_Stop();
    Net_SetDown();
    Net_SetMac(&cfg.net_info);
    Net_SetUp();
    net_state = net_wait_cable;
    return 0;
}

int32_t Net_UpdateConfig(void)
{
    // Msg_SendCableState(Net_CheckCable());
    if (cfg.net_info.dhcp)
    {
        struct network_info_st netinfo;
        memset(&netinfo, 0, sizeof(struct network_info_st));
        memcpy(&netinfo.mac, &cfg.net_info.mac, 6);
        netinfo.dhcp = 1;
        Msg_SendNetInfo(&netinfo);
    }
    else
    {
        Msg_SendNetInfo(&cfg.net_info);
    }
    Net_Reset();
    return 0;
}

/***************************************************************************************************
 * Description: 获取当前系统网络状态
 **************************************************************************************************/
int GetSysDns(struct network_info_st* ninfo)
{
    FILE* fp         = NULL;
    char  rets[1024] = {0};
    char *str_start, *str_end;

    fp = fopen("/etc/resolv.conf", "r");
    if (fp == NULL)
    {
        PRT_ERROR("cann`t read dns info!");
    }
    else
    {
        if (fgets(rets, sizeof(rets), fp) != NULL)
        {
            str_start = strstr(rets, "nameserver ");
            if (str_start != NULL)
            {
                str_start += sizeof("nameserver ") - 1;
                str_end = str_start;
                while ((*str_end != ' ' || *str_end != '\n') && ((str_end - str_start) < 17))
                {
                    str_end++;
                }
                ninfo->dns[0] = str2ip(str_start);
                // PRT_LOG("dns0: %.*s", 15, str_start);
            }
            else
            {
                ninfo->dns[0] = 0;
                PRT_WARN("no dns0 info!");
            }
        }
        if (fgets(rets, sizeof(rets), fp) != NULL)
        {
            str_start = strstr(rets, "nameserver ");
            if (str_start != NULL)
            {
                str_start += sizeof("nameserver ") - 1;
                str_end = str_start;
                while ((*str_end != ' ' || *str_end != '\n') && ((str_end - str_start) < 17))
                {
                    str_end++;
                }
                ninfo->dns[1] = str2ip(str_start);
                // PRT_LOG("dns1: %.*s", 15, str_start);
            }
            else
            {
                ninfo->dns[1] = 0;
                PRT_WARN("no dns1 info!");
            }
        }
    }
    return 0;
}

int32_t GetNetInfo(struct network_info_st* ninfo)
{
    FILE* fp         = NULL;
    char  rets[1024] = {0};
    char *str_start, *str_end;

    fp = popen("ifconfig", "r");
    if (fp == NULL)
    {
        PRT_ERROR("popen ifconfig error!");
    }
    else
    {
        if (!fread(rets, sizeof(rets), 1, fp))
        {
            // PRT_LOG("%s", rets);

            str_start = strstr(rets, "HWaddr ");
            if (str_start != NULL)
            {
                str_start += sizeof("HWaddr ") - 1;
                str_end = str_start;
                while ((*str_end != ' ' || *str_end != '\n') && ((str_end - str_start) < 17))
                {
                    str_end++;
                }
                memcpy(ninfo->mac, str2mac(str_start), 6);
                // PRT_LOG("mac: %.*s", 17, str_start);
            }
            else
            {
                memset(ninfo->mac, 0, 6);
                PRT_WARN("no mac info!");
            }

            str_start = strstr(rets, "inet addr:");
            if (str_start != NULL)
            {
                str_start += sizeof("inet addr:") - 1;
                str_end = str_start;
                while ((*str_end != ' ' || *str_end != '\n') && ((str_end - str_start) < 15))
                {
                    str_end++;
                }
                ninfo->ip = str2ip(str_start);
                // PRT_LOG("ip: %.*s", 15, str_start);
            }
            else
            {
                ninfo->ip = 0;
                PRT_WARN("no ip info!");
            }

            str_start = strstr(rets, "Mask:");
            if (str_start != NULL)
            {
                str_start += sizeof("Mask:") - 1;
                str_end = str_start;
                while ((*str_end != ' ' || *str_end != '\n') && ((str_end - str_start) < 15))
                {
                    str_end++;
                }
                ninfo->netmask = str2ip(str_start);
                // PRT_LOG("netmask: %.*s", 15, str_start);
            }
            else
            {
                PRT_WARN("no netmask info!");
            }
        }
        pclose(fp);
    }

    fp = popen("route", "r");
    if (fp == NULL)
    {
        PRT_ERROR("popen route error!");
    }
    else
    {
        if (!fread(rets, sizeof(rets), 1, fp))
        {
            str_start = strstr(rets, "default         ");
            if (str_start != NULL)
            {
                str_start += sizeof("default         ") - 1;
                str_end = str_start;
                while ((*str_end != ' ' || *str_end != '\n') && ((str_end - str_start) < 17))
                {
                    str_end++;
                }
                ninfo->gateway = str2ip(str_start);
                // PRT_LOG("gateway: %.*s", 15, str_start);
            }
            else
            {
                ninfo->gateway = 0;
                PRT_WARN("no gateway info!");
            }
        }
        pclose(fp);
    }

    GetSysDns(ninfo);

    return 0;
}

/***************************************************************************************************
 * Description: 设置网络设置更新标记
 **************************************************************************************************/
static int net_update_flag = 0;
void Net_Update(void)
{
    net_update_flag = 1;
}

/***************************************************************************************************
 * Description:网络设置循环
 **************************************************************************************************/
static inline void _Net_CheckCable(void)
{
    if (!Net_CheckCable())
    {
        PRT_LOG("net cable pull out!");
        Msg_SendCableState(0);
        CmdTask_Stop();
        net_state = net_wait_cable;
    }
}

void Net_Loop(void)
{
    if(net_update_flag)
    {
        net_update_flag = 0;
        Net_UpdateConfig();
    }

    switch (net_state)
    {
        /* 等待网线插入 */
        case net_wait_cable:
        {
            /* 网线已插入 */
            if (Net_CheckCable())
            {
                PRT_LOG("net cable ready!");
                Msg_SendCableState(1);
                if (cfg.net_info.dhcp)
                {
                    Net_SetDhcp();
                    struct network_info_st netinfo;
                    memset(&netinfo, 0, sizeof(struct network_info_st));
                    memcpy(&netinfo.mac, &cfg.net_info.mac, 6);
                    netinfo.dhcp = 1;
                    Msg_SendNetInfo(&netinfo);
                    net_state = net_dhcp_wait;
                }
                else
                {
                    Net_SetStaticIp(&cfg.net_info);
                    Msg_SendNetInfo(&cfg.net_info);
                    CmdTask_Start();
                    net_state = net_static;
                }
            }
        }
        break;

        /* 网络已连接,静态IP方式 */
        case net_static: 
        {
            _Net_CheckCable();
        }
        break;

        /* 等待DHCP */
        case net_dhcp_wait:
        {
            GetNetInfo(&cfg.net_info);
            if (cfg.net_info.ip != 0)
            {
                Msg_SendNetInfo(&cfg.net_info);
                CmdTask_Start();
                net_state = net_dhcp_ready;
            }

            _Net_CheckCable();
        }
        break;

        /* 网络已连接,DHCP方式 */
        case net_dhcp_ready: 
        { 
            _Net_CheckCable();
        }
        break;
    }
}