#include <string.h>

#include "w25qxxConf.h"
#include "w25qxx.h"

#if (_W25QXX_DEBUG == 1)
#include <stdio.h>
#endif

#if (_W25QXX_USE_FREERTOS == 1)
#define W25qxx_Delay(delay) osDelay(delay)
#include "cmsis_os.h"
#else
#define W25qxx_Delay(delay) HAL_Delay(delay)
#endif

#if (_W25QXX_DEBUG == 1)
#define W25qxx_Debug(STATEMENT) STATEMENT
#else
#define W25qxx_Debug(STATEMENT)
#endif

//###################################################################################################################
#define W25QXX_DUMMY_BYTE 			0xA5
#define W25QXX_WRITE_ENABLE_BYTE 	0x06
#define W25QXX_WRITE_DISABLE_BYTE 	0x04
#define W25QXX_READ_ID_BYTE 		0x9F
#define W25QXX_READ_UNIQUE_ID_BYTE 	0x4B
#define W25QXX_READ_SR1_BYTE 		0x05
#define W25QXX_READ_SR2_BYTE		0x35
#define W25QXX_READ_SR3_BYTE 		0x15
#define W25QXX_WRITE_SR1_BYTE 		0x01
#define W25QXX_WRITE_SR2_BYTE		0x31
#define W25QXX_WRITE_SR3_BYTE		0x11
#define W25QXX_ERASE_CHIP_BYTE		0xC7
#define W25QXX_ERASE_SECTOR_BYTE	0x20
#define W25QXX_ERASE_BLOCK_BYTE		0xD8
#define W25QXX_WRITE_BYTE			0x02
#define W25QXX_READ_BYTE			0x03
#define W25QXX_FAST_READ_BYTE		0x0B
#define W25QXX_FAST_READ_END_BYTE	0x00

#define W25qxx_BUSY_BIT				0x1
#define W25qxx_IsBusy(SR1) 			((SR1) & W25qxx_BUSY_BIT)

//###################################################################################################################
static void W25qxx_WriteEnable(void);
static void W25qxx_WriteDisable(void);
static uint32_t W25qxx_ReadID(void);
static void W25qxx_ReadUniqID(void);
static uint8_t W25qxx_ReadStatusRegister(uint8_t sr_number);
static void W25qxx_WaitForWriteEnd(void);
static void W25qxx_SendAddress(uint32_t addr);
static uint8_t W25qxx_IsBufferEmpty(uint8_t *pBuffer, size_t len);
static void W25qxx_Debug_PrintData(uint8_t *pBuffer, uint32_t len);

static void W25qxx_CSLow(void);
static void W25qxx_CSHigh(void);
static uint8_t W25qxx_Tranceive(uint8_t Data);
static void W25qxx_Receive(uint8_t *pBuffer, uint32_t b2r);
static void W25qxx_Transmit(uint8_t *pBuffer, uint32_t b2t);
static void W25qxx_TakeMutex(void);
static void W25qxx_ReleaseMutex(void);

w25qxx_t w25qxx;

