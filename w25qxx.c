
#include "w25qxxConf.h"
#include "w25qxx.h"
#include <main.h>

#if (_W25QXX_DEBUG == 1)
#include <stdio.h>
#endif

#define W25QXX_DUMMY_BYTE 0xA5

#if (_W25QXX_USE_FREERTOS == 1)
#define W25qxx_Delay(delay) osDelay(delay)
#include "cmsis_os.h"
#else
#define W25qxx_Delay(delay) HAL_Delay(delay)
#endif

//###################################################################################################################
uint8_t W25qxx_Spi(w25qxx_peripheral* per, uint8_t Data)
{
	uint8_t ret;
	HAL_SPI_TransmitReceive(per->hspi, &Data, &ret, 1, 100);
	return ret;
}
//###################################################################################################################
uint32_t W25qxx_ReadID(w25qxx_peripheral* per)
{
	uint32_t Temp = 0, Temp0 = 0, Temp1 = 0, Temp2 = 0;
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	W25qxx_Spi(per, 0x9F);
	Temp0 = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
	Temp1 = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
	Temp2 = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	Temp = (Temp0 << 16) | (Temp1 << 8) | Temp2;
	return Temp;
}
//###################################################################################################################
void W25qxx_ReadUniqID(w25qxx_peripheral* per)
{
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	W25qxx_Spi(per, 0x4B);
	for (uint8_t i = 0; i < 4; i++)
		W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
	for (uint8_t i = 0; i < 8; i++)
		per->desc.UniqID[i] = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
}
//###################################################################################################################
void W25qxx_WriteEnable(w25qxx_peripheral* per)
{
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	W25qxx_Spi(per, 0x06);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_Delay(1);
}
//###################################################################################################################
void W25qxx_WriteDisable(w25qxx_peripheral* per)
{
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	W25qxx_Spi(per, 0x04);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_Delay(1);
}
//###################################################################################################################
uint8_t W25qxx_ReadStatusRegister(w25qxx_peripheral* per, uint8_t SelectStatusRegister_1_2_3)
{
	uint8_t status = 0;
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	if (SelectStatusRegister_1_2_3 == 1)
	{
		W25qxx_Spi(per, 0x05);
		status = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
		per->desc.StatusRegister1 = status;
	}
	else if (SelectStatusRegister_1_2_3 == 2)
	{
		W25qxx_Spi(per, 0x35);
		status = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
		per->desc.StatusRegister2 = status;
	}
	else
	{
		W25qxx_Spi(per, 0x15);
		status = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
		per->desc.StatusRegister3 = status;
	}
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	return status;
}
//###################################################################################################################
void W25qxx_WriteStatusRegister(w25qxx_peripheral* per, uint8_t SelectStatusRegister_1_2_3, uint8_t Data)
{
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	if (SelectStatusRegister_1_2_3 == 1)
	{
		W25qxx_Spi(per, 0x01);
		per->desc.StatusRegister1 = Data;
	}
	else if (SelectStatusRegister_1_2_3 == 2)
	{
		W25qxx_Spi(per, 0x31);
		per->desc.StatusRegister2 = Data;
	}
	else
	{
		W25qxx_Spi(per, 0x11);
		per->desc.StatusRegister3 = Data;
	}
	W25qxx_Spi(per, Data);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
}
//###################################################################################################################
void W25qxx_WaitForWriteEnd(w25qxx_peripheral* per)
{
	W25qxx_Delay(1);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	W25qxx_Spi(per, 0x05);
	do
	{
		per->desc.StatusRegister1 = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
		W25qxx_Delay(1);
	} while ((per->desc.StatusRegister1 & 0x01) == 0x01);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
}
//###################################################################################################################
bool W25qxx_Init(w25qxx_peripheral* per)
{
	per->desc.Lock = 1;
	while (HAL_GetTick() < 100)
		W25qxx_Delay(1);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_Delay(100);
	uint32_t id;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx Init Begin...\r\n");
#endif
	id = W25qxx_ReadID(per);

#if (_W25QXX_DEBUG == 1)
	printf("w25qxx ID:0x%X\r\n", id);
#endif
	switch (id & 0x000000FF)
	{
	case 0x20: // 	w25q512
		per->desc.ID = W25Q512;
		per->desc.BlockCount = 1024;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q512\r\n");
#endif
		break;
	case 0x19: // 	w25q256
		per->desc.ID = W25Q256;
		per->desc.BlockCount = 512;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q256\r\n");
#endif
		break;
	case 0x18: // 	w25q128
		per->desc.ID = W25Q128;
		per->desc.BlockCount = 256;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q128\r\n");
#endif
		break;
	case 0x17: //	w25q64
		per->desc.ID = W25Q64;
		per->desc.BlockCount = 128;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q64\r\n");
#endif
		break;
	case 0x16: //	w25q32
		per->desc.ID = W25Q32;
		per->desc.BlockCount = 64;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q32\r\n");
#endif
		break;
	case 0x15: //	w25q16
		per->desc.ID = W25Q16;
		per->desc.BlockCount = 32;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q16\r\n");
#endif
		break;
	case 0x14: //	w25q80
		per->desc.ID = W25Q80;
		per->desc.BlockCount = 16;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q80\r\n");
#endif
		break;
	case 0x13: //	w25q40
		per->desc.ID = W25Q40;
		per->desc.BlockCount = 8;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q40\r\n");
#endif
		break;
	case 0x12: //	w25q20
		per->desc.ID = W25Q20;
		per->desc.BlockCount = 4;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q20\r\n");
#endif
		break;
	case 0x11: //	w25q10
		per->desc.ID = W25Q10;
		per->desc.BlockCount = 2;
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Chip: w25q10\r\n");
#endif
		break;
	default:
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx Unknown ID\r\n");
#endif
		per->desc.Lock = 0;
		return false;
	}
	per->desc.PageSize = 256;
	per->desc.SectorSize = 0x1000;
	per->desc.SectorCount = per->desc.BlockCount * 16;
	per->desc.PageCount = (per->desc.SectorCount * per->desc.SectorSize) / per->desc.PageSize;
	per->desc.BlockSize = per->desc.SectorSize * 16;
	per->desc.CapacityInKiloByte = (per->desc.SectorCount * per->desc.SectorSize) / 1024;
	W25qxx_ReadUniqID(per);
	W25qxx_ReadStatusRegister(per, 1);
	W25qxx_ReadStatusRegister(per, 2);
	W25qxx_ReadStatusRegister(per, 3);
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx Page Size: %d Bytes\r\n", per->desc.PageSize);
	printf("w25qxx Page Count: %d\r\n", per->desc.PageCount);
	printf("w25qxx Sector Size: %d Bytes\r\n", per->desc.SectorSize);
	printf("w25qxx Sector Count: %d\r\n", per->desc.SectorCount);
	printf("w25qxx Block Size: %d Bytes\r\n", per->desc.BlockSize);
	printf("w25qxx Block Count: %d\r\n", per->desc.BlockCount);
	printf("w25qxx Capacity: %d KiloBytes\r\n", per->desc.CapacityInKiloByte);
	printf("w25qxx Init Done\r\n");
#endif
	per->desc.Lock = 0;
	return true;
}
//###################################################################################################################
void W25qxx_EraseChip(w25qxx_peripheral* per)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
#if (_W25QXX_DEBUG == 1)
	uint32_t StartTime = HAL_GetTick();
	printf("w25qxx EraseChip Begin...\r\n");
