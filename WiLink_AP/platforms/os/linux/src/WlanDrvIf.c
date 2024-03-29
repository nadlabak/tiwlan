/*
 * WlanDrvIf.c
 *
 * Copyright(c) 1998 - 2010 Texas Instruments. All rights reserved.      
 * All rights reserved.                                                  
 *                                                                       
 * Redistribution and use in source and binary forms, with or without    
 * modification, are permitted provided that the following conditions    
 * are met:                                                              
 *                                                                       
 *  * Redistributions of source code must retain the above copyright     
 *    notice, this list of conditions and the following disclaimer.      
 *  * Redistributions in binary form must reproduce the above copyright  
 *    notice, this list of conditions and the following disclaimer in    
 *    the documentation and/or other materials provided with the         
 *    distribution.                                                      
 *  * Neither the name Texas Instruments nor the names of its            
 *    contributors may be used to endorse or promote products derived    
 *    from this software without specific prior written permission.      
 *                                                                       
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS   
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT     
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT      
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT   
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/*
 * src/esta_drv.c
 *
 * Kernel level portion of eSTA DK Linux module driver
 *
 */


/** \file   WlanDrvIf.c 
 *  \brief  The OS-Dependent interfaces of the WLAN driver with external applications:
 *          - Configuration utilities (including download, configuration and activation)
 *          - Network Stack (Tx and Rx)
 *          - Interrupts
 *          - Events to external applications
 *  
 *  \see    WlanDrvIf.h, Wext.c
 */
#define __FILE_ID__  FILE_ID_138

#include <net/sock.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/netlink.h>
#include <linux/version.h>


#include "WlanDrvIf.h"
#include "osApi.h"
#include "host_platform.h"
#include "context.h"
#include "CmdHndlr.h"
#include "WlanDrvWext.h"
#include "DrvMain.h"
#include "txDataQueue_Api.h"
#include "txMgmtQueue_Api.h"
#include "TWDriver.h"
#include "Ethernet.h"
#include "APExternalIf.h"
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include "RxBuf_linux.h"

#ifdef TI_DBG
#include "tracebuf_api.h"
#endif
/* PM hooks */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
#if defined HOST_PLATFORM_OMAP3430 || defined HOST_PLATFORM_ZOOM2  || defined HOST_PLATFORM_ZOOM1
#include "SdioDrv.h"
static int wlanDrvIf_pm_resume(void);
static int wlanDrvIf_pm_suspend(void);
#endif
#else
#ifdef CONFIG_PM
#include "SdioDrv.h"
static int wlanDrvIf_pm_resume(void);
static int wlanDrvIf_pm_suspend(void);
#endif
#endif /*#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)*/
#include "bmtrace_api.h"
#include "report.h"

/* save driver handle just for module cleanup */
static TWlanDrvIfObj *pDrvStaticHandle;  

#define OS_SPECIFIC_RAM_ALLOC_LIMIT         (0xFFFFFFFF)    /* assume OS never reach that limit */


MODULE_DESCRIPTION("TI WLAN Embedded Station Driver");
MODULE_LICENSE("Dual BSD/GPL");

/*External board parameter*/
int g_external_board = 0;
module_param(g_external_board, int, 0644);
EXPORT_SYMBOL( g_external_board);


/* linux/irq.h declarations */ 
extern void disable_irq(unsigned int);
static int xmit_Bridge (struct sk_buff *skb, struct net_device *dev, TIntraBssBridge *pBssBridgeParam);
//MOTO BEGIN
extern void sdioDrv_register_pm(int (*wlanDrvIf_Start)(void),
                                int (*wlanDrvIf_Stop)(void));