//###################################################################################################################
bool W25qxx_Init(void)
{
	uint32_t id;

	W25qxx_TakeMutex();

	while (HAL_GetTick() < 100)
		W25qxx_Delay(1);

	W25qxx_CSHigh();
	W25qxx_Delay(100);

	W25qxx_Debug(printf("w25qxx Init Begin...\r\n"));

	id = W25qxx_ReadID();

	W25qxx_Debug(printf("w25qxx ID:0x%lX\r\n", id));

	switch (id & 0x0000FFFF)
	{
		case 0x401A: // 	w25q512
			w25qxx.ID = W25Q512;
			w25qxx.BlockCount = 1024;
			W25qxx_Debug(printf("w25qxx Chip: w25q512\r\n"));
			break;
		case 0x4019: // 	w25q256
			w25qxx.ID = W25Q256;
			w25qxx.BlockCount = 512;
			W25qxx_Debug(printf("w25qxx Chip: w25q256\r\n"));
			break;
		case 0x4018: // 	w25q128
			w25qxx.ID = W25Q128;
			w25qxx.BlockCount = 256;
			W25qxx_Debug(printf("w25qxx Chip: w25q128\r\n"));
			break;
		case 0x4017: //	w25q64
			w25qxx.ID = W25Q64;
			w25qxx.BlockCount = 128;
			W25qxx_Debug(printf("w25qxx Chip: w25q64\r\n"));
			break;
		case 0x4016: //	w25q32
			w25qxx.ID = W25Q32;
			w25qxx.BlockCount = 64;
			W25qxx_Debug(printf("w25qxx Chip: w25q32\r\n"));
			break;
		case 0x4015: //	w25q16
			w25qxx.ID = W25Q16;
			w25qxx.BlockCount = 32;
			W25qxx_Debug(printf("w25qxx Chip: w25q16\r\n"));
			break;
		case 0x4014: //	w25q80
			w25qxx.ID = W25Q80;
			w25qxx.BlockCount = 16;
			W25qxx_Debug(printf("w25qxx Chip: w25q80\r\n"));
			break;
		case 0x4013: //	w25q40
			w25qxx.ID = W25Q40;
			w25qxx.BlockCount = 8;
			W25qxx_Debug(printf("w25qxx Chip: w25q40\r\n"));
			break;
		case 0x4012: //	w25q20
			w25qxx.ID = W25Q20;
			w25qxx.BlockCount = 4;
			W25qxx_Debug(printf("w25qxx Chip: w25q20\r\n"));
			break;
		case 0x4011: //	w25q10
			w25qxx.ID = W25Q10;
			w25qxx.BlockCount = 2;
			W25qxx_Debug(printf("w25qxx Chip: w25q10\r\n"));
			break;
		default:
			W25qxx_Debug(printf("w25qxx Unknown ID\r\n"));
			W25qxx_ReleaseMutex();
			return false;
	}

	w25qxx.PageSize = 256;
	w25qxx.SectorSize = 0x1000;
	w25qxx.SectorCount = w25qxx.BlockCount * 16;
	w25qxx.PageCount = (w25qxx.SectorCount * w25qxx.SectorSize) / w25qxx.PageSize;
	w25qxx.BlockSize = w25qxx.SectorSize * 16;
	w25qxx.CapacityInKiloByte = (w25qxx.SectorCount * w25qxx.SectorSize) / 1024;
	W25qxx_ReadUniqID();
	W25qxx_ReadStatusRegister(1);
	W25qxx_ReadStatusRegister(2);
	W25qxx_ReadStatusRegister(3);

	W25qxx_Debug(printf("w25qxx Page Size: %lu Bytes\r\n", w25qxx.PageSize));
	W25qxx_Debug(printf("w25qxx Page Count: %lu\r\n", w25qxx.PageCount));
	W25qxx_Debug(printf("w25qxx Sector Size: %lu Bytes\r\n", w25qxx.SectorSize));
	W25qxx_Debug(printf("w25qxx Sector Count: %lu\r\n", w25qxx.SectorCount));
	W25qxx_Debug(printf("w25qxx Block Size: %lu Bytes\r\n", w25qxx.BlockSize));
	W25qxx_Debug(printf("w25qxx Block Count: %lu\r\n", w25qxx.BlockCount));
	W25qxx_Debug(printf("w25qxx Capacity: %lu KiloBytes\r\n", w25qxx.CapacityInKiloByte));
	W25qxx_Debug(printf("w25qxx Init Done\r\n"));

	W25qxx_ReleaseMutex();

	return true;
}
//###################################################################################################################
void W25qxx_DeInit(void)
{
	W25qxx_TakeMutex();
	W25qxx_WriteDisable();
	memset(&w25qxx, 0x0, sizeof(w25qxx));
	W25qxx_ReleaseMutex();
}

//###################################################################################################################
void W25qxx_EraseChip(void)
{
	W25qxx_TakeMutex();

	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());
	W25qxx_Debug(printf("w25qxx EraseChip Begin...\r\n"));

	W25qxx_WriteEnable();

	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_ERASE_CHIP_BYTE);
	W25qxx_CSHigh();

	W25qxx_WaitForWriteEnd();

	W25qxx_Debug(printf("w25qxx EraseBlock done after %lu ms!\r\n", HAL_GetTick() - StartTime));

	W25qxx_Delay(10);
	W25qxx_ReleaseMutex();
}

void W25qxx_EraseSector(uint32_t SectorAddr)
{
	W25qxx_TakeMutex();

	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());
	W25qxx_Debug(printf("w25qxx EraseSector %lu Begin...\r\n", SectorAddr));

	SectorAddr = SectorAddr * w25qxx.SectorSize;

	W25qxx_WaitForWriteEnd();
	W25qxx_WriteEnable();
	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_ERASE_SECTOR_BYTE);
	W25qxx_SendAddress(SectorAddr);
	W25qxx_CSHigh();
	W25qxx_WaitForWriteEnd();

	W25qxx_Debug(printf("w25qxx EraseSector done after %lu ms\r\n", HAL_GetTick() - StartTime));

	W25qxx_Delay(1);
	W25qxx_ReleaseMutex();
}

