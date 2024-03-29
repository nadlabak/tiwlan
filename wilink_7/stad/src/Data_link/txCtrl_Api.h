/*
 * txCtrl_Api.h
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

 
/***************************************************************************/
/*                                                                         */
/*    MODULE:   txCtrl_Api.h                                               */
/*    PURPOSE:  TxCtrl module API Header file                              */
/*                                                                         */
/***************************************************************************/
#ifndef _TX_CTRL_API_H_
#define _TX_CTRL_API_H_

#include "GeneralUtilApi.h"
#include "DrvMainModules.h"



#define TSM_REPORT_NUM_OF_BINS_MAX  	                        6
#define TSM_REPORT_NUM_OF_TID_MAX  	                            8
#define TSM_REPORT_NUM_OF_MEASUREMENT_IN_PARALLEL_MAX  	        3


#define TSM_REQUEST_TRIGGER_CONDITION_AVERAGE  	                BIT_0
#define TSM_REQUEST_TRIGGER_CONDITION_CONSECUTIVE               BIT_1
#define TSM_REQUEST_TRIGGER_CONDITION_DELAY  	                BIT_2


typedef struct
{
    TI_UINT32   binDelayThreshold[TSM_REPORT_NUM_OF_BINS_MAX];/* The thresholds for the delay histogram to be reported in the TSM  */
    TI_UINT32   binsDelayCounter[TSM_REPORT_NUM_OF_BINS_MAX]; /* Every packet delay will increase the corresponding bin counter */
    TI_UINT8    uMaxConsecutiveDelayedPktsThr;     /* The number of consecutive delayed packets threshold which will cause triggering TSM report */
    TI_UINT8    uMaxConsecutiveDelayedPktsCounter; /* The number of consecutive delayed packets counter  */

    
    /* bit field : bit0: average, bit1: consecutive, bit2: delay */
    TI_UINT8    uDelayThreshold; /* in TUs (1024 useconds)  */
    TI_UINT8    uAverageErrorThreshold; 
    TI_UINT8    uConsecutiveErrorThreshold;

    TI_UINT8    uConsecutiveDiscardedMsduCounter;
    TI_UINT8    uAverageDiscardedMsduCredit;
    
    TI_UINT8    uPktCrossedDelayThrCounter;        /* The actual number of consecutive MSDUs crossed the Delay Threshold */
    TI_UINT32   uMsduTotalCounter;                 /* the number of MSDU that were successfully or not transmitted */
    TI_UINT32   uMsduTransmittedOKCounter;         /* the number of MSDU that were successfully transmitted */
    TI_UINT32   uMsduRetryExceededCounter;         /* the number of transmit attempts exceeding short/long retry limit */ 
    TI_UINT32   uMsduMultipleRetryCounter;         /* more than one retransmission attempt */
    TI_UINT32   uMsduLifeTimeExpiredCounter;       /* the number of transmit attempts with lifetime expired */
    TI_BOOL     bTSMInProgress;                    /* flag which specifies that the measurement for this TID has started */
    TI_BOOL     bIsTriggeredReport;
    TI_UINT8    measurementToken;
    TI_UINT8    measurementCount;
    TI_UINT16   measurementDuration;
    
    TI_UINT8    actualMeasurementTSF[8];
    TI_UINT8    frameToken;
    TI_UINT16   frameNumOfRepetitions;
    TMacAddr    peerSTA;
    TI_UINT8    uTID;
    TI_UINT8    reportingReason;
    TI_UINT8    bin0Range;
    TI_UINT8    triggerCondition;
   

    
} TSMReportData_t;


/* Callback function definition for triggering the TSM report towards the RRM module */
typedef void (* tTriggerTSMReport)(TI_HANDLE hMeasurementMgr, TSMReportData_t *pTSMReport);

typedef struct
{
    TI_UINT8           triggerCondition;
    TI_UINT8           averageErrorThreshold;
    TI_UINT8           consecutiveErrorThreshold;
    TI_UINT8           delayThreshold;
    TI_UINT8           measuCount;
    TI_UINT8           triggerTimeout;

} TSMTriggeredReportField_t;