#endif
	W25qxx_WriteEnable(per);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	W25qxx_Spi(per, 0xC7);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd(per);
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx EraseBlock done after %d ms!\r\n", HAL_GetTick() - StartTime);
#endif
	W25qxx_Delay(10);
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_EraseSector(w25qxx_peripheral* per, uint32_t SectorAddr)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
#if (_W25QXX_DEBUG == 1)
	uint32_t StartTime = HAL_GetTick();
	printf("w25qxx EraseSector %d Begin...\r\n", SectorAddr);
#endif
	W25qxx_WaitForWriteEnd(per);
	SectorAddr = SectorAddr * per->desc.SectorSize;
	W25qxx_WriteEnable(per);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0x21);
		W25qxx_Spi(per, (SectorAddr & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0x20);
	}
	W25qxx_Spi(per, (SectorAddr & 0xFF0000) >> 16);
	W25qxx_Spi(per, (SectorAddr & 0xFF00) >> 8);
	W25qxx_Spi(per, SectorAddr & 0xFF);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd(per);
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx EraseSector done after %d ms\r\n", HAL_GetTick() - StartTime);
#endif
	W25qxx_Delay(1);
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_EraseBlock(w25qxx_peripheral* per, uint32_t BlockAddr)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx EraseBlock %d Begin...\r\n", BlockAddr);
	W25qxx_Delay(100);
	uint32_t StartTime = HAL_GetTick();