//MOTO END

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 31))
static int wlanDrvIf_Xmit(struct sk_buff *skb, struct net_device *dev);
static int wlanDrvIf_XmitDummy(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *wlanDrvIf_NetGetStat(struct net_device *dev);
int wlanDrvIf_Open(struct net_device *dev);
int wlanDrvIf_Release(struct net_device *dev);

static struct net_device_ops tiwlan_ops_pri = {
	.ndo_open = wlanDrvIf_Open,
	.ndo_stop = wlanDrvIf_Release,
	.ndo_get_stats = wlanDrvIf_NetGetStat,
	.ndo_do_ioctl = NULL,
	.ndo_start_xmit = wlanDrvIf_Xmit,
};

static struct net_device_ops tiwlan_ops_dummy = {
	.ndo_open = wlanDrvIf_Open,
	.ndo_stop = wlanDrvIf_Release,
	.ndo_get_stats = wlanDrvIf_NetGetStat,
	.ndo_do_ioctl = NULL,
	.ndo_start_xmit = wlanDrvIf_XmitDummy,
};
#endif

/** 
 * \fn     wlanDrvIf_Xmit
 * \brief  Packets transmission
 * 
 * The network stack calls this function in order to transmit a packet
 *     through the WLAN interface.
 * The packet is inserted to the drver Tx queues and its handling is continued
 *     after switching to the driver context.
 *
 * \note   
 * \param  skb - The Linux packet buffer structure
 * \param  dev - The driver network-interface handle
 * \return 0 (= OK)
 * \sa     
 */ 
static int wlanDrvIf_Xmit (struct sk_buff *skb, struct net_device *dev)
{

    return xmit_Bridge(skb, dev, NULL);
}


static int xmit_Bridge (struct sk_buff *skb, struct net_device *dev, TIntraBssBridge *pBssBridgeParam)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)NETDEV_GET_PRIVATE(dev);
    TTxCtrlBlk *  pPktCtrlBlk;
    TEthernetHeader *pEthHead = (TEthernetHeader *)(skb->data);
    int status;

    CL_TRACE_START_L1();

    os_profile (drv, 0, 0);
    drv->stats.tx_packets++;
    drv->stats.tx_bytes += skb->len;

    /* Allocate a TxCtrlBlk for the Tx packet and save timestamp, length and packet handle */
    pPktCtrlBlk = TWD_txCtrlBlk_Alloc (drv->tCommon.hTWD);
    if (pPktCtrlBlk == NULL)
    {
        drv->stats.tx_errors++;
        os_profile (drv, 1, 0);
        CL_TRACE_END_L1("tiwlan_drv.ko", "OS", "TX", "");
        return 0;
    }

    /* Set interface type according to net device interface number */
    if (drv->tCommon.eIfRole == IF_ROLE_TYPE_AP) 
        SET_PKT_TYPE_IF_ROLE_AP(pPktCtrlBlk);
    else
        SET_PKT_TYPE_IF_ROLE_STA(pPktCtrlBlk);
    pPktCtrlBlk->tTxDescriptor.startTime    = os_timeStampMs(drv); /* remove use of skb->tstamp.off_usec */
    pPktCtrlBlk->tTxDescriptor.length       = skb->len;
    pPktCtrlBlk->tTxPktParams.pInputPkt     = skb;

    /* Check MGMT packet from hostapd, forward it to the Mgmt-Queue and exit without ethernet header */
    if (HTOWLANS(pEthHead->type) == AP_MGMT_ETH_TYPE)
    {
        /* Copy WLAN header into aPktHdr - format for MGMT packets */
        memcpy (pPktCtrlBlk->aPktHdr, skb->data + ETHERNET_HDR_LEN , WLAN_HDR_LEN );

        /* Skip ethernet header, send as management frame */
        pPktCtrlBlk->tTxPktParams.uPktType = TX_PKT_TYPE_MGMT;
        pPktCtrlBlk->tTxnStruct.aBuf[0] = (TI_UINT8 *)pPktCtrlBlk->aPktHdr;
        pPktCtrlBlk->tTxnStruct.aLen[0] = WLAN_HDR_LEN;
        pPktCtrlBlk->tTxnStruct.aBuf[1] = skb->data + ETHERNET_HDR_LEN + WLAN_HDR_LEN;
        pPktCtrlBlk->tTxnStruct.aLen[1] = (TI_UINT16)skb->len - ETHERNET_HDR_LEN - WLAN_HDR_LEN;
        pPktCtrlBlk->tTxnStruct.aLen[2] = 0;
        pPktCtrlBlk->tTxPktParams.uInputPktLen = skb->len;
        pPktCtrlBlk->tTxDescriptor.length = (TI_UINT16)((pPktCtrlBlk->tTxnStruct.aLen[0]) + (pPktCtrlBlk->tTxnStruct.aLen[1]));

        status = txMgmtQ_Xmit (drv->tCommon.hTxMgmtQ, pPktCtrlBlk, TI_TRUE);
    }
    else

    {
    /* Point the first BDL buffer to the Ethernet header, and the second buffer to the rest of the packet */
    pPktCtrlBlk->tTxnStruct.aBuf[0] = skb->data;
    pPktCtrlBlk->tTxnStruct.aLen[0] = ETHERNET_HDR_LEN;
    pPktCtrlBlk->tTxnStruct.aBuf[1] = skb->data + ETHERNET_HDR_LEN;
    pPktCtrlBlk->tTxnStruct.aLen[1] = (TI_UINT16)skb->len - ETHERNET_HDR_LEN;
    pPktCtrlBlk->tTxnStruct.aLen[2] = 0;

    /* Send the packet to the driver for transmission. */
    status = txDataQ_InsertPacket (drv->tCommon.hTxDataQ, pPktCtrlBlk,(TI_UINT8)skb->priority, pBssBridgeParam);
    }

    /* If failed (queue full or driver not running), drop the packet. */
    if (status != TI_OK)
    {
        drv->stats.tx_errors++;
    }
    os_profile (drv, 1, 0);

    CL_TRACE_END_L1("tiwlan_drv.ko", "OS", "TX", "");

    return 0;
}


/*--------------------------------------------------------------------------------------*/
/** 
 * \fn     wlanDrvIf_FreeTxPacket
 * \brief  Free the OS Tx packet
 * 
 * Free the OS Tx packet after driver processing is finished.
 *
 * \note   
 * \param  hOs          - The OAL object handle
 * \param  pPktCtrlBlk  - The packet CtrlBlk
 * \param  eStatus      - The packet transmission status (OK/NOK)
 * \return void
 * \sa     
 */ 
/*--------------------------------------------------------------------------------------*/

void wlanDrvIf_FreeTxPacket (TI_HANDLE hOs, TTxCtrlBlk *pPktCtrlBlk, TI_STATUS eStatus)
{
    dev_kfree_skb((struct sk_buff *)pPktCtrlBlk->tTxPktParams.pInputPkt);
}

/** 
 * \fn     wlanDrvIf_XmitDummy
 * \brief  Dummy transmission handler
 * 
 * This function is registered at the network stack interface as the packets-transmission
 *     handler (replacing wlanDrvIf_Xmit) when the driver is not operational.
 * Using this dummy handler is more efficient then checking the driver state for every 
 *     packet transmission.
 *
 * \note   
 * \param  skb - The Linux packet buffer structure
 * \param  dev - The driver network-interface handle
 * \return error 
 * \sa     wlanDrvIf_Xmit
 */ 
static int wlanDrvIf_XmitDummy (struct sk_buff *skb, struct net_device *dev)
{
    /* Just return error. The driver is not running (network stack frees the packet) */
    return -ENODEV;
}


/** 
 * \fn     wlanDrvIf_NetGetStat
 * \brief  Provides driver statistics
 * 
 * Provides driver Tx and Rx statistics to network stack.
 *
 * \note   
 * \param  dev - The driver network-interface handle
 * \return The statistics pointer 
 * \sa     
 */ 
static struct net_device_stats *wlanDrvIf_NetGetStat (struct net_device *dev)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)NETDEV_GET_PRIVATE(dev);
    ti_dprintf (TIWLAN_LOG_OTHER, "wlanDrvIf_NetGetStat()\n");
    
    return &drv->stats;
}


/** 
 * \fn     wlanDrvIf_UpdateDriverState
 * \brief  Update the driver state
 * 
 * The DrvMain uses this function to update the OAL with the driver steady state
 *     that is relevant for the driver users.
 * 
 * \note   
 * \param  hOs          - The driver object handle
 * \param  eDriverState - The new driver state
 * \return void
 * \sa     
 */ 
void wlanDrvIf_UpdateDriverState (TI_HANDLE hOs, EDriverSteadyState eDriverState)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    ti_dprintf(TIWLAN_LOG_OTHER, "wlanDrvIf_UpdateDriverState(): State = %d\n", eDriverState);

    /* Save the new state */
    drv->tCommon.eDriverState = eDriverState;

    /* If the new state is not RUNNING, replace the Tx handler to a dummy one. */
    if (eDriverState != DRV_STATE_RUNNING) {
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
        drv->netdev->hard_start_xmit = wlanDrvIf_XmitDummy;
#else
        drv->netdev->netdev_ops = &tiwlan_ops_dummy;
#endif
    }
}


/** 
 * \fn     wlanDrvIf_HandleInterrupt
 * \brief  The WLAN interrupt handler
 * 
 * The WLAN driver interrupt handler called in the interrupt context.
 * The actual handling is done in the driver's context after switching to the workqueue.
 * 
 * \note   
 * \param  irq      - The interrupt type
 * \param  hDrv     - The driver object handle
 * \param  cpu_regs - The CPU registers
 * \return IRQ_HANDLED
 * \sa     
 */ 
