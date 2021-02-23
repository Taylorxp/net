/***************************************************************************************************
 * FILE: udp_multicast_group.c
 *
 * DESCRIPTION:
 *
 **************************************************************************************************/
#include "common.h"
#include "configs.h"
#include "net.h"

#define UDP_MULTICAST_ADDR "239.255.255.253"
#define UDP_MULTICAST_PORT (5297)

struct udp_thread_arg
{
    uint32_t enable;
    uint32_t running;
};

pthread_t             udp_recv_pid;
pthread_mutex_t       g_mutex;
struct udp_thread_arg udp_recv_arg;
int                   udp_fd;
struct ip_mreq        udp_mreq;
struct sockaddr_in    udp_recv_addr_in;
int32_t               udp_recv_addrlen      = sizeof(udp_recv_addr_in);
static uint8_t        udp_recv_buffer[1024] = {0};

uint8_t reply_discovery[] = "I`m here! #MAC:12:34:56:78:90:AB #BRIEF:K1B Camera control unit.";

extern configs_t cfg;

int32_t Cmd_Parse(uint8_t* cmd, uint32_t size, uint32_t cmd_index);

void* Udp_RecvProcess(void* p);

/***************************************************************************************************
 * Description: 数据处理集合
 **************************************************************************************************/
#define SET_CMD_INDEX(cmd, index)                                                                                                                                                  \
    do                                                                                                                                                                             \
    {                                                                                                                                                                              \
        cmd[0] = (uint8_t)((index >> 24) & 0xFF);                                                                                                                                  \
        cmd[1] = (uint8_t)((index >> 16) & 0xFF);                                                                                                                                  \
        cmd[2] = (uint8_t)((index >> 8) & 0xFF);                                                                                                                                   \
        cmd[3] = (uint8_t)((index >> 0) & 0xFF);                                                                                                                                   \
    } while (0);

inline uint32_t GET_CMD_INDEX(uint8_t* cmd)
{
    uint32_t index;
    index = cmd[0];
    index <<= 8;
    index |= cmd[1];
    index <<= 8;
    index |= cmd[2];
    index <<= 8;
    index |= cmd[3];
    return index;
}

/***************************************************************************************************
 * Description: 初始化UDP组播,加入组
 **************************************************************************************************/
int32_t CmdTask_Start(void)
{
    struct sockaddr_in udp_multicast_addr;

    /* 创建 socket 用于UDP通讯 */
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_fd < 0)
    {
        PRT_ERROR("socket creating err in udptalk");
        return -1;
    }

    /* 设置为非阻塞模式 */
    int flag = fcntl(udp_fd, F_GETFL, 0);
    if (flag < 0)
    {
        PRT_ERROR("fcntl F_GETFL fail");
        close(udp_fd);
        return -1;
    }
    if (fcntl(udp_fd, F_SETFL, flag | O_NONBLOCK) < 0)
    {
        PRT_ERROR("fcntl F_SETFL fail");
        close(udp_fd);
        return -1;
    }

    /* 初始化地址 */
    bzero(&udp_multicast_addr, sizeof(udp_multicast_addr));
    udp_multicast_addr.sin_family      = AF_INET;
    udp_multicast_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    udp_multicast_addr.sin_port        = htons(UDP_MULTICAST_PORT);

    if (bind(udp_fd, (struct sockaddr*)&udp_multicast_addr, sizeof(udp_multicast_addr)) < 0)
    {
        PRT_ERROR("udp bind error!");
        close(udp_fd);
        return -1;
    }

    // int on = 1;
    // /* 设置地址复用许可, 根据具体情况判断是否增加此功能 */
    // if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
    // {
    //     perror("SO_REUSEADDR");
    //     return -1;
    // }

    /*加入多播组*/
    udp_mreq.imr_multiaddr.s_addr = inet_addr(UDP_MULTICAST_ADDR);
    udp_mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(udp_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &udp_mreq, sizeof(udp_mreq)) < 0)
    {
        PRT_ERROR("udp setsockopt error!");
        close(udp_fd);
        return -1;
    }

    udp_recv_arg.enable  = 1;
    udp_recv_arg.running = 0;
    pthread_create(&udp_recv_pid, 0, Udp_RecvProcess, &udp_recv_arg);

    return 0;
}