#endif
	W25qxx_WaitForWriteEnd(per);
	BlockAddr = BlockAddr * per->desc.SectorSize * 16;
	W25qxx_WriteEnable(per);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0xDC);
		W25qxx_Spi(per, (BlockAddr & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0xD8);
	}
	W25qxx_Spi(per, (BlockAddr & 0xFF0000) >> 16);
	W25qxx_Spi(per, (BlockAddr & 0xFF00) >> 8);
	W25qxx_Spi(per, BlockAddr & 0xFF);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd(per);
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx EraseBlock done after %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	W25qxx_Delay(1);
	per->desc.Lock = 0;
}
//###################################################################################################################
uint32_t W25qxx_PageToSector(w25qxx_peripheral* per, uint32_t PageAddress)
{
	return ((PageAddress * per->desc.PageSize) / per->desc.SectorSize);
}
//###################################################################################################################
uint32_t W25qxx_PageToBlock(w25qxx_peripheral* per, uint32_t PageAddress)
{
	return ((PageAddress * per->desc.PageSize) / per->desc.BlockSize);
}
//###################################################################################################################
uint32_t W25qxx_SectorToBlock(w25qxx_peripheral* per, uint32_t SectorAddress)
{
	return ((SectorAddress * per->desc.SectorSize) / per->desc.BlockSize);
}
//###################################################################################################################
uint32_t W25qxx_SectorToPage(w25qxx_peripheral* per, uint32_t SectorAddress)
{
	return (SectorAddress * per->desc.SectorSize) / per->desc.PageSize;
}
//###################################################################################################################
uint32_t W25qxx_BlockToPage(w25qxx_peripheral* per, uint32_t BlockAddress)
{
	return (BlockAddress * per->desc.BlockSize) / per->desc.PageSize;
}
//###################################################################################################################
bool W25qxx_IsEmptyPage(w25qxx_peripheral* per, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_PageSize)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
	if (((NumByteToCheck_up_to_PageSize + OffsetInByte) > per->desc.PageSize) || (NumByteToCheck_up_to_PageSize == 0))
		NumByteToCheck_up_to_PageSize = per->desc.PageSize - OffsetInByte;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckPage:%d, Offset:%d, Bytes:%d begin...\r\n", Page_Address, OffsetInByte, NumByteToCheck_up_to_PageSize);
	W25qxx_Delay(100);
	uint32_t StartTime = HAL_GetTick();
#endif
	uint8_t pBuffer[32];
	uint32_t WorkAddress;
	uint32_t i;
	for (i = OffsetInByte; i < per->desc.PageSize; i += sizeof(pBuffer))
	{
		HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
		WorkAddress = (i + Page_Address * per->desc.PageSize);
		if (per->desc.ID >= W25Q256)
		{
			W25qxx_Spi(per, 0x0C);
			W25qxx_Spi(per, (WorkAddress & 0xFF000000) >> 24);
		}
		else
		{
			W25qxx_Spi(per, 0x0B);
		}
		W25qxx_Spi(per, (WorkAddress & 0xFF0000) >> 16);
		W25qxx_Spi(per, (WorkAddress & 0xFF00) >> 8);
		W25qxx_Spi(per, WorkAddress & 0xFF);
		W25qxx_Spi(per, 0);
		HAL_SPI_Receive(per->hspi, pBuffer, sizeof(pBuffer), 100);
		HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
		for (uint8_t x = 0; x < sizeof(pBuffer); x++)
		{
			if (pBuffer[x] != 0xFF)
				goto NOT_EMPTY;
		}
	}
	if ((per->desc.PageSize + OffsetInByte) % sizeof(pBuffer) != 0)
	{
		i -= sizeof(pBuffer);
		for (; i < per->desc.PageSize; i++)
		{
			HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
			WorkAddress = (i + Page_Address * per->desc.PageSize);
			W25qxx_Spi(per, 0x0B);
			if (per->desc.ID >= W25Q256)
			{
				W25qxx_Spi(per, 0x0C);
				W25qxx_Spi(per, (WorkAddress & 0xFF000000) >> 24);
			}
			else
			{
				W25qxx_Spi(per, 0x0B);
			}
			W25qxx_Spi(per,(WorkAddress & 0xFF0000) >> 16);
			W25qxx_Spi(per, (WorkAddress & 0xFF00) >> 8);
			W25qxx_Spi(per, WorkAddress & 0xFF);
			W25qxx_Spi(per, 0);
			HAL_SPI_Receive(per->hspi, pBuffer, 1, 100);
			HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
			if (pBuffer[0] != 0xFF)
				goto NOT_EMPTY;
		}
	}
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckPage is Empty in %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	per->desc.Lock = 0;
	return true;