irqreturn_t wlanDrvIf_HandleInterrupt (int irq, void *hDrv, struct pt_regs *cpu_regs)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hDrv;

    TWD_InterruptRequest (drv->tCommon.hTWD);

    return IRQ_HANDLED;
}


/** 
 * \fn     PollIrqHandler
 * \brief  WLAN IRQ polling handler - for debug!
 * 
 * A debug option to catch the WLAN events in polling instead of interrupts.
 * A timer calls this function periodically to check the interrupt status register.
 * 
 * \note   
 * \param  parm - The driver object handle
 * \return void
 * \sa     
 */ 
#ifdef PRIODIC_INTERRUPT
static void wlanDrvIf_PollIrqHandler (TI_HANDLE parm)
{
   TWlanDrvIfObj *drv = (TWlanDrvIfObj *)parm;

   wlanDrvIf_HandleInterrupt (0, drv, NULL);
   os_periodicIntrTimerStart (drv);
}
#endif


/** 
 * \fn     wlanDrvIf_DriverTask
 * \brief  The driver task
 * 
 * This is the driver's task, where most of its job is done.
 * External contexts just save required information and schedule the driver's
 *     task to continue the handling.
 * See more information in the context engine module (context.c).
 *
 * \note   
 * \param  hDrv - The driver object handle
 * \return void
 * \sa     
 */ 
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
static void wlanDrvIf_DriverTask (void *hDrv)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hDrv;
#else
static void wlanDrvIf_DriverTask(struct work_struct *work)
{
    TWlanDrvIfObj *drv = container_of(work, TWlanDrvIfObj, tWork);
#endif

    #ifdef STACK_PROFILE
    unsigned int curr1,base1;
    unsigned int curr2,base2;
    static unsigned int maximum_stack = 0;
    #endif   

   
    os_profile (drv, 0, 0);

    #ifdef STACK_PROFILE
    curr1 = check_stack_start(&base1);
    #endif


    /* Call the driver main task */
    context_DriverTask (drv->tCommon.hContext);

	os_wake_lock_timeout(drv);	//MOTO
	os_wake_unlock(drv);		//MOTO

    #ifdef STACK_PROFILE
    curr2 = check_stack_stop(&base2);
    if (base2 == base1)
    {   
       /* if the current measurement is bigger then the maximum store it and print*/
        if ((curr1 - curr2) > maximum_stack)
        {
            printk("STACK PROFILER GOT THE LOCAL MAXIMMUM!!!! \n");
            printk("current operation stack use =%d \n",(curr1 - curr2));
            printk("total stack use=%d \n",8192 - curr2 + base2);
            printk("total stack usage= %d percent \n",100 * (8192 - curr2 + base2) / 8192);
            maximum_stack = curr1 - curr2;
       }
    }
    #endif

    os_profile (drv, 1, 0);
}


/** 
 * \fn     wlanDrvIf_LoadFiles
 * \brief  Load init files from loader
 * 
 * This function is called from the loader context right after the driver
 *     is created (in IDLE state).
 * It copies the following files to the driver's memory:
 *     - Ini-File - The driver's default parameters values
 *     - NVS-File - The NVS data for FW usage
 *     - FW-Image - The FW program image
 *
 * \note   
 * \param  drv - The driver object handle
 * \return void
 * \sa     wlanDrvIf_GetFile
 */ 
int wlanDrvIf_LoadFiles (TWlanDrvIfObj *drv, TLoaderFilesData *pInitFiles)
{
    if (!pInitFiles)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "No Init Files!\n");
        return -EINVAL;
    }

    if (drv->tCommon.eDriverState != DRV_STATE_IDLE)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "Trying to load files not in IDLE state!\n");
        return -EINVAL;
    }

    if (pInitFiles->uIniFileLength) 
    {
        drv->tCommon.tIniFile.uSize = pInitFiles->uIniFileLength;
        drv->tCommon.tIniFile.pImage = kmalloc (pInitFiles->uIniFileLength, GFP_KERNEL);
        #ifdef TI_MEM_ALLOC_TRACE        
          os_printf ("MTT:%s:%d ::kmalloc(%lu, %x) : %lu\n", __FUNCTION__, __LINE__, pInitFiles->uIniFileLength, GFP_KERNEL, pInitFiles->uIniFileLength);
        #endif
        if (!drv->tCommon.tIniFile.pImage)
        {
            ti_dprintf (TIWLAN_LOG_ERROR, "Cannot allocate buffer for Ini-File!\n");
            return -ENOMEM;
        }
        memcpy (drv->tCommon.tIniFile.pImage,
                &pInitFiles->data[pInitFiles->uNvsFileLength + pInitFiles->uFwFileLength],
                drv->tCommon.tIniFile.uSize);
    }

    if (pInitFiles->uNvsFileLength)
    {
        drv->tCommon.tNvsImage.uSize = pInitFiles->uNvsFileLength;
        drv->tCommon.tNvsImage.pImage = kmalloc (drv->tCommon.tNvsImage.uSize, GFP_KERNEL);
        #ifdef TI_MEM_ALLOC_TRACE        
          os_printf ("MTT:%s:%d ::kmalloc(%lu, %x) : %lu\n", 
              __FUNCTION__, __LINE__, drv->tCommon.tNvsImage.uSize, GFP_KERNEL, drv->tCommon.tNvsImage.uSize);
        #endif
        if (!drv->tCommon.tNvsImage.pImage)
        {
            ti_dprintf (TIWLAN_LOG_ERROR, "Cannot allocate buffer for NVS image\n");
            return -ENOMEM;
        }
        memcpy (drv->tCommon.tNvsImage.pImage, &pInitFiles->data[0], drv->tCommon.tNvsImage.uSize );
    }
    
    drv->tCommon.tFwImage.uSize = pInitFiles->uFwFileLength;
    if (!drv->tCommon.tFwImage.uSize)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "No firmware image\n");
        return -EINVAL;
    }
    drv->tCommon.tFwImage.pImage = os_memoryAlloc (drv, drv->tCommon.tFwImage.uSize);
    #ifdef TI_MEM_ALLOC_TRACE        
      os_printf ("MTT:%s:%d ::kmalloc(%lu, %x) : %lu\n", 
          __FUNCTION__, __LINE__, drv->tCommon.tFwImage.uSize, GFP_KERNEL, drv->tCommon.tFwImage.uSize);
    #endif
    if (!drv->tCommon.tFwImage.pImage)
    {
        ti_dprintf(TIWLAN_LOG_ERROR, "Cannot allocate buffer for firmware image\n");
        return -ENOMEM;
    }
    memcpy (drv->tCommon.tFwImage.pImage,
            &pInitFiles->data[pInitFiles->uNvsFileLength],
            drv->tCommon.tFwImage.uSize);

    ti_dprintf(TIWLAN_LOG_OTHER, "--------- Eeeprom=%p(%lu), Firmware=%p(%lu), IniFile=%p(%lu)\n", 
        drv->tCommon.tNvsImage.pImage, drv->tCommon.tNvsImage.uSize, 
        drv->tCommon.tFwImage.pImage,  drv->tCommon.tFwImage.uSize,
        drv->tCommon.tIniFile.pImage,  drv->tCommon.tIniFile.uSize);
    
    return 0;
}