typedef struct
{
    TI_BOOL                         bIsTriggeredReport;
    TI_UINT8                        frameToken;
    TI_UINT16                       frameNumOfRepetitions;
    TI_UINT8                        measurementToken;
    TI_UINT16                       randomInterval;
    TI_UINT16                       uDuration;
    TI_UINT8                        uTID;
    TI_UINT8                        uBin0Range; 
    TI_UINT8                        uDelayedMsduRange; 
    TI_UINT8                        uDelayedMsduCount;
    TMacAddr                        peerSTA;
    TSMTriggeredReportField_t       tTriggerReporting;
    tTriggerTSMReport               fCB;
} TSMParams_t;


/* TxCtrl Xmit results */
typedef enum
{
    STATUS_XMIT_SUCCESS,
    STATUS_XMIT_BUSY,
    STATUS_XMIT_ERROR
} EStatusXmit;


typedef struct 
{
	TI_BOOL    bHtEnable;	                        /* current flag of HT Capabilities enabled */
    TI_UINT32  uTxCtrlHtControl;        	        /* The HT Control Field for futur use. for now empty and the FW set it */
} TtxCtrlHtControl;


/* Build the buffers descriptor of a typical two buffers (header & data) Tx packet */
#define BUILD_TX_TWO_BUF_PKT_BDL(pPktCtrlBlk, pHdrBuf, uHdrLen, pDataBuf, uDataLen)  \
    pPktCtrlBlk->tTxnStruct.aBuf[0]   = (TI_UINT8 *) (pHdrBuf);    \
    pPktCtrlBlk->tTxnStruct.aLen[0]   = (TI_UINT16 ) (uHdrLen);    \
    pPktCtrlBlk->tTxnStruct.aBuf[1]   = (TI_UINT8 *) (pDataBuf);   \
    pPktCtrlBlk->tTxnStruct.aLen[1]   = (TI_UINT16 ) (uDataLen);   \
    pPktCtrlBlk->tTxnStruct.aLen[2]   = 0;                         \
    pPktCtrlBlk->tTxDescriptor.length = (TI_UINT16)((uHdrLen) + (uDataLen));


/****************************************************************/
/*                  MODULE  PUBLIC  FUNCTIONS                   */
/****************************************************************/ 

/* 
 *  The TxCtrl MAIN public functions (in txCtrl.c):
 */
TI_HANDLE txCtrl_Create (TI_HANDLE hOs);
void      txCtrl_Init (TStadHandlesList *pStadHandles);
TI_STATUS txCtrl_SetDefaults (TI_HANDLE hTxCtrl, txDataInitParams_t *txDataInitParams);
TI_STATUS txCtrl_Unload (TI_HANDLE hTxCtrl);
EStatusXmit txCtrl_XmitData (TI_HANDLE hTxCtrl, TTxCtrlBlk *pPktCtrlBlk);
TI_STATUS txCtrl_XmitMgmt (TI_HANDLE hTxCtrl, TTxCtrlBlk *pPktCtrlBlk);
void      txCtrl_UpdateQueuesMapping (TI_HANDLE hTxCtrl);
void *    txCtrl_AllocPacketBuffer (TI_HANDLE hTxCtrl, TTxCtrlBlk *pPktCtrlBlk, TI_UINT32 uPacketLen);
void      txCtrl_FreePacket (TI_HANDLE hTxCtrl, TTxCtrlBlk *pPktCtrlBlk, TI_STATUS eStatus);
TI_STATUS txCtrl_NotifyFwReset(TI_HANDLE hTxCtrl);
TI_STATUS txCtrl_CheckForTxStuck(TI_HANDLE hTxCtrl);
TI_UINT32 txCtrl_BuildDataPktHdr (TI_HANDLE hTxCtrl, TTxCtrlBlk *pPktCtrlBlk, AckPolicy_e ackPolicy);

/* TSM (802.11k measurements) */
TI_STATUS txCtrl_UpdateTSMParameters(TI_HANDLE          hTxCtrl,
                                     TSMParams_t        *pTSMParams,
                                     tTriggerTSMReport  fCB);


/* 
 *  The txCtrlParams.c sub-module public functions:
 */
void      txCtrlParams_resetCounters(TI_HANDLE hTxCtrl);
TI_HANDLE txCtrlParams_RegNotif(TI_HANDLE hTxCtrl, 
                                TI_UINT16 EventMask, 
                                GeneralEventCall_t CallBack,
                                TI_HANDLE context, 
                                TI_UINT32 Cookie);