NOT_EMPTY:
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckPage is Not Empty in %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	per->desc.Lock = 0;
	return false;
}
//###################################################################################################################
bool W25qxx_IsEmptySector(w25qxx_peripheral* per, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_SectorSize)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
	if ((NumByteToCheck_up_to_SectorSize > per->desc.SectorSize) || (NumByteToCheck_up_to_SectorSize == 0))
		NumByteToCheck_up_to_SectorSize = per->desc.SectorSize;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckSector:%d, Offset:%d, Bytes:%d begin...\r\n", Sector_Address, OffsetInByte, NumByteToCheck_up_to_SectorSize);
	W25qxx_Delay(100);
	uint32_t StartTime = HAL_GetTick();
#endif
	uint8_t pBuffer[32];
	uint32_t WorkAddress;
	uint32_t i;
	for (i = OffsetInByte; i < per->desc.SectorSize; i += sizeof(pBuffer))
	{
		HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
		WorkAddress = (i + Sector_Address * per->desc.SectorSize);
		if (per->desc.ID >= W25Q256)
		{
			W25qxx_Spi(per, 0x0C);
			W25qxx_Spi(per, (WorkAddress & 0xFF000000) >> 24);
		}
		else
		{
			W25qxx_Spi(per, 0x0B);
		}
		W25qxx_Spi(per, (WorkAddress & 0xFF0000) >> 16);
		W25qxx_Spi(per, (WorkAddress & 0xFF00) >> 8);
		W25qxx_Spi(per, WorkAddress & 0xFF);
		W25qxx_Spi(per, 0);
		HAL_SPI_Receive(per->hspi, pBuffer, sizeof(pBuffer), 100);
		HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
		for (uint8_t x = 0; x < sizeof(pBuffer); x++)
		{
			if (pBuffer[x] != 0xFF)
				goto NOT_EMPTY;
		}
	}
	if ((per->desc.SectorSize + OffsetInByte) % sizeof(pBuffer) != 0)
	{
		i -= sizeof(pBuffer);
		for (; i < per->desc.SectorSize; i++)
		{
			HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
			WorkAddress = (i + Sector_Address * per->desc.SectorSize);
			if (per->desc.ID >= W25Q256)
			{
				W25qxx_Spi(per, 0x0C);
				W25qxx_Spi(per, (WorkAddress & 0xFF000000) >> 24);
			}
			else
			{
				W25qxx_Spi(per, 0x0B);
			}
			W25qxx_Spi(per, (WorkAddress & 0xFF0000) >> 16);
			W25qxx_Spi(per, (WorkAddress & 0xFF00) >> 8);
			W25qxx_Spi(per, WorkAddress & 0xFF);
			W25qxx_Spi(per, 0);
			HAL_SPI_Receive(per->hspi, pBuffer, 1, 100);
			HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
			if (pBuffer[0] != 0xFF)
				goto NOT_EMPTY;
		}
	}
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckSector is Empty in %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	per->desc.Lock = 0;
	return true;
NOT_EMPTY:
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckSector is Not Empty in %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	per->desc.Lock = 0;
	return false;
}
//###################################################################################################################
bool W25qxx_IsEmptyBlock(w25qxx_peripheral* per, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_BlockSize)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
	if ((NumByteToCheck_up_to_BlockSize > per->desc.BlockSize) || (NumByteToCheck_up_to_BlockSize == 0))
		NumByteToCheck_up_to_BlockSize = per->desc.BlockSize;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckBlock:%d, Offset:%d, Bytes:%d begin...\r\n", Block_Address, OffsetInByte, NumByteToCheck_up_to_BlockSize);
	W25qxx_Delay(100);
	uint32_t StartTime = HAL_GetTick();