/***************************************************************************************************
 * Description:
 **************************************************************************************************/
int32_t CmdTask_Stop(void)
{
    if (udp_recv_arg.running == 0)
        return 0;

    pthread_mutex_lock(&g_mutex);
    udp_recv_arg.enable = 0;
    pthread_mutex_unlock(&g_mutex);
    PRT_LOG("wait udp receive thread exit...");
    int flag = 1;
    while (flag)
    {
        pthread_mutex_lock(&g_mutex);
        flag = udp_recv_arg.running;
        pthread_mutex_unlock(&g_mutex);
        usleep(100000);
    }
    PRT_LOG("udp receive thread has exit!");

    /*退出多播组*/
    setsockopt(udp_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &udp_mreq, sizeof(udp_mreq));
    close(udp_fd);
    return 0;
}

/***************************************************************************************************
 * Description: UDP发送数据
 **************************************************************************************************/
int32_t Udp_SendCmd(uint8_t* cmd, uint32_t size, uint32_t cmd_index)
{
    uint8_t* tmp = (uint8_t*)malloc(size + 5);
    if (tmp == NULL)
        return -1;
    SET_CMD_INDEX(tmp, cmd_index);
    tmp[4] = size;
    memcpy(&tmp[5], cmd, size);
    sendto(udp_fd, tmp, size + 5, 0, (struct sockaddr*)&udp_recv_addr_in, sizeof(struct sockaddr_in));
    free(tmp);
    return 0;
}

/***************************************************************************************************
 * Description: udp接收线程
 **************************************************************************************************/
void* Udp_RecvProcess(void* p)
{
    fd_set                 rcv_fds;
    struct timeval         timeout;
    struct udp_thread_arg* arg;
    int32_t                recv_size;

    memset(udp_recv_buffer, 0, sizeof(udp_recv_buffer));

    arg = (struct udp_thread_arg*)p;

    arg->running = 1;

    while (arg->enable)
    {
        FD_ZERO(&rcv_fds);
        FD_SET(udp_fd, &rcv_fds);
        timeout.tv_sec  = 0;
        timeout.tv_usec = 500000;

        int32_t ret = select(udp_fd + 1, &rcv_fds, NULL, NULL, &timeout);
        if (ret < 0)
        {
            PRT_ERROR("udp receive select error!");
            break;
        }
        else if (ret == 0)
        {
            if (arg->enable == 0)
            {
                pthread_mutex_lock(&g_mutex);
                arg->running = 0;
                pthread_mutex_unlock(&g_mutex);
                return NULL;
            }
        }
        else
        {
            if (!FD_ISSET(udp_fd, &rcv_fds))
                continue;
            do
            {
                recv_size = recvfrom(udp_fd, udp_recv_buffer, sizeof(udp_recv_buffer), 0, (struct sockaddr*)&udp_recv_addr_in, (socklen_t*)&udp_recv_addrlen);
                {
                    if (memcmp("come on baby!", udp_recv_buffer, recv_size) == 0)
                    {
                        PRT_LOG("Divice discovery msg received! send a reply msg.");
                        memset(reply_discovery, '\0', sizeof(reply_discovery));
                        sprintf((char*)reply_discovery, "I`m here! #MAC:%s #BRIEF:k1b camera control unit.", mac2str(cfg.net_info.mac));
                        sendto(udp_fd, reply_discovery, sizeof(reply_discovery), 0, (struct sockaddr*)&udp_recv_addr_in, sizeof(struct sockaddr_in));
                    }
                    else
                    {
                        if (udp_recv_buffer[5] == 0xFF && udp_recv_buffer[recv_size - 1] == 0xFF)
                        {
                            Cmd_Parse(&udp_recv_buffer[4], recv_size - 4, GET_CMD_INDEX(udp_recv_buffer));
                        }
                    }
                    memset(udp_recv_buffer, 0, sizeof(udp_recv_buffer));
                }
            } while (recv_size > 0);
        }
    }

    arg->running = 0;
    // pthread_exit(NULL);

    return NULL;
}

/****************************************** END OF FILE *******************************************/