TI_STATUS txCtrlParams_AddToNotifMask(TI_HANDLE hTxCtrl, TI_HANDLE Notifh, TI_UINT16 EventMask);
TI_STATUS txCtrlParams_UnRegNotif(TI_HANDLE hTxCtrl, TI_HANDLE RegEventHandle);
TI_STATUS txCtrlParams_setAdmissionCtrlParams(TI_HANDLE hTxCtrl, 
                                              TI_UINT8 acId, 
                                              TI_UINT16 mediumTime, 
                                              TI_UINT32 minimumPHYRate, 
                                              TI_BOOL admFlag);
TI_STATUS txCtrlParams_getParam(TI_HANDLE hTxCtrl, paramInfo_t *pParamInfo);    
TI_STATUS txCtrlParams_setParam(TI_HANDLE hTxCtrl, paramInfo_t *pParamInfo);
TI_STATUS txCtrlParams_SetHtControl (TI_HANDLE hTxCtrl, TtxCtrlHtControl *pHtControl);
void txCtrlParams_setBssId (TI_HANDLE hTxCtrl, TMacAddr *pCurrBssId);
void txCtrlParams_setBssType (TI_HANDLE hTxCtrl, ScanBssType_e currBssType);
void txCtrlParams_setQosHeaderConverMode (TI_HANDLE hTxCtrl, EHeaderConvertMode  headerConverMode);
void txCtrlParams_setCurrentPrivacyInvokedMode (TI_HANDLE hTxCtrl, TI_BOOL currentPrivacyInvokedMode);
void txCtrlParams_setEapolEncryptionStatus (TI_HANDLE hTxCtrl, TI_BOOL eapolEncryptionStatus);
void txCtrlParams_setEncryptionFieldSizes (TI_HANDLE hTxCtrl, TI_UINT8 encryptionFieldSize);
void txCtrlParams_getCurrentEncryptionInfo (TI_HANDLE hTxCtrl, 
                                            TI_BOOL    *pCurrentPrivacyInvokedMode,
                                            TI_UINT8   *pEncryptionFieldSize);
ERate txCtrlParams_GetTxRate (TI_HANDLE hTxCtrl);
void txCtrlParams_setAcAdmissionStatus (TI_HANDLE hTxCtrl, 
                                        TI_UINT8 ac, 
                                        EAdmissionState admissionRequired,
                                        ETrafficAdmState admissionState);
void txCtrlParams_setDowngradeStatus(TI_HANDLE hTxCtrl,
                                     TI_UINT32 ac,
                                     TI_BOOL bDowngraded);
void txCtrlParams_setAcMsduLifeTime (TI_HANDLE hTxCtrl, TI_UINT8 ac, TI_UINT32 msduLifeTime);
void txCtrlParams_setAcAckPolicy (TI_HANDLE hTxCtrl, TI_UINT8 ac, AckPolicy_e ackPolicy);
void txCtrlParams_updateMgmtRateAttributes(TI_HANDLE hTxCtrl, TI_UINT8 ratePolicyId, TI_UINT8 ac);
void txCtrlParams_updateDataRateAttributes(TI_HANDLE hTxCtrl, TI_UINT8 ratePolicyId, TI_UINT8 ac);
void txCtrlParams_updateTxSessionCount(TI_HANDLE hTxCtrl, TI_UINT16 txSessionCount);
#ifdef TI_DBG
void txCtrlParams_printInfo(TI_HANDLE hTxCtrl);
void txCtrlParams_printDebugCounters(TI_HANDLE hTxCtrl);
void txCtrlParams_resetDbgCounters(TI_HANDLE hTxCtrl);
#endif /* TI_DBG */


/* 
 *  The txCtrlServ.c sub-module public functions:
 */
TI_STATUS txCtrlServ_buildNullFrame(TI_HANDLE hTxCtrl, TI_UINT8* pFrame, TI_UINT32* pLength);
TI_STATUS txCtrlServ_buildWlanHeader(TI_HANDLE hTxCtrl, TI_UINT8* pFrame, TI_UINT32* pLength);

#endif /* _TX_CTRL_API_H_ */
