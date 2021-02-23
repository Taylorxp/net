/***************************************************************************************************
 * FILE: net.h
 *
 * DESCRIPTION:
 *
 **************************************************************************************************/
#ifndef __NET_H__
#define __NET_H__

#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

enum net_state_e {
    net_wait_cable,
    net_static,
    net_dhcp_wait,
    net_dhcp_ready,
};

struct network_info_st;

int32_t Net_SetUp(void);
int32_t Net_SetDown(void);
int32_t Net_CheckCable(void);
int32_t Net_SetMac(struct network_info_st* net);
int32_t Net_SetStaticIp(struct network_info_st* net);
int32_t Net_SetDhcp(void);
int32_t Net_Reset(void);
int32_t Net_UpdateConfig(void);
int32_t GetNetInfo(struct network_info_st* ninfo);

void Net_Update(void);

void Net_Loop(void);

#ifdef __cplusplus
}
#endif

#endif
/****************************************** END OF FILE *******************************************/