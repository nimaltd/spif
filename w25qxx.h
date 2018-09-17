#ifndef _W25QXX_H
#define _W25QXX_H

#include <stdbool.h>
#include "spi.h"


typedef enum
{
	W25Q10=1,
	W25Q20,
	W25Q40,
	W25Q80,
	W25Q16,
	W25Q32,
	W25Q64,
	W25Q128,
	W25Q256,
	W25Q512,
	
}W25QXX_ID_t;

typedef struct
{
	W25QXX_ID_t	ID;
	uint8_t			UniqID[8];
	
	
	uint8_t	StatusRegister1;
	uint8_t	StatusRegister2;
	uint8_t	StatusRegister3;
	
}w25qxx_t;

extern w25qxx_t	w25qxx;
//############################################################################
bool	W25qxx_Init(void);
void	W25qxx_EraseChip(void);
void 	W25qxx_EraseSector(uint32_t SectorAddr);
void 	W25qxx_EraseBlock(uint32_t BlockAddr);
void 	W25qxx_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void 	W25qxx_WriteBuffer(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void 	W25qxx_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
//############################################################################
#endif