/** 
 * \fn     wlanDrvIf_GetFile
 * \brief  Provides access to a requested init file
 * 
 * Provide the requested file information and call the requester callback.
 * Note that in Linux the files were previously loaded to driver memory 
 *     by the loader (see wlanDrvIf_LoadFiles).
 *
 * \note   
 * \param  hOs       - The driver object handle
 * \param  pFileInfo - The requested file's properties
 * \return TI_OK
 * \sa     wlanDrvIf_LoadFiles
 */ 
int wlanDrvIf_GetFile (TI_HANDLE hOs, TFileInfo *pFileInfo)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    if (drv == NULL || pFileInfo == NULL) {
        ti_dprintf(TIWLAN_LOG_ERROR, "wlanDrv_GetFile: ERROR: Null File Handler, Exiting");
        return TI_NOK;
    }

    /* Future option for getting the FW image part by part */ 
    pFileInfo->hOsFileDesc = NULL;

    /* Fill the file's location and size in the file's info structure */
    switch (pFileInfo->eFileType) 
    {
    case FILE_TYPE_INI: 
        pFileInfo->pBuffer = (TI_UINT8 *)drv->tCommon.tIniFile.pImage; 
        pFileInfo->uLength = drv->tCommon.tIniFile.uSize; 
        break;
    case FILE_TYPE_NVS:     
        pFileInfo->pBuffer = (TI_UINT8 *)drv->tCommon.tNvsImage.pImage; 
        pFileInfo->uLength = drv->tCommon.tNvsImage.uSize; 
        break;
    case FILE_TYPE_FW:
        pFileInfo->pBuffer = (TI_UINT8 *)drv->tCommon.tFwImage.pImage; 
        if(pFileInfo->pBuffer == NULL) {
            ti_dprintf(TIWLAN_LOG_ERROR, "wlanDrv_GetFile: ERROR: Null pimage, exiting");
            return TI_NOK;
        }
        pFileInfo->bLast        = TI_FALSE;
        pFileInfo->uLength  = 0;
        pFileInfo->uOffset              = 0;
        pFileInfo->uChunkBytesLeft      = 0;
        pFileInfo->uChunksLeft          = BYTE_SWAP_LONG( *((TI_UINT32*)(pFileInfo->pBuffer)) );
        /* check uChunksLeft's Validity */
        if (( pFileInfo->uChunksLeft == 0 ) || ( pFileInfo->uChunksLeft > MAX_CHUNKS_IN_FILE ))
        {
            ti_dprintf (TIWLAN_LOG_ERROR, "wlanDrvIf_GetFile() Read Invalid Chunks Left: %d!\n",pFileInfo->uChunksLeft);
            return TI_NOK;
        }
        pFileInfo->pBuffer += DRV_ADDRESS_SIZE;
        /* FALL THROUGH */
    case FILE_TYPE_FW_NEXT:
        /* check dec. validity */
        if ( pFileInfo->uChunkBytesLeft >= pFileInfo->uLength )
        {
            pFileInfo->uChunkBytesLeft      -= pFileInfo->uLength;
        }
        /* invalid Dec. */
        else
        {
            ti_dprintf (TIWLAN_LOG_ERROR, "wlanDrvIf_GetFile() No. of Bytes Left < File Length\n");
            return TI_NOK;
        }
        pFileInfo->pBuffer  += pFileInfo->uLength; 

        /* Finished reading all Previous Chunk */
        if ( pFileInfo->uChunkBytesLeft == 0 )
        {
            /* check dec. validity */
            if ( pFileInfo->uChunksLeft > 0 )
            {
                pFileInfo->uChunksLeft--;
            }
            /* invalid Dec. */
            else
            {
                ti_dprintf (TIWLAN_LOG_ERROR, "No. of Bytes Left = 0 and Chunks Left <= 0\n");
                return TI_NOK;
            }
            /* read Chunk's address */
            pFileInfo->uAddress = BYTE_SWAP_LONG( *((TI_UINT32*)(pFileInfo->pBuffer)) );
            pFileInfo->pBuffer += DRV_ADDRESS_SIZE;
            /* read Portion's length */
            pFileInfo->uChunkBytesLeft = BYTE_SWAP_LONG( *((TI_UINT32*)(pFileInfo->pBuffer)) );
            pFileInfo->pBuffer += DRV_ADDRESS_SIZE;
        }
        /* Reading Chunk is NOT complete */
        else
        {
            pFileInfo->uAddress += pFileInfo->uLength;
        }
        
        if ( pFileInfo->uChunkBytesLeft < OS_SPECIFIC_RAM_ALLOC_LIMIT )
        {
            pFileInfo->uLength = pFileInfo->uChunkBytesLeft;
        }
        else
        {
            pFileInfo->uLength = OS_SPECIFIC_RAM_ALLOC_LIMIT;
        }

        /* If last chunk to download */
        if (( pFileInfo->uChunksLeft == 0 ) && 
            ( pFileInfo->uLength == pFileInfo->uChunkBytesLeft ))
        {
            pFileInfo->bLast = TI_TRUE;
        }

        break;
    }

    /* Call the requester callback */
    if (pFileInfo->fCbFunc)
    {
        pFileInfo->fCbFunc (pFileInfo->hCbHndl);
    }

    return TI_OK;
}


/** 
 * \fn     wlanDrvIf_SetMacAddress
 * \brief  Set STA MAC address
 * 
 * Called by DrvMain from init process.
 * Copies STA MAC address to the network interface structure.
 *
 * \note   
 * \param  hOs      - The driver object handle
 * \param  pMacAddr - The STA MAC address
 * \return TI_OK
 * \sa     wlanDrvIf_LoadFiles
 */ 
void wlanDrvIf_SetMacAddress (TI_HANDLE hOs, TI_UINT8 *pMacAddr)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    /* Copy STA MAC address to the network interface structure */
    MAC_COPY (drv->netdev->dev_addr, pMacAddr);
}


