#ifndef _MIOS32_LPC17XX_TINYUSB_CHIP_H
#define _MIOS32_LPC17XX_TINYUSB_CHIP_H

#include <LPC17xx.h>

/*
 * TinyUSB's LPC17/40 DCD uses the shorter LPCOpen register member names.
 * The last official LPC1700 DFP describes the same registers with a USB
 * prefix, so map only the names needed by that unmodified upstream DCD.
 */
#define CmdCode       USBCmdCode
#define CmdData       USBCmdData
#define Ctrl          USBCtrl
#define DevIntClr     USBDevIntClr
#define DevIntEn      USBDevIntEn
#define DevIntSt      USBDevIntSt
#define DMAIntEn      USBDMAIntEn
#define DMAIntSt      USBDMAIntSt
#define DMARClr       USBDMARClr
#define DMARSet       USBDMARSet
#define EoTIntClr     USBEoTIntClr
#define EoTIntSt      USBEoTIntSt
#define EpDMADis      USBEpDMADis
#define EpDMAEn       USBEpDMAEn
#define EpInd         USBEpInd
#define EpIntClr      USBEpIntClr
#define EpIntEn       USBEpIntEn
#define EpIntPri      USBEpIntPri
#define EpIntSt       USBEpIntSt
#define MaxPSize      USBMaxPSize
#define NDDRIntClr    USBNDDRIntClr
#define ReEp          USBReEp
#define RxData        USBRxData
#define RxPLen        USBRxPLen
#define SysErrIntClr  USBSysErrIntClr
#define TxData        USBTxData
#define TxPLen        USBTxPLen
#define UDCAH         USBUDCAH

#endif /* _MIOS32_LPC17XX_TINYUSB_CHIP_H */
