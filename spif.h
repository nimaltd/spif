#ifndef _SPIF_H_
#define _SPIF_H_

/***********************************************************************************************************

  Author:     Nima Askari
  Github:     https://www.github.com/NimaLTD
  LinkedIn:   https://www.linkedin.com/in/nimaltd
  Youtube:    https://www.youtube.com/@nimaltd
  Instagram:  https://instagram.com/github.NimaLTD

  Version:    2.1.0

  History:
              2.1.0
              - Add Support HAL-DMA
              - Remove SPI_Trasmit function

              2.0.1
              - Remove SPI_Receive function

              2.0.0
              - Rewrite again
              - Support STM32CubeMx Packet installer

***********************************************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <string.h>
#include "NimaLTD.I-CUBE-SPIF_conf.h"
#include "spi.h"

#if SPIF_CMSIS_RTOS == SPIF_CMSIS_RTOS_DISABLE
#define SPIF_Delay(x) HAL_Delay(x)
#elif SPIF_CMSIS_RTOS == SPIF_CMSIS_RTOS_V1
#include "cmsis_os.h"
#define SPIF_Delay(x) osDelay(x)
#else
#include "cmsis_os2.h"
#define SPIF_Delay(x) osDelay(x)
#endif

/***********************************************************************************************************/
/***********************************************************************************************************/
/***********************************************************************************************************/

#define SPIF_PAGE_SIZE                      0x100
#define SPIF_SECTOR_SIZE                    0x1000
#define SPIF_BLOCK_SIZE                     0x10000

#define SPIF_PageToSector(PageNumber)      ((PageNumber * SPIF_PAGE_SIZE) / SPIF_SECTOR_SIZE)
#define SPIF_PageToBlock(PageNumber)       ((PageNumber * SPIF_PAGE_SIZE) / SPIF_BLOCK_SIZE)
#define SPIF_SectorToBlock(SectorNumber)   ((SectorNumber * SPIF_SECTOR_SIZE) / SPIF_BLOCK_SIZE)
#define SPIF_SectorToPage(SectorNumber)    ((SectorNumber * SPIF_SECTOR_SIZE) / SPIF_PAGE_SIZE)
#define SPIF_BlockToPage(BlockNumber)      ((BlockNumber * SPIF_BLOCK_SIZE) / SPIF_PAGE_SIZE)
#define SPIF_PageToAddress(PageNumber)     (PageNumber * SPIF_PAGE_SIZE)
#define SPIF_SectorToAddress(SectorNumber) (SectorNumber * SPIF_SECTOR_SIZE)
#define SPIF_BlockToAddress(BlockNumber)   (BlockNumber * SPIF_BLOCK_SIZE)
#define SPIF_AddressToPage(Address)        (Address / SPIF_PAGE_SIZE)
#define SPIF_AddressToSector(Address)      (Address / SPIF_SECTOR_SIZE)
#define SPIF_AddressToBlock(Address)       (Address / SPIF_BLOCK_SIZE)

typedef enum
{
	SPIF_MANUFACTOR_ERROR = 0,
	SPIF_MANUFACTOR_WINBOND = 0xEF,
	SPIF_MANUFACTOR_ISSI = 0xD5,
	SPIF_MANUFACTOR_MICRON = 0x20,
	SPIF_MANUFACTOR_GIGADEVICE = 0xC8,
	SPIF_MANUFACTOR_MACRONIX = 0xC2,
	SPIF_MANUFACTOR_SPANSION = 0x01,
	SPIF_MANUFACTOR_AMIC = 0x37,
	SPIF_MANUFACTOR_SST = 0xBF,
	SPIF_MANUFACTOR_HYUNDAI = 0xAD,
	SPIF_MANUFACTOR_ATMEL = 0x1F,
	SPIF_MANUFACTOR_FUDAN = 0xA1,
	SPIF_MANUFACTOR_ESMT = 0x8C,
	SPIF_MANUFACTOR_INTEL = 0x89,
	SPIF_MANUFACTOR_SANYO = 0x62,
	SPIF_MANUFACTOR_FUJITSU = 0x04,
	SPIF_MANUFACTOR_EON = 0x1C,
	SPIF_MANUFACTOR_PUYA = 0x85,

} SPIF_ManufactorTypeDef;

typedef enum
{
	SPIF_SIZE_ERROR = 0,
	SPIF_SIZE_1MBIT = 0x11,
	SPIF_SIZE_2MBIT = 0x12,
	SPIF_SIZE_4MBIT = 0x13,
	SPIF_SIZE_8MBIT = 0x14,
	SPIF_SIZE_16MBIT = 0x15,
	SPIF_SIZE_32MBIT = 0x16,
	SPIF_SIZE_64MBIT = 0x17,
	SPIF_SIZE_128MBIT = 0x18,
	SPIF_SIZE_256MBIT = 0x19,
	SPIF_SIZE_512MBIT = 0x20,

} SPIF_SizeTypeDef;

typedef struct
{
	SPI_HandleTypeDef      *HSpi;
	GPIO_TypeDef           *Gpio;
	SPIF_ManufactorTypeDef Manufactor;
	SPIF_SizeTypeDef       Size;
	uint8_t                Inited;
	uint8_t                MemType;
	uint8_t                Lock;
	uint8_t                Reserved;
	uint32_t               Pin;
	uint32_t               PageCnt;
	uint32_t               SectorCnt;
	uint32_t               BlockCnt;

} SPIF_HandleTypeDef;

/***********************************************************************************************************/
/***********************************************************************************************************/
/***********************************************************************************************************/

bool SPIF_Init(SPIF_HandleTypeDef *Handle, SPI_HandleTypeDef *HSpi, GPIO_TypeDef *Gpio, uint16_t Pin);

bool SPIF_EraseChip(SPIF_HandleTypeDef *Handle);
bool SPIF_EraseSector(SPIF_HandleTypeDef *Handle, uint32_t Sector);
bool SPIF_EraseBlock(SPIF_HandleTypeDef *Handle, uint32_t Block);

bool SPIF_WriteAddress(SPIF_HandleTypeDef *Handle, uint32_t Address, uint8_t *Data, uint32_t Size);
bool SPIF_WritePage(SPIF_HandleTypeDef *Handle, uint32_t PageNumber, uint8_t *Data, uint32_t Size, uint32_t Offset);
bool SPIF_WriteSector(SPIF_HandleTypeDef *Handle, uint32_t SectorNumber, uint8_t *Data, uint32_t Size, uint32_t Offset);
bool SPIF_WriteBlock(SPIF_HandleTypeDef *Handle, uint32_t BlockNumber, uint8_t *Data, uint32_t Size, uint32_t Offset);

bool SPIF_ReadAddress(SPIF_HandleTypeDef *Handle, uint32_t Address, uint8_t *Data, uint32_t Size);
bool SPIF_ReadPage(SPIF_HandleTypeDef *Handle, uint32_t PageNumber, uint8_t *Data, uint32_t Size, uint32_t Offset);
bool SPIF_ReadSector(SPIF_HandleTypeDef *Handle, uint32_t SectorNumber, uint8_t *Data, uint32_t Size, uint32_t Offset);
bool SPIF_ReadBlock(SPIF_HandleTypeDef *Handle, uint32_t BlockNumber, uint8_t *Data, uint32_t Size, uint32_t Offset);

#ifdef __cplusplus
}
#endif
#endif