/** 
 * \fn     wlanDrvIf_Start
 * \brief  Start driver
 * 
 * Called by network stack upon opening network interface (ifconfig up).
 * Can also be called from user application or CLI for flight mode.
 * Start the driver initialization process up to OPERATIONAL state.
 *
 * \note   
 * \param  dev - The driver network-interface handle
 * \return 0 if succeeded, error if driver not available
 * \sa     wlanDrvIf_Stop
 */ 
int wlanDrvIf_Start (struct net_device *dev)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)NETDEV_GET_PRIVATE(dev);

    ti_dprintf (TIWLAN_LOG_OTHER, "wlanDrvIf_Start()\n");
    printk("%s\n", __func__);

    if (!drv->tCommon.hDrvMain)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "wlanDrvIf_Start() Driver not created!\n");
        return -ENODEV;
    }


    /* 
     *  Insert Start command in DrvMain action queue, request driver scheduling 
     *      and wait for action completion (all init process).
     */
    os_wake_lock_timeout_enable(drv);	//MOTO
    drvMain_InsertAction (drv->tCommon.hDrvMain, ACTION_TYPE_START);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
     /* 
     *  Finalize network interface setup
     */
    netif_start_queue (dev);
#ifdef AP_MODE_ENABLED
    netif_carrier_on (dev);
#endif


    /* register 3430 PM hooks in our SDIO driver */
#if defined HOST_PLATFORM_OMAP3430 || defined HOST_PLATFORM_ZOOM2 || defined HOST_PLATFORM_ZOOM1
    /*sdioDrv_register_pm(wlanDrvIf_pm_resume, wlanDrvIf_pm_suspend);*/
#endif

#endif
    return 0;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
/** 
 * \fn     wlanDrvIf_Open
 * \brief  Start driver
 * 
 * Called by network stack upon opening network interface (ifconfig up).
 * Can also be called from user application or CLI for flight mode.
 * Start the driver initialization process up to OPERATIONAL state.
 *
 * \note   
 * \param  dev - The driver network-interface handle
 * \return 0 if succeeded, error if driver not available
 * \sa     wlanDrvIf_Release
 */ 
int wlanDrvIf_Open (struct net_device *dev)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)NETDEV_GET_PRIVATE(dev);

    ti_dprintf (TIWLAN_LOG_OTHER, "wlanDrvIf_Open()\n");

    if (!drv->tCommon.hDrvMain)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "wlanDrvIf_Open() Driver not created!\n");
        return -ENODEV;
    }
    if (drv->tCommon.eDriverState == DRV_STATE_FAILED) 
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "Driver in FAILED state!\n");
        return -EPERM;
    }
    if (drv->tCommon.eDriverState != DRV_STATE_RUNNING) 
	{
        wlanDrvIf_Start(dev);
    }
//yangm: maybe should not define at here
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
	drv->netdev->hard_start_xmit = wlanDrvIf_Xmit;
#else
	drv->netdev->netdev_ops = &tiwlan_ops_pri;
#endif
        drv->netdev->addr_len = MAC_ADDR_LEN;
	
#ifndef AP_MODE_ENABLED 
	netif_start_queue (dev); /* Temporal, use wlanDrvIf_Enable/DisableTx in STA mode */
#endif

    /* register 3430 PM hooks in our SDIO driver */
#ifdef CONFIG_PM
    /*sdioDrv_register_pm(wlanDrvIf_pm_resume, wlanDrvIf_pm_suspend);*/
#endif

    return 0;
}
#endif   /*#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)*/
/** 
 * \fn     wlanDrvIf_Stop
 * \brief  Stop driver
 * 
 * Called by network stack upon closing network interface (ifconfig down).
 * Can also be called from user application or CLI for flight mode.
 * Stop the driver and turn off the device.
 *
 * \note   
 * \param  dev - The driver network-interface handle
 * \return 0 (OK)
 * \sa     wlanDrvIf_Start
 */ 
int wlanDrvIf_Stop (struct net_device *dev)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)NETDEV_GET_PRIVATE(dev);

    ti_dprintf (TIWLAN_LOG_OTHER, "wlanDrvIf_Stop()\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
    /* Disable network interface queue */
    netif_stop_queue (dev);
#endif
    /* 
     *  Insert Stop command in DrvMain action queue, request driver scheduling 
     *      and wait for Stop process completion.
     */
    os_wake_lock_timeout_enable(drv);	//MOTO
    drvMain_InsertAction (drv->tCommon.hDrvMain, ACTION_TYPE_STOP);
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
/** 
 * \fn     wlanDrvIf_Release
 * \brief  Stop driver
 * 
 * Called by network stack upon closing network interface (ifconfig down).
 * Can also be called from user application or CLI for flight mode.
 * Stop the driver and turn off the device.
 *
 * \note   
 * \param  dev - The driver network-interface handle
 * \return 0 (OK)
 * \sa     wlanDrvIf_Open
 */ 
int wlanDrvIf_Release (struct net_device *dev)
{
    /* TWlanDrvIfObj *drv = (TWlanDrvIfObj *)NETDEV_GET_PRIVATE(dev); */

    ti_dprintf (TIWLAN_LOG_OTHER, "wlanDrvIf_Release()\n");

    /* Disable network interface queue */
    netif_stop_queue (dev);
    return 0;
}
#endif

/* 3430 PM hooksr */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
#if defined HOST_PLATFORM_OMAP3430 || defined HOST_PLATFORM_ZOOM2 || defined HOST_PLATFORM_ZOOM1
static int wlanDrvIf_pm_resume(void)
{
    return(wlanDrvIf_Start(pDrvStaticHandle->netdev));
}

static int wlanDrvIf_pm_suspend(void)
{
    return(wlanDrvIf_Stop(pDrvStaticHandle->netdev));
}
#endif
#else
#ifdef CONFIG_PM
static int wlanDrvIf_pm_resume(void)
{
    return(wlanDrvIf_Open(pDrvStaticHandle->netdev));
}

static int wlanDrvIf_pm_suspend(void)
{
    wlanDrvIf_Release(pDrvStaticHandle->netdev);
    return(wlanDrvIf_Stop(pDrvStaticHandle->netdev));
}
#endif
#endif  /*#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)*/
/** 
 * \fn     wlanDrvIf_SetupNetif
 * \brief  Setup driver network interface
 * 
 * Called in driver creation process.
 * Setup driver network interface.
 *
 * \note   
 * \param  drv - The driver object handle
 * \return 0 - OK, else - failure
 * \sa     
 */ 
static int wlanDrvIf_SetupNetif (TWlanDrvIfObj *drv)
{
   struct net_device *dev;
   int res;

   /* Allocate network interface structure for the driver */
   dev = alloc_etherdev (0);
   if (dev == NULL)
   {
      ti_dprintf (TIWLAN_LOG_ERROR, "alloc_etherdev() failed\n");
      return -ENOMEM;
   }

   /* Setup the network interface */
   ether_setup (dev);

   NETDEV_SET_PRIVATE(dev,drv);
   drv->netdev = dev;
   strcpy (dev->name, TIWLAN_DRV_IF_NAME);
   netif_carrier_off (dev);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 31))
