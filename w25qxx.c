
#include <stdio.h>
#include "w25qxx.h"
#include "w25qxxConfig.h"



#define W25QXX_DUMMY_BYTE         0xA5

w25qxx_t	w25qxx;

#if (_W25QXX_USE_FREERTOS==1)
#define	W25qxx_Delay(delay)		osDelay(delay)
#include "cmsis_os.h"
#else
#define	W25qxx_Delay(delay)		HAL_Delay(delay)
#endif
//###################################################################################################################
uint8_t	W25qxx_Spi(uint8_t	Data)
{
	uint8_t	ret;
	HAL_SPI_TransmitReceive(&_W25QXX_SPI,&Data,&ret,1,10);
	return ret;	
}
//###################################################################################################################
uint32_t W25qxx_ReadID(void)
{
  uint32_t Temp = 0, Temp0 = 0, Temp1 = 0, Temp2 = 0;
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x9F);
  Temp0 = W25qxx_Spi(W25QXX_DUMMY_BYTE);
  Temp1 = W25qxx_Spi(W25QXX_DUMMY_BYTE);
  Temp2 = W25qxx_Spi(W25QXX_DUMMY_BYTE);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  Temp = (Temp0 << 16) | (Temp1 << 8) | Temp2;
  return Temp;
}
//###################################################################################################################
void W25qxx_ReadUniqID(void)
{
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x4B);
	for(uint8_t	i=0;i<4;i++)
		W25qxx_Spi(W25QXX_DUMMY_BYTE);
	for(uint8_t	i=0;i<8;i++)
		w25qxx.UniqID[i] = W25qxx_Spi(W25QXX_DUMMY_BYTE);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
}
//###################################################################################################################
void W25qxx_WriteEnable(void)
{
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x06);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
}
//###################################################################################################################
void W25qxx_WriteDisable(void)
{
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x04);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
}
//###################################################################################################################
uint8_t W25qxx_ReadStatusRegister(uint8_t	SelectStatusRegister_1_2_3)
{
	uint8_t	status=0;
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
	if(SelectStatusRegister_1_2_3==1)
	{
		W25qxx_Spi(0x05);
		status=W25qxx_Spi(W25QXX_DUMMY_BYTE);	
		w25qxx.StatusRegister1 = status;
	}
	else if(SelectStatusRegister_1_2_3==2)
	{
		W25qxx_Spi(0x35);
		status=W25qxx_Spi(W25QXX_DUMMY_BYTE);	
		w25qxx.StatusRegister2 = status;
	}
	else
	{
		W25qxx_Spi(0x15);
		status=W25qxx_Spi(W25QXX_DUMMY_BYTE);	
		w25qxx.StatusRegister3 = status;
	}	
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	return status;
}
//###################################################################################################################
void W25qxx_WriteStatusRegister(uint8_t	SelectStatusRegister_1_2_3,uint8_t Data)
{
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
	if(SelectStatusRegister_1_2_3==1)
	{
		W25qxx_Spi(0x01);
		w25qxx.StatusRegister1 = Data;
	}
	else if(SelectStatusRegister_1_2_3==2)
	{
		W25qxx_Spi(0x31);
		w25qxx.StatusRegister2 = Data;
	}
	else
	{
		W25qxx_Spi(0x11);
		w25qxx.StatusRegister3 = Data;
	}
	W25qxx_Spi(Data);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
}
//###################################################################################################################
void W25qxx_WaitForWriteEnd(void)
{
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
	W25qxx_Spi(0x05);
  do
  {
    w25qxx.StatusRegister1 = W25qxx_Spi(W25QXX_DUMMY_BYTE);
		W25qxx_Delay(1);
  }
  while ((w25qxx.StatusRegister1 & 0x01) == 0x01);
 HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
}
//###################################################################################################################
bool	W25qxx_Init(void)
{
	while(HAL_GetTick()<100)
		W25qxx_Delay(1);
	uint32_t	id;
	id=W25qxx_ReadID();
	switch(id&0x0000FFFF)
	{
		case 0x401A:	// 	w25q512
			w25qxx.ID=W25Q512;
		break;
		case 0x4019:	// 	w25q256
			w25qxx.ID=W25Q256;
		break;
		case 0x4018:	// 	w25q128
			w25qxx.ID=W25Q128;
		break;
		case 0x4017:	//	w25q64
			w25qxx.ID=W25Q64;
		break;
		case 0x4016:	//	w25q32
			w25qxx.ID=W25Q32;
		break;
		case 0x4015:	//	w25q16
			w25qxx.ID=W25Q16;
		break;
		case 0x4014:	//	w25q80
			w25qxx.ID=W25Q80;
		break;
		case 0x4013:	//	w25q40
			w25qxx.ID=W25Q40;
		break;
		case 0x4012:	//	w25q20
			w25qxx.ID=W25Q20;
		break;
		case 0x4011:	//	w25q10
			w25qxx.ID=W25Q10;
		break;
		default:
			return false;
				
	}		
	W25qxx_ReadUniqID();
	W25qxx_ReadStatusRegister(1);
	W25qxx_ReadStatusRegister(2);
	W25qxx_ReadStatusRegister(3);
	return true;
}	
//###################################################################################################################
void	W25qxx_EraseChip(void)
{
	W25qxx_WriteEnable();
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0xC7);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd();
}
//###################################################################################################################
void W25qxx_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x03);
  W25qxx_Spi((ReadAddr & 0xFF0000) >> 16);
  W25qxx_Spi((ReadAddr& 0xFF00) >> 8);
  W25qxx_Spi(ReadAddr & 0xFF);
  while (NumByteToRead--) 
  {
    *pBuffer = W25qxx_Spi(W25QXX_DUMMY_BYTE);
    pBuffer++;
  }
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
}
//###################################################################################################################
void W25qxx_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x02);
  W25qxx_Spi((WriteAddr & 0xFF0000) >> 16);
  W25qxx_Spi((WriteAddr & 0xFF00) >> 8);
  W25qxx_Spi(WriteAddr & 0xFF);
  while (NumByteToWrite--)
  {
    W25qxx_Spi(*pBuffer);
    pBuffer++;
  }
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
}
//###################################################################################################################
void W25qxx_WriteBuffer(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
  uint8_t NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;
  Addr = WriteAddr % 0x1000;
  count = 0x1000 - Addr;
  NumOfPage =  NumByteToWrite / 0x1000;
  NumOfSingle = NumByteToWrite % 0x1000;
  if (Addr == 0) 
  {
    if (NumOfPage == 0)
    {
      W25qxx_WritePage(pBuffer, WriteAddr, NumByteToWrite);
    }
    else
    {
      while (NumOfPage--)
      {
        W25qxx_WritePage(pBuffer, WriteAddr, 0x1000);
        WriteAddr +=  0x1000;
        pBuffer += 0x1000;
      }

      W25qxx_WritePage(pBuffer, WriteAddr, NumOfSingle);
    }
  }
  else 
  {
    if (NumOfPage == 0) 
    {
      if (NumOfSingle > count) 
      {
        temp = NumOfSingle - count;
        W25qxx_WritePage(pBuffer, WriteAddr, count);
        WriteAddr +=  count;
        pBuffer += count;
        W25qxx_WritePage(pBuffer, WriteAddr, temp);
      }
      else
      {
        W25qxx_WritePage(pBuffer, WriteAddr, NumByteToWrite);
      }
    }
    else 
    {
      NumByteToWrite -= count;
      NumOfPage =  NumByteToWrite / 0x1000;
      NumOfSingle = NumByteToWrite % 0x1000;
      W25qxx_WritePage(pBuffer, WriteAddr, count);
      WriteAddr +=  count;
      pBuffer += count;
      while (NumOfPage--)
      {
        W25qxx_WritePage(pBuffer, WriteAddr, 0x1000);
        WriteAddr +=  0x1000;
        pBuffer += 0x1000;
      }
      if (NumOfSingle != 0)
      {
        W25qxx_WritePage(pBuffer, WriteAddr, NumOfSingle);
      }
    }
  }
}
//###################################################################################################################
void W25qxx_EraseSector(uint32_t SectorAddr)
{
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x20);
  W25qxx_Spi((SectorAddr & 0xFF0000) >> 16);
  W25qxx_Spi((SectorAddr & 0xFF00) >> 8);
  W25qxx_Spi(SectorAddr & 0xFF);
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
}
//###################################################################################################################
void W25qxx_EraseBlock(uint32_t BlockAddr)
{
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0xD8);
  W25qxx_Spi((BlockAddr & 0xFF0000) >> 16);
  W25qxx_Spi((BlockAddr & 0xFF00) >> 8);
  W25qxx_Spi(BlockAddr & 0xFF);
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
}
//###################################################################################################################