#endif
	uint8_t pBuffer[32];
	uint32_t WorkAddress;
	uint32_t i;
	for (i = OffsetInByte; i < per->desc.BlockSize; i += sizeof(pBuffer))
	{
		HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
		WorkAddress = (i + Block_Address * per->desc.BlockSize);

		if (per->desc.ID >= W25Q256)
		{
			W25qxx_Spi(per, 0x0C);
			W25qxx_Spi(per, (WorkAddress & 0xFF000000) >> 24);
		}
		else
		{
			W25qxx_Spi(per, 0x0B);
		}
		W25qxx_Spi(per, (WorkAddress & 0xFF0000) >> 16);
		W25qxx_Spi(per, (WorkAddress & 0xFF00) >> 8);
		W25qxx_Spi(per, WorkAddress & 0xFF);
		W25qxx_Spi(per, 0);
		HAL_SPI_Receive(per->hspi, pBuffer, sizeof(pBuffer), 100);
		HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
		for (uint8_t x = 0; x < sizeof(pBuffer); x++)
		{
			if (pBuffer[x] != 0xFF)
				goto NOT_EMPTY;
		}
	}
	if ((per->desc.BlockSize + OffsetInByte) % sizeof(pBuffer) != 0)
	{
		i -= sizeof(pBuffer);
		for (; i < per->desc.BlockSize; i++)
		{
			HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
			WorkAddress = (i + Block_Address * per->desc.BlockSize);

			if (per->desc.ID >= W25Q256)
			{
				W25qxx_Spi(per, 0x0C);
				W25qxx_Spi(per, (WorkAddress & 0xFF000000) >> 24);
			}
			else
			{
				W25qxx_Spi(per, 0x0B);
			}
			W25qxx_Spi(per, (WorkAddress & 0xFF0000) >> 16);
			W25qxx_Spi(per, (WorkAddress & 0xFF00) >> 8);
			W25qxx_Spi(per, WorkAddress & 0xFF);
			W25qxx_Spi(per, 0);
			HAL_SPI_Receive(per->hspi, pBuffer, 1, 100);
			HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
			if (pBuffer[0] != 0xFF)
				goto NOT_EMPTY;
		}
	}
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckBlock is Empty in %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	per->desc.Lock = 0;
	return true;
NOT_EMPTY:
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx CheckBlock is Not Empty in %d ms\r\n", HAL_GetTick() - StartTime);
	W25qxx_Delay(100);
#endif
	per->desc.Lock = 0;
	return false;
}
//###################################################################################################################
void W25qxx_WriteByte(w25qxx_peripheral* per, uint8_t pBuffer, uint32_t WriteAddr_inBytes)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
#if (_W25QXX_DEBUG == 1)
	uint32_t StartTime = HAL_GetTick();
	printf("w25qxx WriteByte 0x%02X at address %d begin...", pBuffer, WriteAddr_inBytes);
#endif
	W25qxx_WaitForWriteEnd(per);
	W25qxx_WriteEnable(per);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);

	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0x12);
		W25qxx_Spi(per, (WriteAddr_inBytes & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0x02);
	}
	W25qxx_Spi(per, (WriteAddr_inBytes & 0xFF0000) >> 16);
	W25qxx_Spi(per, (WriteAddr_inBytes & 0xFF00) >> 8);
	W25qxx_Spi(per, WriteAddr_inBytes & 0xFF);
	W25qxx_Spi(per, pBuffer);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd(per);
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx WriteByte done after %d ms\r\n", HAL_GetTick() - StartTime);
#endif
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_WritePage(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_PageSize)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
	if (((NumByteToWrite_up_to_PageSize + OffsetInByte) > per->desc.PageSize) || (NumByteToWrite_up_to_PageSize == 0))
		NumByteToWrite_up_to_PageSize = per->desc.PageSize - OffsetInByte;
	if ((OffsetInByte + NumByteToWrite_up_to_PageSize) > per->desc.PageSize)
		NumByteToWrite_up_to_PageSize = per->desc.PageSize - OffsetInByte;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx WritePage:%d, Offset:%d ,Writes %d Bytes, begin...\r\n", Page_Address, OffsetInByte, NumByteToWrite_up_to_PageSize);
	W25qxx_Delay(100);
	uint32_t StartTime = HAL_GetTick();