/* the following is required on at least BSP 23.8 and higher.
    Without it, the Open function of the driver will not be called
    when trying to 'ifconfig up' the interface */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
   dev->validate_addr   = NULL;
#endif

   NETDEV_SET_PRIVATE(dev,drv);
   drv->netdev = dev;
   strcpy (dev->name, TIWLAN_DRV_IF_NAME);
   netif_carrier_off (dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
   dev->open = wlanDrvIf_Start;
   dev->stop = wlanDrvIf_Stop;
#else
   dev->open = wlanDrvIf_Open;
   dev->stop = wlanDrvIf_Release;
#endif
   /*dev->hard_start_xmit = wlanDrvIf_XmitDummy;*/
   dev->hard_start_xmit = wlanDrvIf_Xmit;
   dev->addr_len = MAC_ADDR_LEN;
   dev->get_stats = wlanDrvIf_NetGetStat;
   dev->tx_queue_len = 100;
   dev->do_ioctl = NULL;
#else
    dev->tx_queue_len = 100;
    dev->netdev_ops = &tiwlan_ops_dummy;
#endif 

   /* Initialize Wireless Extensions interface (WEXT) */
   wlanDrvWext_Init (dev);

   res = register_netdev (dev);
   if (res != 0)
   {
      ti_dprintf (TIWLAN_LOG_ERROR, "register_netdev() failed : %d\n", res);
      kfree (dev);
      return res;
   }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
#if defined HOST_PLATFORM_OMAP3430 || defined HOST_PLATFORM_ZOOM2 || defined HOST_PLATFORM_ZOOM1
   sdioDrv_register_pm(wlanDrvIf_pm_resume, wlanDrvIf_pm_suspend);
#endif
#else
#ifdef CONFIG_PM
   sdioDrv_register_pm(wlanDrvIf_pm_resume, wlanDrvIf_pm_suspend);
#endif
#endif
/*
On the latest Kernel there is no more support for the below macro.
*/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
   SET_MODULE_OWNER (dev);
#endif
   return 0;
}

/** 
 * \fn     wlanDrvIf_CommandDone
 * \brief  Free current command semaphore.
 * 
 * This routine is called whenever a command has finished executing and Free current command semaphore.
 *
 * \note   
 * \param  hOs           - The driver object handle
 * \param  pSignalObject - handle to complete mechanism per OS
 * \param  CmdResp_p     - respond structure (TCmdRespUnion) for OSE OS only 
 * \return 0 - OK, else - failure
 * \sa     wlanDrvIf_Destroy
 */ 
void wlanDrvIf_CommandDone (TI_HANDLE hOs, void *pSignalObject, TI_UINT8 *CmdResp_p)
{
    /* Free semaphore */
    os_SignalObjectSet (hOs, pSignalObject);
}


/** 
 * \fn     wlanDrvIf_Create
 * \brief  Create the driver instance
 * 
 * Allocate driver object.
 * Initialize driver OS resources (IRQ, workqueue, events socket)
 * Setup driver network interface.
 * Create and link all driver modules.
 *
 * \note   
 * \param  void
 * \return 0 - OK, else - failure
 * \sa     wlanDrvIf_Destroy
 */ 
static int wlanDrvIf_Create (void)
{
    TWlanDrvIfObj *drv;
    int rc;

    /* Allocate driver's structure */
    drv = kmalloc (sizeof(TWlanDrvIfObj), GFP_KERNEL);
    if (!drv)
    {
        return -ENOMEM;
    }
#ifdef TI_DBG
    tb_init(TB_OPTION_NONE);
#endif
    pDrvStaticHandle = drv;  /* save for module destroy */
    #ifdef TI_MEM_ALLOC_TRACE        
      os_printf ("MTT:%s:%d ::kmalloc(%lu, %x) : %lu\n", __FUNCTION__, __LINE__, sizeof(TWlanDrvIfObj), GFP_KERNEL, sizeof(TWlanDrvIfObj));
    #endif
    memset (drv, 0, sizeof(TWlanDrvIfObj));

//MOTO BEGIN
    /* Dm:   drv->irq = TNETW_IRQ; */
//MOTO END
    drv->tCommon.eDriverState = DRV_STATE_IDLE;
#ifdef AP_MODE_ENABLED
    /* for STA role, need to allocate another driver and to set STA role */
    drv->tCommon.eIfRole = IF_ROLE_TYPE_AP;
#endif
//MOTO BEGIN
 drv->pWorkQueue = create_singlethread_workqueue (TIWLAN_DRV_NAME);
       if (!drv->pWorkQueue) {
               kfree (drv);
               ti_dprintf (TIWLAN_LOG_ERROR, "wlanDrvIf_Create(): Failed to create workQ!\n");
               return -ENOMEM;
       }
       drv->wl_packet = 0;
       drv->wl_count = 0;
#ifdef CONFIG_HAS_WAKELOCK
       wake_lock_init(&drv->wl_wifi, WAKE_LOCK_SUSPEND, "wifi_wake");
       wake_lock_init(&drv->wl_rxwake, WAKE_LOCK_SUSPEND, "wifi_rx_wake");
#endif
//MOTO END
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
    INIT_WORK(&drv->tWork, wlanDrvIf_DriverTask, (void *)drv);
#else
    INIT_WORK(&drv->tWork, wlanDrvIf_DriverTask);
#endif
    spin_lock_init (&drv->lock);

    /* Setup driver network interface. */
    rc = wlanDrvIf_SetupNetif (drv);
    if (rc)
    {
        kfree (drv);
        return rc;
    }
   

    /* Create the events socket interface */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
    drv->wl_sock = netlink_kernel_create( NETLINK_USERSOCK, 0, NULL, THIS_MODULE );
#else
        drv->wl_sock = netlink_kernel_create(&init_net, NETLINK_USERSOCK, 0, NULL, NULL, THIS_MODULE );
#endif
    if (drv->wl_sock == NULL)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "netlink_kernel_create() failed !\n");
        return -EINVAL;
    }

    /* Create all driver modules and link their handles */
    drvMain_Create (drv, 
                    &drv->tCommon.hDrvMain, 
                    &drv->tCommon.hCmdHndlr, 
                    &drv->tCommon.hContext, 
                    &drv->tCommon.hTxDataQ,
                    &drv->tCommon.hTxMgmtQ,
                    &drv->tCommon.hTxCtrl,
                    &drv->tCommon.hTWD,
                    &drv->tCommon.hEvHandler,
                    &drv->tCommon.hCmdDispatch,
                    &drv->tCommon.hReport);

    /* 
     *  Initialize interrupts (or polling mode for debug):
     */
