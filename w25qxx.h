#ifndef _W25QXX_H
#define _W25QXX_H

/*
  Author:     Nima Askari
  WebSite:    http://www.github.com/NimaLTD
  Instagram:  http://instagram.com/github.NimaLTD
  Youtube:    https://www.youtube.com/channel/UCUhY7qY1klJm1d2kulr9ckw
  
  Version:    1.1.4
  
  
  Reversion History:
  
  (1.1.4)
  Fix W25qxx_IsEmptySector function.
  
  (1.1.3)
  Fix Erase and write sector in w25q256 and w25q512.

  (1.1.2)
  Fix read ID.
  
  (1.1.1)
  Fix some errors.
  
  (1.1.0)
  Fix some errors.
  
  (1.0.0)
  First release.
*/

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include "main.h"

	typedef enum
	{
		W25Q10 = 1,
		W25Q20,
		W25Q40,
		W25Q80,
		W25Q16,
		W25Q32,
		W25Q64,
		W25Q128,
		W25Q256,
		W25Q512,

	} W25QXX_ID_t;

	typedef struct
	{
		W25QXX_ID_t ID;
		uint8_t UniqID[8];
		uint16_t PageSize;
		uint32_t PageCount;
		uint32_t SectorSize;
		uint32_t SectorCount;
		uint32_t BlockSize;
		uint32_t BlockCount;
		uint32_t CapacityInKiloByte;
		uint8_t StatusRegister1;
		uint8_t StatusRegister2;
		uint8_t StatusRegister3;
		uint8_t Lock;

	} w25qxx_t;

	extern w25qxx_t w25qxx;
	//############################################################################
	// in Page,Sector and block read/write functions, can put 0 to read maximum bytes
	//############################################################################
	bool W25qxx_Init(void);

	void W25qxx_EraseChip(void);
	void W25qxx_EraseSector(uint32_t SectorAddr);
	void W25qxx_EraseBlock(uint32_t BlockAddr);

	uint32_t W25qxx_PageToSector(uint32_t PageAddress);
	uint32_t W25qxx_PageToBlock(uint32_t PageAddress);
	uint32_t W25qxx_SectorToBlock(uint32_t SectorAddress);
	uint32_t W25qxx_SectorToPage(uint32_t SectorAddress);
	uint32_t W25qxx_BlockToPage(uint32_t BlockAddress);

	bool W25qxx_IsEmptyPage(uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_PageSize);
	bool W25qxx_IsEmptySector(uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_SectorSize);
	bool W25qxx_IsEmptyBlock(uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_BlockSize);

	void W25qxx_WriteByte(uint8_t pBuffer, uint32_t Bytes_Address);
	void W25qxx_WritePage(uint8_t *pBuffer, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_PageSize);
	void W25qxx_WriteSector(uint8_t *pBuffer, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_SectorSize);
	void W25qxx_WriteBlock(uint8_t *pBuffer, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_BlockSize);

	void W25qxx_ReadByte(uint8_t *pBuffer, uint32_t Bytes_Address);
	void W25qxx_ReadBytes(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead);
	void W25qxx_ReadPage(uint8_t *pBuffer, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_PageSize);
	void W25qxx_ReadSector(uint8_t *pBuffer, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_SectorSize);
	void W25qxx_ReadBlock(uint8_t *pBuffer, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_BlockSize);
//############################################################################
#ifdef __cplusplus
}
#endif

#endif