#endif
	W25qxx_WaitForWriteEnd(per);
	W25qxx_WriteEnable(per);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	Page_Address = (Page_Address * per->desc.PageSize) + OffsetInByte;
	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0x12);
		W25qxx_Spi(per, (Page_Address & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0x02);
	}
	W25qxx_Spi(per, (Page_Address & 0xFF0000) >> 16);
	W25qxx_Spi(per, (Page_Address & 0xFF00) >> 8);
	W25qxx_Spi(per, Page_Address & 0xFF);
	HAL_SPI_Transmit(per->hspi, pBuffer, NumByteToWrite_up_to_PageSize, 100);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd(per);
#if (_W25QXX_DEBUG == 1)
	StartTime = HAL_GetTick() - StartTime;
	for (uint32_t i = 0; i < NumByteToWrite_up_to_PageSize; i++)
	{
		if ((i % 8 == 0) && (i > 2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,", pBuffer[i]);
	}
	printf("\r\n");
	printf("w25qxx WritePage done after %d ms\r\n", StartTime);
	W25qxx_Delay(100);
#endif
	W25qxx_Delay(1);
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_WriteSector(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_SectorSize)
{
	if ((NumByteToWrite_up_to_SectorSize > per->desc.SectorSize) || (NumByteToWrite_up_to_SectorSize == 0))
		NumByteToWrite_up_to_SectorSize = per->desc.SectorSize;
#if (_W25QXX_DEBUG == 1)
	printf("+++w25qxx WriteSector:%d, Offset:%d ,Write %d Bytes, begin...\r\n", Sector_Address, OffsetInByte, NumByteToWrite_up_to_SectorSize);
	W25qxx_Delay(100);
#endif
	if (OffsetInByte >= per->desc.SectorSize)
	{
#if (_W25QXX_DEBUG == 1)
		printf("---w25qxx WriteSector Faild!\r\n");
		W25qxx_Delay(100);
#endif
		return;
	}
	uint32_t StartPage;
	int32_t BytesToWrite;
	uint32_t LocalOffset;
	if ((OffsetInByte + NumByteToWrite_up_to_SectorSize) > per->desc.SectorSize)
		BytesToWrite = per->desc.SectorSize - OffsetInByte;
	else
		BytesToWrite = NumByteToWrite_up_to_SectorSize;
	StartPage = W25qxx_SectorToPage(per, Sector_Address) + (OffsetInByte / per->desc.PageSize);
	LocalOffset = OffsetInByte % per->desc.PageSize;
	do
	{
		W25qxx_WritePage(per, pBuffer, StartPage, LocalOffset, BytesToWrite);
		StartPage++;
		BytesToWrite -= per->desc.PageSize - LocalOffset;
		pBuffer += per->desc.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToWrite > 0);
#if (_W25QXX_DEBUG == 1)
	printf("---w25qxx WriteSector Done\r\n");
	W25qxx_Delay(100);
#endif
}
//###################################################################################################################
void W25qxx_WriteBlock(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_BlockSize)
{
	if ((NumByteToWrite_up_to_BlockSize > per->desc.BlockSize) || (NumByteToWrite_up_to_BlockSize == 0))
		NumByteToWrite_up_to_BlockSize = per->desc.BlockSize;
#if (_W25QXX_DEBUG == 1)
	printf("+++w25qxx WriteBlock:%d, Offset:%d ,Write %d Bytes, begin...\r\n", Block_Address, OffsetInByte, NumByteToWrite_up_to_BlockSize);
	W25qxx_Delay(100);
#endif
	if (OffsetInByte >= per->desc.BlockSize)
	{
#if (_W25QXX_DEBUG == 1)
		printf("---w25qxx WriteBlock Faild!\r\n");
		W25qxx_Delay(100);
#endif
		return;
	}
	uint32_t StartPage;
	int32_t BytesToWrite;
	uint32_t LocalOffset;
	if ((OffsetInByte + NumByteToWrite_up_to_BlockSize) > per->desc.BlockSize)
		BytesToWrite = per->desc.BlockSize - OffsetInByte;
	else
		BytesToWrite = NumByteToWrite_up_to_BlockSize;
	StartPage = W25qxx_BlockToPage(per, Block_Address) + (OffsetInByte / per->desc.PageSize);
	LocalOffset = OffsetInByte % per->desc.PageSize;
	do
	{
		W25qxx_WritePage(per, pBuffer, StartPage, LocalOffset, BytesToWrite);
		StartPage++;
		BytesToWrite -= per->desc.PageSize - LocalOffset;
		pBuffer += per->desc.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToWrite > 0);
#if (_W25QXX_DEBUG == 1)
	printf("---w25qxx WriteBlock Done\r\n");
	W25qxx_Delay(100);
#endif
}
//###################################################################################################################
void W25qxx_ReadByte(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Bytes_Address)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
#if (_W25QXX_DEBUG == 1)
	uint32_t StartTime = HAL_GetTick();
	printf("w25qxx ReadByte at address %d begin...\r\n", Bytes_Address);
#endif
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);

	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0x0C);
		W25qxx_Spi(per, (Bytes_Address & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0x0B);
	}
	W25qxx_Spi(per, (Bytes_Address & 0xFF0000) >> 16);
	W25qxx_Spi(per, (Bytes_Address & 0xFF00) >> 8);
	W25qxx_Spi(per, Bytes_Address & 0xFF);
	W25qxx_Spi(per, 0);
	*pBuffer = W25qxx_Spi(per, W25QXX_DUMMY_BYTE);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx ReadByte 0x%02X done after %d ms\r\n", *pBuffer, HAL_GetTick() - StartTime);