#ifdef PRIODIC_INTERRUPT
    /* Debug mode: Polling (the timer is started by HwInit process) */
    drv->hPollTimer = os_timerCreate ((TI_HANDLE)drv, wlanDrvIf_PollIrqHandler, (TI_HANDLE)drv);
#else 
    /* Normal mode: Interrupts (the default mode) */
    rc = hPlatform_initInterrupt (drv, (void*)wlanDrvIf_HandleInterrupt);
    if (rc)
    {
        ti_dprintf (TIWLAN_LOG_ERROR, "wlanDrvIf_Create(): Failed to register interrupt handler!\n");
        return rc;
    }
#ifdef  HOST_PLATFORM_OMAP2430
    set_irq_type (drv->irq, IRQT_FALLING);
#endif
#endif  /* PRIODIC_INTERRUPT */

   return 0;
}


/** 
 * \fn     wlanDrvIf_Destroy
 * \brief  Destroy the driver instance
 * 
 * Destroy all driver modules.
 * Release driver OS resources (IRQ, workqueue, events socket)
 * Release driver network interface.
 * Free init files memory.
 * Free driver object.
 *
 * \note   
 * \param  drv - The driver object handle
 * \return void
 * \sa     wlanDrvIf_Create
 */ 
static void wlanDrvIf_Destroy (TWlanDrvIfObj *drv)
{
    /* Release the driver network interface */
//printk("   wlanDrvIf_Destroy enter \n");
 if (drv->netdev)
    {
//printk("   wlanDrvIf_Destroy enter 2\n");  
   netif_stop_queue  (drv->netdev);
//printk("   wlanDrvIf_Destroy cp1 \n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
        wlanDrvIf_Stop    (drv->netdev);
//printk("   wlanDrvIf_Destroy cp2 \n");
#endif
        unregister_netdev (drv->netdev);
//printk("   wlanDrvIf_Destroy cp3 \n");
        free_netdev(drv->netdev);
//printk("   wlanDrvIf_Destroy cp4 \n");
    }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,21))
//printk("   wlanDrvIf_Destroy cp5 \n");
    cancel_work_sync (&drv->tWork); 
#else
//printk("   wlanDrvIf_Destroy cp6 \n");
    cancel_delayed_work (&drv->tWork);
#endif
    //TODO: Yang Ming. check with STA code again.
    /*
    flush_workqueue (drv->pWorkQueue);
    destroy_workqueue (drv->pWorkQueue);
    */
//printk("   wlanDrvIf_Destroy cp7 \n");
    /* Destroy all driver modules */
    if (drv->tCommon.hDrvMain)
    {
 //printk("   wlanDrvIf_Destroy cp8 \n");
        drvMain_Destroy (drv->tCommon.hDrvMain);
    }
//printk("   wlanDrvIf_Destroy cp9 \n");
    /* close the ipc_kernel socket*/
    if (drv && drv->wl_sock) 
    {
  //     printk("   wlanDrvIf_Destroy cp10 \n");
        sock_release (drv->wl_sock->sk_socket);
    }
//printk("   wlanDrvIf_Destroy cp11 \n");
    /* Release the driver interrupt (or polling timer) */
#ifdef PRIODIC_INTERRUPT
//printk("   wlanDrvIf_Destroy cp12 \n");
    os_timerDestroy (drv, drv->hPollTimer);
#else
    if (drv->irq)
    {
//printk("   wlanDrvIf_Destroy cp13 \n");
//        free_irq (drv->irq, drv);
        hPlatform_freeInterrupt (drv);	//MOTO
//printk("   wlanDrvIf_Destroy cp14 \n");
    }
#endif
//MOTO BEGIN
    if (drv->pWorkQueue)
//printk("   wlanDrvIf_Destroy cp15 \n");
        destroy_workqueue(drv->pWorkQueue);
//printk("   wlanDrvIf_Destroy cp16 \n");
#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_destroy(&drv->wl_wifi);
	wake_lock_destroy(&drv->wl_rxwake);
#endif
//MOTO END
//printk("   wlanDrvIf_Destroy cp17 \n");
    /* 
     *  Free init files memory
     */
    if (drv->tCommon.tFwImage.pImage)
    {
        os_memoryFree (drv, drv->tCommon.tFwImage.pImage, drv->tCommon.tFwImage.uSize);
        #ifdef TI_MEM_ALLOC_TRACE        
          os_printf ("MTT:%s:%d ::kfree(0x%p) : %d\n", 
              __FUNCTION__, __LINE__, drv->tCommon.tFwImage.uSize, -drv->tCommon.tFwImage.uSize);
        #endif
    }
//printk("   wlanDrvIf_Destroy cp18 \n");
    if (drv->tCommon.tNvsImage.pImage)
    {
        kfree (drv->tCommon.tNvsImage.pImage);
        #ifdef TI_MEM_ALLOC_TRACE        
          os_printf ("MTT:%s:%d ::kfree(0x%p) : %d\n", 
              __FUNCTION__, __LINE__, drv->tCommon.tNvsImage.uSize, -drv->tCommon.tNvsImage.uSize);
        #endif
    }
//printk("   wlanDrvIf_Destroy cp19 \n");
    if (drv->tCommon.tIniFile.pImage)
    {
        kfree (drv->tCommon.tIniFile.pImage);
        #ifdef TI_MEM_ALLOC_TRACE        
          os_printf ("MTT:%s:%d ::kfree(0x%p) : %d\n", 
              __FUNCTION__, __LINE__, drv->tCommon.tIniFile.uSize, -drv->tCommon.tIniFile.uSize);
        #endif
    }
//printk("   wlanDrvIf_Destroy cp20 \n");
    /* Free the driver object */
#ifdef TI_DBG
    tb_destroy();
#endif  
    kfree (drv);
}


/** 
 * \fn     wlanDrvIf_ModuleInit  &  wlanDrvIf_ModuleExit
 * \brief  Linux Init/Exit functions
 * 
 * The driver Linux Init/Exit functions (insmod/rmmod) 
 *
 * \note   
 * \param  void
 * \return Init: 0 - OK, else - failure.   Exit: void
 * \sa     wlanDrvIf_Create, wlanDrvIf_Destroy
 */ 
