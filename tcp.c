/***************************************************************************************************
 * FILE: tcp_client.c
 *
 * DESCRIPTION: tcp客户端
 *
 **************************************************************************************************/
#include "common.h"

#include "xprint.h"
#include "configs.h"
#include "md5.h"



uint8_t cmd_request_upgrade[]            = "request upgrade.";
uint8_t cmd_request_file_check[]         = "request file check.";
uint8_t cmd_request_file_start[]         = "request file start.";
uint8_t cmd_request_file_start_confirm[] = "request file start confirm.";
uint8_t cmd_request_file_end_confirm[]   = "request file end confirm.";
uint8_t cmd_request_file_end_reject[]    = "request file end reject.";
uint8_t cmd_flag[5]                      = {'#', 'C', 'M', 'D', '#'};
uint8_t cmd_flag_file_size[]             = "file size:";
uint8_t cmd_flag_file_check[]            = "file check:";
uint8_t cmd_flag_file_start[]            = "file start.";

int32_t Tcp_DoUpgrade(uint32_t ipaddr, uint16_t port)
{
    char    tmp_file_name[] = "/tmp/upgrade.XXXXXX";
    int32_t tmp_fd          = 0;
    FILE*   tmp_fp          = NULL;

    int32_t            sk_fd;
    int32_t            addr_len;
    struct sockaddr_in addr_local;
    struct sockaddr_in addr_in;
    uint8_t            recv_buffer[4096] = {0};

    fd_set         tcprcv_fds;
    int32_t        selret;
    struct timeval timeout;

    uint32_t recv_start = 0;
    int32_t  recv_size;
    uint32_t file_size     = 0;
    uint32_t exp_file_size = 0;
    char     md5sum[33]    = {0};

    /* 连接服务器 */
    if ((sk_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        PRT_ERROR("create tcp socket error: %s(errno: %d)", strerror(errno), errno);
        fflush(stdout);
        return 0;
    }

    memset(&addr_in, 0, sizeof(addr_in));
    addr_in.sin_family      = AF_INET;
    addr_in.sin_addr.s_addr = htonl(ipaddr);
    addr_in.sin_port        = htons(port);
    if (connect(sk_fd, (struct sockaddr*)&addr_in, sizeof(addr_in)) < 0)
    {
        PRT_ERROR("tcp connect error: %s(errno: %d)", strerror(errno), errno);
        return 0;
    }
    getsockname(sk_fd, (struct sockaddr*)&addr_local, (socklen_t*)&addr_len);
    PRT_LOG("tcp client connect to %d.%d.%d.%d:%d local port:%d", (addr_in.sin_addr.s_addr >> 0) & 0xFF, (addr_in.sin_addr.s_addr >> 8) & 0xFF,
            (addr_in.sin_addr.s_addr >> 16) & 0xFF, (addr_in.sin_addr.s_addr >> 24) & 0xFF, port, ntohs(addr_local.sin_port));

    /* 发送升级请求 */
    if (send(sk_fd, cmd_request_upgrade, sizeof(cmd_request_upgrade) - 1, 0) < 0)
    {
        PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
        return 0;
    }

    memset(recv_buffer, 0, sizeof(recv_buffer));

    while (1)
    {
        FD_ZERO(&tcprcv_fds);
        FD_SET(sk_fd, &tcprcv_fds);

        timeout.tv_sec  = 3;
        timeout.tv_usec = 0;

        selret = select(sk_fd + 1, &tcprcv_fds, NULL, NULL, &timeout);
        if (selret < 0)
        {
            PRT_ERROR("tcp receive select error!");
            return 0;
        }
        /* 超时退出 */
        else if (selret == 0)
        {
            PRT_ERROR("tcp receive select timeout!");
            return 0;
        }

        if (FD_ISSET(sk_fd, &tcprcv_fds))
        {
            // recv_size = recvfrom(sk_fd, recv_buffer, sizeof(recv_buffer), 0, (struct sockaddr*)&addr_local, (socklen_t*)&addr_len);
            recv_size = recv(sk_fd, recv_buffer, sizeof(recv_buffer), 0);  //接收消息

            if (recv_size > 0)
            {
                /* 指令 */
                if (recv_size > 5 && memcmp(recv_buffer, cmd_flag, 5) == 0)
                {
                    /* 文件大小 */
                    if (recv_size > (5 + sizeof(cmd_flag_file_size)) &&
                        memcmp(&recv_buffer[5], cmd_flag_file_size, sizeof(cmd_flag_file_size) - 1) == 0)
                    {
                        char* str_size;
                        str_size = (char*)malloc(recv_size - (sizeof(cmd_flag) + sizeof(cmd_flag_file_size)));
                        memcpy(str_size, &recv_buffer[5 + sizeof(cmd_flag_file_size) - 1],
                               recv_size - (sizeof(cmd_flag) + sizeof(cmd_flag_file_size) - 1));
                        PRT_LOG("size:%s", str_size);
                        int size = atoi(str_size);
                        free(str_size);

                        exp_file_size = size;
                        PRT_LOG("upgrade file size:%d", size);

                        if (send(sk_fd, cmd_request_file_check, sizeof(cmd_request_file_check) - 1, 0) < 0)
                        {
                            PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
                            goto ERROR;
                        }
                    }

                    /* 文件校验信息 */
                    if (recv_size == (5 + sizeof(cmd_flag_file_check) + 32 - 1) &&
                        memcmp(&recv_buffer[5], cmd_flag_file_check, sizeof(cmd_flag_file_check) - 1) == 0)
                    {
                        memcpy(md5sum, &recv_buffer[5 + sizeof(cmd_flag_file_check) - 1], 32);
                        PRT_LOG("got file md5sum:%s", md5sum);
                        if (send(sk_fd, cmd_request_file_start, sizeof(cmd_request_file_start) - 1, 0) < 0)
                        {
                            PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
                            goto ERROR;
                        }
                    }
                    /* 开始发送文件 */
                    else if (recv_size == (5 + sizeof(cmd_flag_file_start) - 1) &&
                             memcmp(&recv_buffer[5], cmd_flag_file_start, sizeof(cmd_flag_file_start) - 1) == 0)
                    {
                        if (send(sk_fd, cmd_request_file_start_confirm, sizeof(cmd_request_file_start_confirm) - 1, 0) < 0)
                        {
                            PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
                            goto ERROR;
                        }
                        file_size  = 0;
                        recv_start = 1;

                        if (tmp_fd != 0)
                        {
                            /* 清空文件 */
                            ftruncate(tmp_fd, 0);
                            /* 重新设置文件偏移量 */
                            lseek(tmp_fd, 0, SEEK_SET);
                        }
                        else
                        {
                            tmp_fd = mkstemp(tmp_file_name);
                            if (tmp_fd <= 0)
                            {
                                PRT_ERROR("create tmp file failure!");
                            }
                            PRT_LOG("tmp file name: %s", tmp_file_name);
                            tmp_fp = fopen(tmp_file_name, "wb");
                            if (tmp_fp == NULL)
                            {
                                PRT_ERROR("open tmp file failure!");
                            }
                        }
                    }
                }
                /* 文件数据 */
                else
                {
                    if (recv_start != 1)
                        continue;

                    if (tmp_fp)
                    {
                        fwrite(recv_buffer, recv_size, 1, tmp_fp);
                        fflush(tmp_fp);
                    }

                    file_size += recv_size;

                    /* 文件接收完毕 */
                    if (file_size == exp_file_size)
                    {
                        PRT_LOG("tcp receive file finish! file size:%d", file_size);

                        char md5_str[33] = {0};
                        if (get_file_md5sum(tmp_file_name, md5_str) == 0 && memcmp(md5sum, md5_str, 32) == 0)
                        {
                            PRT_LOG("file checked successfully! file size:%d, md5sum:%s", get_file_size(tmp_file_name), md5_str);
                            if (send(sk_fd, cmd_request_file_end_confirm, sizeof(cmd_request_file_end_confirm) - 1, 0) < 0)
                            {
                                PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
                                goto ERROR;
                            }

                            system("rm -rf /tmp/upgrade");
                            system("mkdir /tmp/upgrade");
                            char shcmd[64] = {0};
                            memset(shcmd, '\0', sizeof(shcmd));
                            sprintf(shcmd, "unzip -d /tmp/upgrade/ %s", tmp_file_name);
                            int32_t shret = system(shcmd);
                            PRT_LOG("system return:%d, errno:%d", shret, errno);

                            memset(shcmd, '\0', sizeof(shcmd));
                            sprintf(shcmd, "cd /tmp/upgrade/ && chmod +x upgrade.sh && ./upgrade.sh");
                            shret = system(shcmd);
                            PRT_LOG("system return:%d, errno:%d", shret, errno);

                            remove(tmp_file_name);
                        }
                        else
                        {
                            PRT_LOG("file checked failure! file size:%d. \n got md5sum:%s\n. calculate md5sum:%s.", (int32_t)ftell(tmp_fp), md5sum,
                                     md5_str);
                            if (send(sk_fd, cmd_request_file_end_reject, sizeof(cmd_request_file_end_reject) - 1, 0) < 0)
                            {
                                PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
                                goto ERROR;
                            }
                        }
                        goto END;
                    }
                    /* 文件大小超出预期 */
                    else if (file_size > exp_file_size)
                    {
                        PRT_ERROR("the file size received is not the size expected! exp size:%d, actual size:%d", exp_file_size, file_size);
                        if (send(sk_fd, cmd_request_file_end_reject, sizeof(cmd_request_file_end_reject) - 1, 0) < 0)
                        {
                            PRT_ERROR("tcp send error: %s(errno: %d)", strerror(errno), errno);
                            goto ERROR;
                        }
                    }
                }
            }
        }
    }

ERROR:
    fflush(stdout);
    shutdown(sk_fd, SHUT_RDWR);
    close(sk_fd);
    fclose(tmp_fp);
    return 0;
END:
    fflush(stdout);
    shutdown(sk_fd, SHUT_RDWR);
    close(sk_fd);
    fclose(tmp_fp);
    return file_size;
}

/****************************************** END OF FILE *******************************************/