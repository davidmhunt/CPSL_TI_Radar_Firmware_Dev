/*
 * Copyright (c) 2001,2002 Florian Schulze.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the authors nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * enet_stream.c - This file is part of streaming Radar object data.
 *
 */

/* ========================================================================== */
/*                             Include Files                                  */
/* ========================================================================== */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
/* lwIP core includes */
#include "lwip/opt.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/dhcp.h"

#include <kernel/dpl/TaskP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/ClockP.h>


#ifdef SOC_AWR294X
#include <ti/demo/awr294x/mmw/mss/mmw_mss.h>
#else
#include <ti/demo/am273x/mmw/mss/mmw_mss.h>
#endif
#include "ti_enet_lwipif.h"
#include "enet_cpswconfighandler.h"
#include "enet_tcpclient.h"


/* ========================================================================== */
/*                           Macros & Typedefs                                */
/* ========================================================================== */

static const uint8_t BROADCAST_MAC_ADDRESS[ENET_MAC_ADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* ========================================================================== */
/*                         Structure Declarations                             */
/* ========================================================================== */

/* ========================================================================== */
/*                          Function Declarations                             */
/* ========================================================================== */

static void Enet_tcpipInitCompleteCb(void *pArg);

static void Enet_setupNetif();

static void Enet_allocateIPAddress();

static void Enet_setupNetworkStack();

static void Enet_shutdownNetworkStack();

static void Enet_netifStatusChangeCb(struct netif *state_netif);

static void Enet_netifLinkChangeCb(struct netif *state_netif);

static inline int32_t Enet_isNetworkUp(struct netif* netif_);

/* ========================================================================== */
/*                            Global Variables                                */
/* ========================================================================== */
/* dhcp struct for the ethernet netif */
static struct dhcp g_netifDhcp[ENET_SYSCFG_NETIF_COUNT];
struct netif *g_pNetif[ENET_SYSCFG_NETIF_COUNT];

extern MmwDemo_MSS_MCB gMmwMssMCB;

/* ========================================================================== */
/*                          Function Definitions                              */
/* ========================================================================== */

int enetTask(void *args)
{
    Enet_Type enetType;
    uint32_t instId;

    EnetApp_getEnetInstInfo(&enetType, &instId);

    EnetAppUtils_enableClocks(enetType, instId);

    const int32_t status = EnetApp_driverOpen(enetType, instId);
    if (ENET_SOK != status)
    {
        EnetAppUtils_print("Failed to open ENET: %d\r\n", status);
        EnetAppUtils_assert(false);
        return -1;
    }

    EnetApp_addMCastEntry(enetType,
                          instId,
                          EnetSoc_getCoreId(),
                          BROADCAST_MAC_ADDRESS,
                          CPSW_ALE_ALL_PORTS_MASK);

    Enet_setupNetworkStack();

    while (false == Enet_isNetworkUp(netif_default))
    {
        DebugP_log("Waiting for network UP ...\r\n");
        ClockP_sleep(2);
    }

    DebugP_log("Network is UP ...\r\n");
    ClockP_sleep(1);
    AppTcp_startClient();

    while (1)
    {
        ClockP_usleep(1000);
    }

    Enet_shutdownNetworkStack();
    return 0;
}

static void Enet_setupNetworkStack()
{
    sys_sem_t pInitSem;
    const err_t err = sys_sem_new(&pInitSem, 0);
    EnetAppUtils_assert(err == ERR_OK);

    tcpip_init(Enet_tcpipInitCompleteCb, &pInitSem);

    /* wait for TCP/IP initialization to complete */
    sys_sem_wait(&pInitSem);
    sys_sem_free(&pInitSem);

    return;
}

static void Enet_shutdownNetworkStack()
{
    LwipifEnetApp_netifClose(NETIF_INST_ID0);
    return;
}

static void Enet_tcpipInitCompleteCb(void *pArg)
{
    sys_sem_t *pSem = (sys_sem_t*)pArg;
    EnetAppUtils_assert(pArg != NULL);

    /* init randomizer again (seed per thread) */
    srand((unsigned int)sys_now()/1000);

    Enet_setupNetif();

    Enet_allocateIPAddress();

    sys_sem_signal(pSem);
}

static void Enet_setupNetif()
{
     ip4_addr_t ipaddr, netmask, gw;

     ip4_addr_set_zero(&gw);
     ip4_addr_set_zero(&ipaddr);
     ip4_addr_set_zero(&netmask);

     DebugP_log("Starting lwIP, local interface IP is dhcp-enabled\r\n");

     for (uint32_t i = 0U; i < ENET_SYSCFG_NETIF_COUNT; i++)
     {
         /* Open the netif and get it populated*/
         LwipifEnetApp_netifOpen(NETIF_INST_ID0 + i, &ipaddr, &netmask, &gw);
         g_pNetif[NETIF_INST_ID0 + i] = LwipifEnetApp_getNetifFromId(NETIF_INST_ID0);
         netif_set_status_callback(g_pNetif[NETIF_INST_ID0 + i], Enet_netifStatusChangeCb);
         netif_set_link_callback(g_pNetif[NETIF_INST_ID0 + i], Enet_netifLinkChangeCb);
         netif_set_up(g_pNetif[NETIF_INST_ID0 + i]);
    }
    LwipifEnetApp_startSchedule(g_pNetif[ENET_SYSCFG_DEFAULT_NETIF_IDX]);
}

static void Enet_allocateIPAddress()
{
    sys_lock_tcpip_core();

    for (uint32_t  i = 0U; i < ENET_SYSCFG_NETIF_COUNT; i++)
    {
        dhcp_set_struct(g_pNetif[NETIF_INST_ID0 + i], &g_netifDhcp[NETIF_INST_ID0 + i]);

        const err_t err = dhcp_start(g_pNetif[NETIF_INST_ID0 + i]);
        EnetAppUtils_assert(err == ERR_OK);
    }
    sys_unlock_tcpip_core();
    return;
}

static void Enet_netifStatusChangeCb(struct netif *pNetif)
{
    if (netif_is_up(pNetif))
    {
        DebugP_log("Enet IF UP Event. Local interface IP:%s\r\n",
                    ip4addr_ntoa(netif_ip4_addr(pNetif)));

        const ip4_addr_t *localIpTemp = netif_ip4_addr(pNetif);
        memcpy(&gMmwMssMCB.enetCfg.localIp, localIpTemp, sizeof(ip4_addr_t));
        gMmwMssMCB.enetCfg.status = 1;
    }
    else
    {
        DebugP_log("Enet IF DOWN Event\r\n");
    }
    return;
}

static void Enet_netifLinkChangeCb(struct netif *pNetif)
{
    if (netif_is_link_up(pNetif))
    {
        DebugP_log("Network Link UP Event\r\n");
    }
    else
    {
        DebugP_log("Network Link DOWN Event\r\n");
    }
    return;
}

static int32_t Enet_isNetworkUp(struct netif* netif_)
{
    return (netif_is_up(netif_) && netif_is_link_up(netif_) && !ip4_addr_isany_val(*netif_ip4_addr(netif_)));
}