//MOTO BEGIN
#ifndef TI_SDIO_STANDALONE
static int sdc_ctrl = 2;
module_param(sdc_ctrl, int, S_IRUGO | S_IWUSR | S_IWGRP);

extern int sdioDrv_init(int sdcnum);
extern void sdioDrv_exit(void);
#endif
//MOTO END
static int __init wlanDrvIf_ModuleInit (void)
{
    printk(KERN_INFO "TIWLAN: driver init\n");
//MOTO BEGIN
#ifndef TI_SDIO_STANDALONE
#ifndef CONFIG_MMC_EMBEDDED_SDIO
    sdioDrv_init(sdc_ctrl);
#endif
#endif
//MOTO END

    return wlanDrvIf_Create ();
}

static void __exit wlanDrvIf_ModuleExit (void)
{
//printk("   wlanDrvIf_exit cp1 \n");
    wlanDrvIf_Destroy (pDrvStaticHandle);

//MOTO BEGIN
#ifndef TI_SDIO_STANDALONE
#ifndef CONFIG_MMC_EMBEDDED_SDIO
//printk("   wlanDrvIf_exit cp2 \n");
    sdioDrv_exit();

//printk("   wlanDrvIf_exit cp3 \n");
#endif
#endif
//MOTO END
    printk (KERN_INFO "TI WLAN: driver unloaded\n");
}


/** 
 * \fn     wlanDrvIf_StopTx
 * \brief  block Tx thread until wlanDrvIf_ResumeTx called .
 * 
 * This routine is called whenever we need to stop the network stack to send us pakets since one of our Q's is full.
 *
 * \note   
 * \param  hOs           - The driver object handle
* \return 
 * \sa     wlanDrvIf_StopTx
 */ 
void wlanDrvIf_StopTx (TI_HANDLE hOs)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    netif_stop_queue (drv->netdev);
}

/** 
 * \fn     wlanDrvIf_ResumeTx
 * \brief  Resume Tx thread .
 * 
 * This routine is called whenever we need to resume the network stack to send us pakets since our Q's are empty.
 *
 * \note   
 * \param  hOs           - The driver object handle
 * \return 
 * \sa     wlanDrvIf_ResumeTx
 */ 
void wlanDrvIf_ResumeTx (TI_HANDLE hOs)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    netif_wake_queue (drv->netdev);
}

/** 
 * \fn     wlanDrvIf_EnableTx
 * \brief  Resume Tx thread .
 * 
 * This routine is called when driver is ready to accept Tx
 * packets from network device
 *
 */ 
void wlanDrvIf_EnableTx (TI_HANDLE hOs)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    netif_carrier_on(drv->netdev);
    netif_wake_queue (drv->netdev);
}

/** 
 * \fn     wlanDrvIf_DisableTx
 * \brief  Resume Tx thread .
 * 
 * This routine is called when driver wills to stop Tx from
 * network device
 *
 */ 
void wlanDrvIf_DisableTx (TI_HANDLE hOs)
{
    TWlanDrvIfObj *drv = (TWlanDrvIfObj *)hOs;

    netif_stop_queue (drv->netdev);
    netif_carrier_off(drv->netdev);
}

/** 
 * \fn     wlanDrvIf_receivePacket
 * \brief  Receive packet from from lower level
 * 
 */ 
TI_BOOL wlanDrvIf_receivePacket(TI_HANDLE OsContext, void *pRxDesc ,void *pPacket, TI_UINT16 Length, TIntraBssBridge *pBridgeDecision)
{
   TWlanDrvIfObj  *drv     = (TWlanDrvIfObj *)OsContext;
   unsigned char  *pdata   = (unsigned char *)((TI_UINT32)pRxDesc & ~(TI_UINT32)0x3);
   rx_head_t      *rx_head = (rx_head_t *)(pdata -  WSPI_PAD_BYTES - RX_HEAD_LEN_ALIGNED);
   struct sk_buff *skb     = rx_head->skb;
   struct sk_buff *new_skb;
   EIntraBssBridgeDecision eBridge = INTRA_BSS_BRIDGE_NO_BRIDGE;


   skb->data = pPacket;
   /*IKPRODFOURVLA-127 Begin Ti Fix ftp port issue*/
   skb_reset_tail_pointer(skb); // adding this line to adjust the length.
   /*End TI fix IKPRODFOURVLA-127*/
   skb_put(skb, Length);


   skb->dev       = drv->netdev;

   drv->stats.rx_packets++;
   drv->stats.rx_bytes += skb->len;
   /* Intra BSS bridge section */
   if(pBridgeDecision != NULL) 
   {
       eBridge = pBridgeDecision->eDecision;
   }
   if(INTRA_BSS_BRIDGE_NO_BRIDGE == eBridge) 
   {
       /* Forward packet to network stack*/
       CL_TRACE_START_L1();
       skb->protocol  = eth_type_trans(skb, drv->netdev);
       skb->ip_summed = CHECKSUM_NONE;
       netif_rx_ni(skb);

       /* Note: Don't change this trace (needed to exclude OS processing from Rx CPU utilization) */
       CL_TRACE_END_L1("tiwlan_drv.ko", "OS", "RX", "");

   }
   else if( INTRA_BSS_BRIDGE_UNICAST == eBridge) 
   {
       /* Send packet to Tx */
       TRACE2(drv->tCommon.hReport, REPORT_SEVERITY_WARNING, " wlanDrvIf_receivePacket() Unicast Bridge data=0x%x len=%d  \n", RX_ETH_PKT_DATA(pPacket), RX_ETH_PKT_LEN(pPacket));
       xmit_Bridge (skb, pDrvStaticHandle->netdev, pBridgeDecision);
   }
   else /* Broadcast/Multicast packet*/
   {
       /* Duplicate packet*/
       new_skb = skb_clone(skb, GFP_ATOMIC);
       skb->protocol  = eth_type_trans(skb, drv->netdev);
       skb->ip_summed = CHECKSUM_NONE;
       netif_rx_ni(skb);

       if(new_skb) 
       {
           xmit_Bridge (new_skb, pDrvStaticHandle->netdev, pBridgeDecision);
       }
       else
       {
           printk (KERN_ERR "%s: skb_clone failed\n", __FUNCTION__);
           return TI_FALSE;
       }
   }
   return TI_TRUE;
}


module_init (wlanDrvIf_ModuleInit);
module_exit (wlanDrvIf_ModuleExit);