void W25qxx_EraseBlock(uint32_t BlockAddr)
{
	W25qxx_TakeMutex();
	W25qxx_Debug(printf("w25qxx EraseBlock %lu Begin...\r\n", BlockAddr));
	W25qxx_Debug(W25qxx_Delay(100));
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());

	W25qxx_WaitForWriteEnd();

	BlockAddr = BlockAddr * w25qxx.SectorSize * 16;

	W25qxx_WriteEnable();
	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_ERASE_BLOCK_BYTE);

	W25qxx_SendAddress(BlockAddr);

	W25qxx_CSHigh();
	W25qxx_WaitForWriteEnd();

	W25qxx_Debug(printf("w25qxx EraseBlock done after %lu ms\r\n", HAL_GetTick() - StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_Delay(1);
	W25qxx_ReleaseMutex();
}
//###################################################################################################################
uint32_t W25qxx_PageToSector(uint32_t PageAddress)
{
	return ((PageAddress * w25qxx.PageSize) / w25qxx.SectorSize);
}
//###################################################################################################################
uint32_t W25qxx_PageToBlock(uint32_t PageAddress)
{
	return ((PageAddress * w25qxx.PageSize) / w25qxx.BlockSize);
}
//###################################################################################################################
uint32_t W25qxx_SectorToBlock(uint32_t SectorAddress)
{
	return ((SectorAddress * w25qxx.SectorSize) / w25qxx.BlockSize);
}
//###################################################################################################################
uint32_t W25qxx_SectorToPage(uint32_t SectorAddress)
{
	return (SectorAddress * w25qxx.SectorSize) / w25qxx.PageSize;
}
//###################################################################################################################
uint32_t W25qxx_BlockToPage(uint32_t BlockAddress)
{
	return (BlockAddress * w25qxx.BlockSize) / w25qxx.PageSize;
}
//###################################################################################################################
bool W25qxx_IsEmptyPage(uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_PageSize)
{

	uint8_t pBuffer[32];
	uint32_t WorkAddress;
	uint32_t i;

	uint8_t isempty = 1;

	W25qxx_TakeMutex();

	if (((NumByteToCheck_up_to_PageSize + OffsetInByte) > w25qxx.PageSize) || (NumByteToCheck_up_to_PageSize == 0))
	{
		NumByteToCheck_up_to_PageSize = w25qxx.PageSize - OffsetInByte;
	}

	W25qxx_Debug(printf("w25qxx CheckPage:%lu, Offset:%lu, Bytes:%lu begin...\r\n", Page_Address, OffsetInByte, NumByteToCheck_up_to_PageSize));
	W25qxx_Debug(W25qxx_Delay(100));
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());

	for (i = OffsetInByte; i < w25qxx.PageSize; i += sizeof(pBuffer))
	{
		WorkAddress = (i + Page_Address * w25qxx.PageSize);

		W25qxx_CSLow();
		W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
		W25qxx_SendAddress(WorkAddress);
		W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
		W25qxx_Receive(pBuffer, sizeof(pBuffer));
		W25qxx_CSHigh();

		if (0 == W25qxx_IsBufferEmpty(pBuffer, sizeof(pBuffer)))
		{
			isempty = 0;
			break;
		}
	}

	if (!isempty)
	{
		W25qxx_Debug(printf("w25qxx CheckPage is Not Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
		W25qxx_Debug(W25qxx_Delay(100));

		W25qxx_ReleaseMutex();
		return false;
	}

	if ((w25qxx.PageSize + OffsetInByte) % sizeof(pBuffer) != 0)
	{
		i -= sizeof(pBuffer);
		for (; i < w25qxx.PageSize; i++)
		{
			WorkAddress = (i + Page_Address * w25qxx.PageSize);

			W25qxx_CSLow();
			W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
			W25qxx_SendAddress(WorkAddress);
			W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
			W25qxx_Receive(pBuffer, 1);
			W25qxx_CSHigh();

			if (pBuffer[0] != 0xFF)
			{
				isempty = 0;
				break;
			}
		}
	}

	if (!isempty)
	{
		W25qxx_Debug(printf("w25qxx CheckPage is Not Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
		W25qxx_Debug(W25qxx_Delay(100));

		W25qxx_ReleaseMutex();
		return false;
	}

	W25qxx_Debug(printf("w25qxx CheckPage is Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_ReleaseMutex();
	return true;
}
//###################################################################################################################
bool W25qxx_IsEmptySector(uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_SectorSize)
{
	uint8_t pBuffer[32];
	uint32_t WorkAddress;
	uint32_t i;
	uint8_t isempty = 1;

	W25qxx_TakeMutex();

	if ((NumByteToCheck_up_to_SectorSize > w25qxx.SectorSize) || (NumByteToCheck_up_to_SectorSize == 0))
		NumByteToCheck_up_to_SectorSize = w25qxx.SectorSize;

	W25qxx_Debug(printf("w25qxx CheckSector:%lu, Offset:%lu, Bytes:%lu begin...\r\n", Sector_Address, OffsetInByte, NumByteToCheck_up_to_SectorSize));
	W25qxx_Debug(W25qxx_Delay(100));
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());

	for (i = OffsetInByte; i < w25qxx.SectorSize; i += sizeof(pBuffer))
	{
		WorkAddress = (i + Sector_Address * w25qxx.SectorSize);

		W25qxx_CSLow();
		W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
		W25qxx_SendAddress(WorkAddress);
		W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
		W25qxx_Receive(pBuffer, sizeof(pBuffer));
		W25qxx_CSHigh();

		if (0 == W25qxx_IsBufferEmpty(pBuffer, sizeof(pBuffer)))
		{
			isempty = 0;
			break;
		}
	}

	if (!isempty)
	{
		W25qxx_Debug(printf("w25qxx CheckSector is Not Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
		W25qxx_Debug(W25qxx_Delay(100));

		W25qxx_ReleaseMutex();
		return false;
	}

	if ((w25qxx.SectorSize + OffsetInByte) % sizeof(pBuffer) != 0)
	{
		i -= sizeof(pBuffer);
		for (; i < w25qxx.SectorSize; i++)
		{
			WorkAddress = (i + Sector_Address * w25qxx.SectorSize);

			W25qxx_CSLow();
			W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
			W25qxx_SendAddress(WorkAddress);
			W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
			W25qxx_Receive(pBuffer, 1);
			W25qxx_CSHigh();

			if (pBuffer[0] != 0xFF)
			{
				isempty = 0;
				break;
			}
		}
	}

	if (!isempty)
	{
		W25qxx_Debug(printf("w25qxx CheckSector is Not Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
		W25qxx_Debug(W25qxx_Delay(100));

		W25qxx_ReleaseMutex();
		return false;
	}

	W25qxx_Debug(printf("w25qxx CheckSector is Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_ReleaseMutex();
	return true;
}
//###################################################################################################################
bool W25qxx_IsEmptyBlock(uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToCheck_up_to_BlockSize)
{
	uint8_t pBuffer[32];
	uint32_t WorkAddress;
	uint32_t i;
	uint8_t isempty = 1;

	W25qxx_TakeMutex();

	if ((NumByteToCheck_up_to_BlockSize > w25qxx.BlockSize) || (NumByteToCheck_up_to_BlockSize == 0))
		NumByteToCheck_up_to_BlockSize = w25qxx.BlockSize;

	W25qxx_Debug(printf("w25qxx CheckBlock:%lu, Offset:%lu, Bytes:%lu begin...\r\n", Block_Address, OffsetInByte, NumByteToCheck_up_to_BlockSize));
	W25qxx_Debug(W25qxx_Delay(100));
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());

	for (i = OffsetInByte; i < w25qxx.BlockSize; i += sizeof(pBuffer))
	{
		WorkAddress = (i + Block_Address * w25qxx.BlockSize);

		W25qxx_CSLow();
		W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
		W25qxx_SendAddress(WorkAddress);
		W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
		W25qxx_Receive(pBuffer, sizeof(pBuffer));
		W25qxx_CSHigh();

		if (0 == W25qxx_IsBufferEmpty(pBuffer, sizeof(pBuffer)))
		{
			isempty = 0;
			break;
		}
	}

	if (!isempty)
	{
		W25qxx_Debug(printf("w25qxx CheckBlock is Not Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
		W25qxx_Debug(W25qxx_Delay(100));

		W25qxx_ReleaseMutex();
		return false;
	}

	if ((w25qxx.BlockSize + OffsetInByte) % sizeof(pBuffer) != 0)
	{
		i -= sizeof(pBuffer);
		for (; i < w25qxx.BlockSize; i++)
		{

			WorkAddress = (i + Block_Address * w25qxx.BlockSize);

			W25qxx_CSLow();
			W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
			W25qxx_SendAddress(WorkAddress);
			W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
			W25qxx_Receive(pBuffer, 1);
			W25qxx_CSHigh();

			if (pBuffer[0] != 0xFF)
			{
				isempty = 0;
				break;
			}
		}
	}

	if (!isempty)
	{
		W25qxx_Debug(printf("w25qxx CheckBlock is Not Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
		W25qxx_Debug(W25qxx_Delay(100));

		W25qxx_ReleaseMutex();
		return false;
	}

	W25qxx_Debug(printf("w25qxx CheckBlock is Empty in %lu ms\r\n", HAL_GetTick() - StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_ReleaseMutex();
	return true;
}

void W25qxx_WriteByte(uint8_t pBuffer, uint32_t WriteAddr_inBytes)
{
	W25qxx_TakeMutex();

	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());
	W25qxx_Debug(printf("w25qxx WriteByte 0x%02X at address %lu begin...", pBuffer, WriteAddr_inBytes));

	W25qxx_WaitForWriteEnd();
	W25qxx_WriteEnable();
	W25qxx_CSLow();

	W25qxx_Tranceive(W25QXX_WRITE_BYTE);
	W25qxx_SendAddress(WriteAddr_inBytes);

	W25qxx_Tranceive(pBuffer);
	W25qxx_CSHigh();
	W25qxx_WaitForWriteEnd();

	W25qxx_Debug(printf("w25qxx WriteByte done after %lu ms\r\n", HAL_GetTick() - StartTime));

	W25qxx_ReleaseMutex();
}
//###################################################################################################################
void W25qxx_WritePage(uint8_t *pBuffer, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_PageSize)
{
	W25qxx_TakeMutex();

	if (((NumByteToWrite_up_to_PageSize + OffsetInByte) > w25qxx.PageSize) || (NumByteToWrite_up_to_PageSize == 0))
		NumByteToWrite_up_to_PageSize = w25qxx.PageSize - OffsetInByte;
	if ((OffsetInByte + NumByteToWrite_up_to_PageSize) > w25qxx.PageSize)
		NumByteToWrite_up_to_PageSize = w25qxx.PageSize - OffsetInByte;

	W25qxx_Debug(printf("w25qxx WritePage:%lu, Offset:%lu, Writes %lu Bytes, begin...\r\n", Page_Address, OffsetInByte, NumByteToWrite_up_to_PageSize));
	W25qxx_Debug(W25qxx_Delay(100));
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());

	Page_Address = (Page_Address * w25qxx.PageSize) + OffsetInByte;

	W25qxx_WaitForWriteEnd();
	W25qxx_WriteEnable();
	W25qxx_CSLow();

	W25qxx_Tranceive(W25QXX_WRITE_BYTE);
	W25qxx_SendAddress(Page_Address);

	W25qxx_Transmit(pBuffer, NumByteToWrite_up_to_PageSize);

	W25qxx_CSHigh();
	W25qxx_WaitForWriteEnd();

	W25qxx_Debug(StartTime = HAL_GetTick() - StartTime);
	W25qxx_Debug_PrintData(pBuffer, NumByteToWrite_up_to_PageSize);
	W25qxx_Debug(printf("w25qxx WritePage done after %lu ms\r\n", StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_Delay(1);
	W25qxx_ReleaseMutex();
}
//###################################################################################################################
void W25qxx_WriteSector(uint8_t *pBuffer, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_SectorSize)
{
	if ((NumByteToWrite_up_to_SectorSize > w25qxx.SectorSize) || (NumByteToWrite_up_to_SectorSize == 0))
		NumByteToWrite_up_to_SectorSize = w25qxx.SectorSize;

	W25qxx_Debug(printf("+++w25qxx WriteSector:%lu, Offset:%lu ,Write %lu Bytes, begin...\r\n", Sector_Address, OffsetInByte, NumByteToWrite_up_to_SectorSize));
	W25qxx_Debug(W25qxx_Delay(100));

	if (OffsetInByte >= w25qxx.SectorSize)
	{
		W25qxx_Debug(printf("---w25qxx WriteSector Faild!\r\n"));
		W25qxx_Debug(W25qxx_Delay(100));
		return;
	}

	uint32_t StartPage;
	int32_t BytesToWrite;
	uint32_t LocalOffset;

	if ((OffsetInByte + NumByteToWrite_up_to_SectorSize) > w25qxx.SectorSize)
		BytesToWrite = w25qxx.SectorSize - OffsetInByte;
	else
		BytesToWrite = NumByteToWrite_up_to_SectorSize;

	StartPage = W25qxx_SectorToPage(Sector_Address) + (OffsetInByte / w25qxx.PageSize);
	LocalOffset = OffsetInByte % w25qxx.PageSize;

	do
	{
		W25qxx_WritePage(pBuffer, StartPage, LocalOffset, BytesToWrite);
		StartPage++;
		BytesToWrite -= w25qxx.PageSize - LocalOffset;
		pBuffer += w25qxx.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToWrite > 0);

	W25qxx_Debug(printf("---w25qxx WriteSector Done\r\n"));
	W25qxx_Debug(W25qxx_Delay(100));
}
//###################################################################################################################
void W25qxx_WriteBlock(uint8_t *pBuffer, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToWrite_up_to_BlockSize)
{
	if ((NumByteToWrite_up_to_BlockSize > w25qxx.BlockSize) || (NumByteToWrite_up_to_BlockSize == 0))
		NumByteToWrite_up_to_BlockSize = w25qxx.BlockSize;

	W25qxx_Debug(printf("+++w25qxx WriteBlock:%lu, Offset:%lu ,Write %lu Bytes, begin...\r\n", Block_Address, OffsetInByte, NumByteToWrite_up_to_BlockSize));
	W25qxx_Debug(W25qxx_Delay(100));

	if (OffsetInByte >= w25qxx.BlockSize)
	{
		W25qxx_Debug(printf("---w25qxx WriteBlock Faild!\r\n"));
		W25qxx_Debug(W25qxx_Delay(100));
		return;
	}

	uint32_t StartPage;
	int32_t BytesToWrite;
	uint32_t LocalOffset;

	if ((OffsetInByte + NumByteToWrite_up_to_BlockSize) > w25qxx.BlockSize)
		BytesToWrite = w25qxx.BlockSize - OffsetInByte;
	else
		BytesToWrite = NumByteToWrite_up_to_BlockSize;

	StartPage = W25qxx_BlockToPage(Block_Address) + (OffsetInByte / w25qxx.PageSize);
	LocalOffset = OffsetInByte % w25qxx.PageSize;
	do
	{
		W25qxx_WritePage(pBuffer, StartPage, LocalOffset, BytesToWrite);
		StartPage++;
		BytesToWrite -= w25qxx.PageSize - LocalOffset;
		pBuffer += w25qxx.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToWrite > 0);

	W25qxx_Debug(printf("---w25qxx WriteBlock Done\r\n"));
	W25qxx_Debug(W25qxx_Delay(100));
}
//###################################################################################################################
void W25qxx_ReadByte(uint8_t *pBuffer, uint32_t Bytes_Address)
{
	W25qxx_TakeMutex();

	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());
	W25qxx_Debug(printf("w25qxx ReadByte at address %lu begin...\r\n", Bytes_Address));

	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
	W25qxx_SendAddress(Bytes_Address);
	W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);

	*pBuffer = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
	W25qxx_CSHigh();

	W25qxx_Debug(printf("w25qxx ReadByte 0x%02X done after %lu ms\r\n", *pBuffer, HAL_GetTick() - StartTime));

	W25qxx_ReleaseMutex();
}
//###################################################################################################################
void W25qxx_ReadBytes(uint8_t *pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
	W25qxx_TakeMutex();
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());
	W25qxx_Debug(printf("w25qxx ReadBytes at Address:%lu, %lu Bytes  begin...\r\n", ReadAddr, NumByteToRead));

	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
	W25qxx_SendAddress(ReadAddr);
	W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);
	W25qxx_Receive(pBuffer, NumByteToRead);
	W25qxx_CSHigh();

	W25qxx_Debug(StartTime = HAL_GetTick() - StartTime);
	W25qxx_Debug_PrintData(pBuffer, NumByteToRead);
	W25qxx_Debug(printf("w25qxx ReadBytes done after %lu ms\r\n", StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_Delay(1);
	W25qxx_ReleaseMutex();
}
//###################################################################################################################
void W25qxx_ReadPage(uint8_t *pBuffer, uint32_t Page_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_PageSize)
{
	W25qxx_TakeMutex();

	if ((NumByteToRead_up_to_PageSize > w25qxx.PageSize) || (NumByteToRead_up_to_PageSize == 0))
		NumByteToRead_up_to_PageSize = w25qxx.PageSize;

	if ((OffsetInByte + NumByteToRead_up_to_PageSize) > w25qxx.PageSize)
		NumByteToRead_up_to_PageSize = w25qxx.PageSize - OffsetInByte;

	W25qxx_Debug(printf("w25qxx ReadPage:%lu, Offset:%lu ,Read %lu Bytes, begin...\r\n", Page_Address, OffsetInByte, NumByteToRead_up_to_PageSize));
	W25qxx_Debug(W25qxx_Delay(100));
	W25qxx_Debug(uint32_t StartTime = HAL_GetTick());

	Page_Address = Page_Address * w25qxx.PageSize + OffsetInByte;

	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_FAST_READ_BYTE);
	W25qxx_SendAddress(Page_Address);
	W25qxx_Tranceive(W25QXX_FAST_READ_END_BYTE);

	W25qxx_Receive(pBuffer, NumByteToRead_up_to_PageSize);

	W25qxx_CSHigh();

	W25qxx_Debug(StartTime = HAL_GetTick() - StartTime);
	W25qxx_Debug_PrintData(pBuffer, NumByteToRead_up_to_PageSize);
	W25qxx_Debug(printf("w25qxx ReadPage done after %lu ms\r\n", StartTime));
	W25qxx_Debug(W25qxx_Delay(100));

	W25qxx_Delay(1);
	W25qxx_ReleaseMutex();
}
//###################################################################################################################
void W25qxx_ReadSector(uint8_t *pBuffer, uint32_t Sector_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_SectorSize)
{
	if ((NumByteToRead_up_to_SectorSize > w25qxx.SectorSize) || (NumByteToRead_up_to_SectorSize == 0))
		NumByteToRead_up_to_SectorSize = w25qxx.SectorSize;

	W25qxx_Debug(printf("+++w25qxx ReadSector:%lu, Offset:%lu, Read %lu Bytes, begin...\r\n", Sector_Address, OffsetInByte, NumByteToRead_up_to_SectorSize));
	W25qxx_Debug(W25qxx_Delay(100));

	if (OffsetInByte >= w25qxx.SectorSize)
	{

		W25qxx_Debug(printf("---w25qxx ReadSector Faild!\r\n"));
		W25qxx_Debug(W25qxx_Delay(100));
		return;
	}

	uint32_t StartPage;
	int32_t BytesToRead;
	uint32_t LocalOffset;

	if ((OffsetInByte + NumByteToRead_up_to_SectorSize) > w25qxx.SectorSize)
		BytesToRead = w25qxx.SectorSize - OffsetInByte;
	else
		BytesToRead = NumByteToRead_up_to_SectorSize;

	StartPage = W25qxx_SectorToPage(Sector_Address) + (OffsetInByte / w25qxx.PageSize);
	LocalOffset = OffsetInByte % w25qxx.PageSize;

	do
	{
		W25qxx_ReadPage(pBuffer, StartPage, LocalOffset, BytesToRead);
		StartPage++;
		BytesToRead -= w25qxx.PageSize - LocalOffset;
		pBuffer += w25qxx.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToRead > 0);

	W25qxx_Debug(printf("---w25qxx ReadSector Done\r\n"));
	W25qxx_Debug(W25qxx_Delay(100));
}
//###################################################################################################################
void W25qxx_ReadBlock(uint8_t *pBuffer, uint32_t Block_Address, uint32_t OffsetInByte, uint32_t NumByteToRead_up_to_BlockSize)
{
	if ((NumByteToRead_up_to_BlockSize > w25qxx.BlockSize) || (NumByteToRead_up_to_BlockSize == 0))
		NumByteToRead_up_to_BlockSize = w25qxx.BlockSize;

	W25qxx_Debug(printf("+++w25qxx ReadBlock:%lu, Offset:%lu ,Read %lu Bytes, begin...\r\n", Block_Address, OffsetInByte, NumByteToRead_up_to_BlockSize));
	W25qxx_Debug(W25qxx_Delay(100));

	if (OffsetInByte >= w25qxx.BlockSize)
	{
		W25qxx_Debug(printf("w25qxx ReadBlock Faild!\r\n"));
		W25qxx_Debug(W25qxx_Delay(100));
		return;
	}

	uint32_t StartPage;
	int32_t BytesToRead;
	uint32_t LocalOffset;

	if ((OffsetInByte + NumByteToRead_up_to_BlockSize) > w25qxx.BlockSize)
		BytesToRead = w25qxx.BlockSize - OffsetInByte;
	else
		BytesToRead = NumByteToRead_up_to_BlockSize;

	StartPage = W25qxx_BlockToPage(Block_Address) + (OffsetInByte / w25qxx.PageSize);
	LocalOffset = OffsetInByte % w25qxx.PageSize;

	do
	{
		W25qxx_ReadPage(pBuffer, StartPage, LocalOffset, BytesToRead);
		StartPage++;
		BytesToRead -= w25qxx.PageSize - LocalOffset;
		pBuffer += w25qxx.PageSize - LocalOffset;
		LocalOffset = 0;
	} while (BytesToRead > 0);

	W25qxx_Debug(printf("---w25qxx ReadBlock Done\r\n"));
	W25qxx_Debug(W25qxx_Delay(100));
}
//###################################################################################################################
static void W25qxx_WriteEnable(void)
{
	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_WRITE_ENABLE_BYTE);
	W25qxx_CSHigh();
	W25qxx_Delay(1);
}
//###################################################################################################################
static void W25qxx_WriteDisable(void)
{
	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_WRITE_DISABLE_BYTE);
	W25qxx_CSHigh();
	W25qxx_Delay(1);
}
//###################################################################################################################
static void W25qxx_SendAddress(uint32_t addr)
{
	if (w25qxx.ID >= W25Q256)
	{
		W25qxx_Tranceive((addr >> 24) & 0xFF);
	}

	W25qxx_Tranceive((addr >> 16) & 0xFF);
	W25qxx_Tranceive((addr >> 8) & 0xFF);
	W25qxx_Tranceive((addr >> 0) & 0xFF);
}
//###################################################################################################################
static void W25qxx_WaitForWriteEnd(void)
{
	W25qxx_Delay(1);
	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_READ_SR1_BYTE);
	do
	{
		w25qxx.StatusRegister1 = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
		W25qxx_Delay(1);
	} while (W25qxx_IsBusy(w25qxx.StatusRegister1));
	W25qxx_CSHigh();
}
//###################################################################################################################
static uint8_t W25qxx_IsBufferEmpty(uint8_t *pBuffer, size_t len)
{
	uint8_t x;
	for (x = 0; x < len; x++)
	{
		if (pBuffer[x] != 0xFF)
			return 0;
	}
	return 1;
}
//###################################################################################################################
static void W25qxx_ReadUniqID(void)
{
	uint8_t i;

	W25qxx_CSLow();
	W25qxx_Tranceive(W25QXX_READ_UNIQUE_ID_BYTE);
	for (i = 0; i < 4; i++)
		W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
	for (uint8_t i = 0; i < 8; i++)
		w25qxx.UniqID[i] = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
	W25qxx_CSHigh();
}
//###################################################################################################################
static  uint32_t W25qxx_ReadID(void)
{
	uint8_t i;
	uint8_t ans;
	uint32_t ID = 0x0;

	W25qxx_CSLow();

	W25qxx_Tranceive(W25QXX_READ_ID_BYTE);

	for (i = 0; i < 3; i++)
	{
		ans = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
		ID = (ID << 8) | ans;
	}

	W25qxx_CSHigh();

	return ID;
}
//###################################################################################################################
void W25qxx_WriteStatusRegister(uint8_t sr_number, uint8_t data)
{
	W25qxx_CSLow();

	if (sr_number == 1)
	{
		W25qxx_Tranceive(W25QXX_WRITE_SR1_BYTE);
		w25qxx.StatusRegister1 = data;
	}
	else if (sr_number == 2)
	{
		W25qxx_Tranceive(W25QXX_WRITE_SR1_BYTE);
		w25qxx.StatusRegister2 = data;
	}
	else
	{
		W25qxx_Tranceive(W25QXX_WRITE_SR1_BYTE);
		w25qxx.StatusRegister3 = data;
	}
	W25qxx_Tranceive(data);

	W25qxx_CSHigh();
}
//###################################################################################################################
static uint8_t W25qxx_ReadStatusRegister(uint8_t sr_number)
{
	uint8_t status = 0;
	W25qxx_CSLow();

	if (sr_number == 1)
	{
		W25qxx_Tranceive(W25QXX_READ_SR1_BYTE);
		status = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
		w25qxx.StatusRegister1 = status;
	}
	else if (sr_number == 2)
	{
		W25qxx_Tranceive(W25QXX_READ_SR2_BYTE);
		status = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
		w25qxx.StatusRegister2 = status;
	}
	else
	{
		W25qxx_Tranceive(W25QXX_READ_SR3_BYTE);
		status = W25qxx_Tranceive(W25QXX_DUMMY_BYTE);
		w25qxx.StatusRegister3 = status;
	}

	W25qxx_CSHigh();

	return status;
}
//###################################################################################################################
static void W25qxx_Debug_PrintData(uint8_t *pBuffer, uint32_t len)
{
#if (_W25QXX_DEBUG == 1)
	for (uint32_t i = 0; i < len; i++)
	{
		if ((i % 8 == 0) && (i > 2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,", pBuffer[i]);
	}
	printf("\r\n");
#endif
}

/* ****************************** */
/*     MCU Dependant Codes        */
/* ****************************** */
//###################################################################################################################
static void W25qxx_CSLow(void)
{
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO, _W25QXX_CS_PIN, GPIO_PIN_RESET);
}
//###################################################################################################################
static void W25qxx_CSHigh(void)
{
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO, _W25QXX_CS_PIN, GPIO_PIN_SET);
}
//###################################################################################################################
static uint8_t W25qxx_Tranceive(uint8_t Data)
{
	uint8_t ret;
	HAL_SPI_TransmitReceive(&_W25QXX_SPI, &Data, &ret, 1, 100);
	return ret;
}
//###################################################################################################################
static void W25qxx_Transmit(uint8_t *pBuffer, uint32_t b2t)
{
	HAL_SPI_Transmit(&_W25QXX_SPI, pBuffer, b2t, 100);
}
//###################################################################################################################
static void W25qxx_Receive(uint8_t *pBuffer, uint32_t b2r)
{
	HAL_SPI_Receive(&_W25QXX_SPI, pBuffer, b2r, 2000);
}
//###################################################################################################################
static void W25qxx_TakeMutex(void)
{
	while (w25qxx.Lock == 1)
		W25qxx_Delay(1);
	w25qxx.Lock = 1;
}
//###################################################################################################################
static void W25qxx_ReleaseMutex(void)
{
	w25qxx.Lock = 0;
}
//###################################################################################################################
