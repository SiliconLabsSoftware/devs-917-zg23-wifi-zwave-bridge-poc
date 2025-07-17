/*******************************************************************************
 * @file  ZW_PRNG.h
 * @brief
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#ifndef _ZW_SECURITY_AES_MODULE_H_
#define _ZW_SECURITY_AES_MODULE_H_

/*=============================   PRNGInit   =================================
**    PRNGInit
**
**    Side effects :
**
**--------------------------------------------------------------------------*/
extern void PRNGInit(void);
extern void PRNGOutput(BYTE *pDest);

/*==============================   InitSecurity   ============================
**    Initialization of the Security module, can be called in ApplicationInitSW
**
**    This is an application function example
**
**--------------------------------------------------------------------------*/
extern void InitPRNG();

extern void GetRNGData(BYTE *pRNDData, BYTE noRNDDataBytes);

/**
 * }@
 */

#endif
