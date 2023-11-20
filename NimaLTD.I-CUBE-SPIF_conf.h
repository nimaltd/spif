/**
  ******************************************************************************
  * File Name          : NimaLTD.I-CUBE-SPIF_conf.h
  * Description        : This file provides code for the configuration
  *                      of the NimaLTD.I-CUBE-SPIF_conf.h instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef _NIMALTD_I_CUBE_SPIF_CONF_H_
#define _NIMALTD_I_CUBE_SPIF_CONF_H_

#ifdef __cplusplus
 extern "C" {
#endif

#define SPIF_DEBUG_DISABLE                    0
#define SPIF_DEBUG_MIN                        1
#define SPIF_DEBUG_FULL                       2

#define SPIF_PLATFORM_HAL                     0
#define SPIF_PLATFORM_HAL_DMA                 1

#define SPIF_CMSIS_RTOS_DISABLE               0
#define SPIF_CMSIS_RTOS_V1                    1
#define SPIF_CMSIS_RTOS_V2                    2

/**
	MiddleWare name : NimaLTD.I-CUBE-SPIF.2.0.0
	MiddleWare fileName : NimaLTD.I-CUBE-SPIF_conf.h
*/
/*---------- SPIF_DEBUG  -----------*/
#define SPIF_DEBUG      SPIF_DEBUG_DISABLE

/*---------- SPIF_PLATFORM  -----------*/
#define SPIF_PLATFORM      SPIF_PLATFORM_HAL

/*---------- SPIF_CMSIS_RTOS  -----------*/
#define SPIF_CMSIS_RTOS      SPIF_CMSIS_RTOS_DISABLE

#ifdef __cplusplus
}
#endif
#endif /* _NIMALTD_I_CUBE_SPIF_CONF_H_ */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