#endif
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_ReadBytes(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
#if (_W25QXX_DEBUG == 1)
	uint32_t StartTime = HAL_GetTick();
	printf("w25qxx ReadBytes at Address:%d, %d Bytes  begin...\r\n", ReadAddr, NumByteToRead);
#endif
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);

	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0x0C);
		W25qxx_Spi(per, (ReadAddr & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0x0B);
	}
	W25qxx_Spi(per, (ReadAddr & 0xFF0000) >> 16);
	W25qxx_Spi(per, (ReadAddr & 0xFF00) >> 8);
	W25qxx_Spi(per, ReadAddr & 0xFF);
	W25qxx_Spi(per, 0);
	HAL_SPI_Receive(per->hspi, pBuffer, NumByteToRead, 2000);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
#if (_W25QXX_DEBUG == 1)
	StartTime = HAL_GetTick() - StartTime;
	for (uint32_t i = 0; i < NumByteToRead; i++)
	{
		if ((i % 8 == 0) && (i > 2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,", pBuffer[i]);
	}
	printf("\r\n");
	printf("w25qxx ReadBytes done after %d ms\r\n", StartTime);
	W25qxx_Delay(100);
#endif
	W25qxx_Delay(1);
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_ReadPage(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_PageSize)
{
	while (per->desc.Lock == 1)
		W25qxx_Delay(1);
	per->desc.Lock = 1;
	if ((NumByteToRead_up_to_PageSize > per->desc.PageSize) || (NumByteToRead_up_to_PageSize == 0))
		NumByteToRead_up_to_PageSize = per->desc.PageSize;
	if ((OffsetInByte + NumByteToRead_up_to_PageSize) > per->desc.PageSize)
		NumByteToRead_up_to_PageSize = per->desc.PageSize - OffsetInByte;
#if (_W25QXX_DEBUG == 1)
	printf("w25qxx ReadPage:%d, Offset:%d ,Read %d Bytes, begin...\r\n", Page_Address, OffsetInByte, NumByteToRead_up_to_PageSize);
	W25qxx_Delay(100);
	uint32_t StartTime = HAL_GetTick();
#endif
	Page_Address = Page_Address * per->desc.PageSize + OffsetInByte;
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_RESET);
	if (per->desc.ID >= W25Q256)
	{
		W25qxx_Spi(per, 0x0C);
		W25qxx_Spi(per, (Page_Address & 0xFF000000) >> 24);
	}
	else
	{
		W25qxx_Spi(per, 0x0B);
	}
	W25qxx_Spi(per, (Page_Address & 0xFF0000) >> 16);
	W25qxx_Spi(per, (Page_Address & 0xFF00) >> 8);
	W25qxx_Spi(per, Page_Address & 0xFF);
	W25qxx_Spi(per, 0);
	HAL_SPI_Receive(per->hspi, pBuffer, NumByteToRead_up_to_PageSize, 100);
	HAL_GPIO_WritePin(per->cs_gpio, per->cs_pin, GPIO_PIN_SET);
#if (_W25QXX_DEBUG == 1)
	StartTime = HAL_GetTick() - StartTime;
	for (uint32_t i = 0; i < NumByteToRead_up_to_PageSize; i++)
	{
		if ((i % 8 == 0) && (i > 2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,", pBuffer[i]);
	}
	printf("\r\n");
	printf("w25qxx ReadPage done after %d ms\r\n", StartTime);
	W25qxx_Delay(100);
#endif
	W25qxx_Delay(1);
	per->desc.Lock = 0;
}
//###################################################################################################################
void W25qxx_ReadSector(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_SectorSize)
{
	if ((NumByteToRead_up_to_SectorSize > per->desc.SectorSize) || (NumByteToRead_up_to_SectorSize == 0))
		NumByteToRead_up_to_SectorSize = per->desc.SectorSize;
#if (_W25QXX_DEBUG == 1)
	printf("+++w25qxx ReadSector:%d, Offset:%d ,Read %d Bytes, begin...\r\n", Sector_Address, OffsetInByte, NumByteToRead_up_to_SectorSize);
	W25qxx_Delay(100);
#endif
	if (OffsetInByte >= per->desc.SectorSize)
	{
#if (_W25QXX_DEBUG == 1)
		printf("---w25qxx ReadSector Faild!\r\n");
		W25qxx_Delay(100);
#endif
		return;
	}
	uint32_t StartPage;
	int32_t BytesToRead;
	uint32_t LocalOffset;
	if ((OffsetInByte + NumByteToRead_up_to_SectorSize) > per->desc.SectorSize)
		BytesToRead = per->desc.SectorSize - OffsetInByte;
	else
		BytesToRead = NumByteToRead_up_to_SectorSize;
	StartPage = W25qxx_SectorToPage(per, Sector_Address) + (OffsetInByte / per->desc.PageSize);
	LocalOffset = OffsetInByte % per->desc.PageSize;
	do
	{
		W25qxx_ReadPage(per, pBuffer, StartPage, LocalOffset, BytesToRead);
		StartPage++;
		BytesToRead -= per->desc.PageSize - LocalOffset;
		pBuffer += per->desc.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToRead > 0);
#if (_W25QXX_DEBUG == 1)
	printf("---w25qxx ReadSector Done\r\n");
	W25qxx_Delay(100);
#endif
}
//###################################################################################################################
void W25qxx_ReadBlock(w25qxx_peripheral* per, uint8_t *pBuffer, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_BlockSize)
{
	if ((NumByteToRead_up_to_BlockSize > per->desc.BlockSize) || (NumByteToRead_up_to_BlockSize == 0))
		NumByteToRead_up_to_BlockSize = per->desc.BlockSize;
#if (_W25QXX_DEBUG == 1)
	printf("+++w25qxx ReadBlock:%d, Offset:%d ,Read %d Bytes, begin...\r\n", Block_Address, OffsetInByte, NumByteToRead_up_to_BlockSize);
	W25qxx_Delay(100);
#endif
	if (OffsetInByte >= per->desc.BlockSize)
	{
#if (_W25QXX_DEBUG == 1)
		printf("w25qxx ReadBlock Faild!\r\n");
		W25qxx_Delay(100);
#endif
		return;
	}
	uint32_t StartPage;
	int32_t BytesToRead;
	uint32_t LocalOffset;
	if ((OffsetInByte + NumByteToRead_up_to_BlockSize) > per->desc.BlockSize)
		BytesToRead = per->desc.BlockSize - OffsetInByte;
	else
		BytesToRead = NumByteToRead_up_to_BlockSize;
	StartPage = W25qxx_BlockToPage(per, Block_Address) + (OffsetInByte / per->desc.PageSize);
	LocalOffset = OffsetInByte % per->desc.PageSize;
	do
	{
		W25qxx_ReadPage(per, pBuffer, StartPage, LocalOffset, BytesToRead);
		StartPage++;
		BytesToRead -= per->desc.PageSize - LocalOffset;
		pBuffer += per->desc.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToRead > 0);
#if (_W25QXX_DEBUG == 1)
	printf("---w25qxx ReadBlock Done\r\n");
	W25qxx_Delay(100);
#endif
}
//###################################################################################################################
