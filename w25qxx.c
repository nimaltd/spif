
#if (_W25QXX_DEBUG==1)
#include <stdio.h>
#endif
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
	W25qxx_Delay(1);
}
//###################################################################################################################
void W25qxx_WriteDisable(void)
{
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x04);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	W25qxx_Delay(1);
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
	W25qxx_Delay(1);
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
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx Init Begin...\r\n");
	#endif
	id=W25qxx_ReadID();
	
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx ID:0x%X\r\n",id);
	#endif
	switch(id&0x0000FFFF)
	{
		case 0x401A:	// 	w25q512
			w25qxx.ID=W25Q512;
			w25qxx.BlockCount=1024;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q512\r\n");
			#endif
		break;
		case 0x4019:	// 	w25q256
			w25qxx.ID=W25Q256;
			w25qxx.BlockCount=512;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q256\r\n");
			#endif
		break;
		case 0x4018:	// 	w25q128
			w25qxx.ID=W25Q128;
			w25qxx.BlockCount=256;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q128\r\n");
			#endif
		break;
		case 0x4017:	//	w25q64
			w25qxx.ID=W25Q64;
			w25qxx.BlockCount=128;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q64\r\n");
			#endif
		break;
		case 0x4016:	//	w25q32
			w25qxx.ID=W25Q32;
			w25qxx.BlockCount=64;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q32\r\n");
			#endif
		break;
		case 0x4015:	//	w25q16
			w25qxx.ID=W25Q16;
			w25qxx.BlockCount=32;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q16\r\n");
			#endif
		break;
		case 0x4014:	//	w25q80
			w25qxx.ID=W25Q80;
			w25qxx.BlockCount=16;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q80\r\n");
			#endif
		break;
		case 0x4013:	//	w25q40
			w25qxx.ID=W25Q40;
			w25qxx.BlockCount=8;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q40\r\n");
			#endif
		break;
		case 0x4012:	//	w25q20
			w25qxx.ID=W25Q20;
			w25qxx.BlockCount=4;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q20\r\n");
			#endif
		break;
		case 0x4011:	//	w25q10
			w25qxx.ID=W25Q10;
			w25qxx.BlockCount=2;
			#if (_W25QXX_DEBUG==1)
			printf("w25qxx Chip: w25q10\r\n");
			#endif
		break;
		default:
				#if (_W25QXX_DEBUG==1)
				printf("w25qxx Unknown ID\r\n");
				#endif
			return false;
				
	}		
	w25qxx.PageSize=256;
	w25qxx.SectorSize=0x1000;
	w25qxx.SectorCount=w25qxx.BlockCount*16;
	w25qxx.PageCount=(w25qxx.SectorCount*w25qxx.SectorSize)/w25qxx.PageSize;
	w25qxx.CapacityInKiloByte=(w25qxx.SectorCount*w25qxx.SectorSize)/1024;
	W25qxx_ReadUniqID();
	W25qxx_ReadStatusRegister(1);
	W25qxx_ReadStatusRegister(2);
	W25qxx_ReadStatusRegister(3);
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx Page Size: %d Bytes\r\n",w25qxx.PageSize);
	printf("w25qxx Page Count: %d\r\n",w25qxx.PageCount);
	printf("w25qxx Sector Size: %d Bytes\r\n",w25qxx.SectorSize);
	printf("w25qxx Sector Count: %d\r\n",w25qxx.SectorCount);
	printf("w25qxx Capacity: %d KiloBytes\r\n",w25qxx.CapacityInKiloByte);
	printf("w25qxx Init Done\r\n");
	#endif
	return true;
}	
//###################################################################################################################
void	W25qxx_EraseChip(void)
{
	#if (_W25QXX_DEBUG==1)
	uint32_t	StartTime=HAL_GetTick();	
	printf("w25qxx EraseChip Begin...\r\n");
	#endif
	W25qxx_WriteEnable();
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0xC7);
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	W25qxx_WaitForWriteEnd();
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx EraseBlock done after %d ms!\r\n",HAL_GetTick()-StartTime);
	#endif
	W25qxx_Delay(10);
}
//###################################################################################################################
void W25qxx_EraseSector(uint32_t SectorAddr)
{
	#if (_W25QXX_DEBUG==1)
	uint32_t	StartTime=HAL_GetTick();	
	printf("w25qxx EraseSector %d Begin...\r\n",SectorAddr);
	#endif
	W25qxx_WaitForWriteEnd();
	SectorAddr = SectorAddr * w25qxx.SectorSize;
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x20);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((SectorAddr & 0xFF000000) >> 24);
  W25qxx_Spi((SectorAddr & 0xFF0000) >> 16);
  W25qxx_Spi((SectorAddr & 0xFF00) >> 8);
  W25qxx_Spi(SectorAddr & 0xFF);
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx EraseSector done after %d ms\r\n",HAL_GetTick()-StartTime);
	#endif
	W25qxx_Delay(1);
}
//###################################################################################################################
void W25qxx_EraseBlock(uint32_t BlockAddr)
{
	#if (_W25QXX_DEBUG==1)
	uint32_t	StartTime=HAL_GetTick();	
	printf("w25qxx EraseBlock %d Begin...\r\n",BlockAddr);
	#endif
	W25qxx_WaitForWriteEnd();
	BlockAddr = BlockAddr * w25qxx.SectorSize*16;
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0xD8);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((BlockAddr & 0xFF000000) >> 24);
  W25qxx_Spi((BlockAddr & 0xFF0000) >> 16);
  W25qxx_Spi((BlockAddr & 0xFF00) >> 8);
  W25qxx_Spi(BlockAddr & 0xFF);
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx EraseBlock done after %d ms\r\n",HAL_GetTick()-StartTime);
	#endif
	W25qxx_Delay(1);
}
//###################################################################################################################
void W25qxx_WriteByte(uint8_t pBuffer, uint32_t WriteAddr_inBytes)
{
	#if (_W25QXX_DEBUG==1)
	uint32_t	StartTime=HAL_GetTick();
	printf("w25qxx WriteByte 0x%02X at address %d begin...",pBuffer,WriteAddr_inBytes);
	#endif
	W25qxx_WaitForWriteEnd();
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x02);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((WriteAddr_inBytes & 0xFF000000) >> 24);
  W25qxx_Spi((WriteAddr_inBytes & 0xFF0000) >> 16);
  W25qxx_Spi((WriteAddr_inBytes & 0xFF00) >> 8);
  W25qxx_Spi(WriteAddr_inBytes & 0xFF);
  W25qxx_Spi(pBuffer);
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx WriteByte done after %d ms\r\n",HAL_GetTick()-StartTime);
	#endif
}
//###################################################################################################################
void 	W25qxx_WritePage	(uint8_t *pBuffer	,uint32_t Page_Address		,uint32_t NumByteToWrite_up_to_PageSize)
{
	if(NumByteToWrite_up_to_PageSize>w25qxx.PageSize)
		NumByteToWrite_up_to_PageSize=w25qxx.PageSize;
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx WritePage %d begin...\r\n",Page_Address);
	W25qxx_Delay(100);
	uint32_t	StartTime=HAL_GetTick();
	#endif	
	W25qxx_WaitForWriteEnd();
  W25qxx_WriteEnable();
  HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x02);
	Page_Address = Page_Address*w25qxx.PageSize;
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((Page_Address & 0xFF000000) >> 24);
  W25qxx_Spi((Page_Address & 0xFF0000) >> 16);
  W25qxx_Spi((Page_Address & 0xFF00) >> 8);
  W25qxx_Spi(Page_Address&0xFF);
	HAL_SPI_Transmit(&_W25QXX_SPI,pBuffer,NumByteToWrite_up_to_PageSize,100);	
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
  W25qxx_WaitForWriteEnd();
	#if (_W25QXX_DEBUG==1)
	StartTime = HAL_GetTick()-StartTime; 
	for(uint32_t i=0;i<NumByteToWrite_up_to_PageSize ; i++)
	{
		if((i%8==0)&&(i>2))
		{
			printf("\r\n");
			W25qxx_Delay(10);			
		}
		printf("0x%02X,",pBuffer[i]);		
	}	
	printf("\r\n");
	printf("w25qxx WritePage done after %d ms\r\n",StartTime);
	W25qxx_Delay(100);
	#endif	
	W25qxx_Delay(1);
}
//###################################################################################################################
void 	W25qxx_WriteSector(uint8_t *pBuffer	,uint32_t Sector_Address	,uint32_t NumByteToWrite_up_to_SectorSize)
{
	uint8_t	inSectorIndex=0;
	if(NumByteToWrite_up_to_SectorSize>w25qxx.SectorSize)
		NumByteToWrite_up_to_SectorSize=w25qxx.SectorSize;
	do
	{
		W25qxx_WritePage(pBuffer,(Sector_Address*w25qxx.SectorSize/w25qxx.PageSize)+inSectorIndex,NumByteToWrite_up_to_SectorSize);
		if(NumByteToWrite_up_to_SectorSize<=w25qxx.PageSize)
			return;
		inSectorIndex++;
		NumByteToWrite_up_to_SectorSize-=w25qxx.PageSize;
		pBuffer+=w25qxx.PageSize;		
	}while(NumByteToWrite_up_to_SectorSize>0);
}
//###################################################################################################################
//void 	W25qxx_WriteBlock	(uint8_t* pBuffer ,uint32_t Block_Address		,uint32_t	NumByteToWrite_up_to_BlockSize)
//{

//}
//###################################################################################################################
void 	W25qxx_ReadByte		(uint8_t *pBuffer	,uint32_t Bytes_Address)
{
	#if (_W25QXX_DEBUG==1)
	uint32_t	StartTime=HAL_GetTick();
	printf("w25qxx ReadByte at address %d begin...\r\n",Bytes_Address);
	#endif
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
  W25qxx_Spi(0x0B);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((Bytes_Address & 0xFF000000) >> 24);
  W25qxx_Spi((Bytes_Address & 0xFF0000) >> 16);
  W25qxx_Spi((Bytes_Address& 0xFF00) >> 8);
  W25qxx_Spi(Bytes_Address & 0xFF);
	W25qxx_Spi(0);
	*pBuffer = W25qxx_Spi(W25QXX_DUMMY_BYTE);
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);	
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx ReadByte 0x%02X done after %d ms\r\n",*pBuffer,HAL_GetTick()-StartTime);
	#endif
}
//###################################################################################################################
void W25qxx_ReadBytes(uint8_t* pBuffer, uint32_t ReadAddr, uint32_t NumByteToRead)
{
	#if (_W25QXX_DEBUG==1)
	uint32_t	StartTime=HAL_GetTick();
	printf("w25qxx ReadBytes %d begin...\r\n",ReadAddr);
	#endif	
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
	W25qxx_Spi(0x0B);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((ReadAddr & 0xFF000000) >> 24);
  W25qxx_Spi((ReadAddr & 0xFF0000) >> 16);
  W25qxx_Spi((ReadAddr& 0xFF00) >> 8);
  W25qxx_Spi(ReadAddr & 0xFF);
	W25qxx_Spi(0);
	HAL_SPI_Receive(&_W25QXX_SPI,pBuffer,ReadAddr,2000);	
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	#if (_W25QXX_DEBUG==1)
	StartTime = HAL_GetTick()-StartTime; 
	for(uint32_t i=0;i<NumByteToRead ; i++)
	{
		if((i%8==0)&&(i>2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,",pBuffer[i]);		
	}
	printf("\r\n");
	printf("w25qxx ReadBytes done after %d ms\r\n",StartTime);
	W25qxx_Delay(100);
	#endif	
	W25qxx_Delay(1);
}
//###################################################################################################################
void 	W25qxx_ReadPage	  (uint8_t *pBuffer	,uint32_t Page_Address		,uint32_t NumByteToWrite_up_to_PageSize)
{
	if(NumByteToWrite_up_to_PageSize>w25qxx.PageSize)
		NumByteToWrite_up_to_PageSize=w25qxx.PageSize;
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx ReadPage %d begin...\r\n",Page_Address);
	W25qxx_Delay(100);
	uint32_t	StartTime=HAL_GetTick();
	#endif	
	Page_Address = Page_Address*w25qxx.PageSize;
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
	W25qxx_Spi(0x0B);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((Page_Address & 0xFF000000) >> 24);
  W25qxx_Spi((Page_Address & 0xFF0000) >> 16);
  W25qxx_Spi((Page_Address& 0xFF00) >> 8);
  W25qxx_Spi(Page_Address & 0xFF);
	W25qxx_Spi(0);
	HAL_SPI_Receive(&_W25QXX_SPI,pBuffer,NumByteToWrite_up_to_PageSize,100);	
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	#if (_W25QXX_DEBUG==1)
	StartTime = HAL_GetTick()-StartTime; 
	for(uint32_t i=0;i<NumByteToWrite_up_to_PageSize ; i++)
	{
		if((i%8==0)&&(i>2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,",pBuffer[i]);		
	}	
	printf("\r\n");
	printf("w25qxx ReadPage done after %d ms\r\n",StartTime);
	W25qxx_Delay(100);
	#endif	
	W25qxx_Delay(1);
}
//###################################################################################################################
void 	W25qxx_ReadSector (uint8_t *pBuffer	,uint32_t Sector_Address	,uint32_t NumByteToWrite_up_to_SectorSize)
{
	if(NumByteToWrite_up_to_SectorSize>w25qxx.SectorSize)
		NumByteToWrite_up_to_SectorSize=w25qxx.SectorSize;
	#if (_W25QXX_DEBUG==1)
	printf("w25qxx ReadSector %d begin...\r\n",Sector_Address);
	W25qxx_Delay(100);
	uint32_t	StartTime=HAL_GetTick();
	#endif	
	Sector_Address = Sector_Address*w25qxx.SectorSize;
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_RESET);
	W25qxx_Spi(0x0B);
	if(w25qxx.ID>=W25Q256)
		W25qxx_Spi((Sector_Address & 0xFF000000) >> 24);
  W25qxx_Spi((Sector_Address & 0xFF0000) >> 16);
  W25qxx_Spi((Sector_Address& 0xFF00) >> 8);
  W25qxx_Spi(Sector_Address & 0xFF);
	W25qxx_Spi(0);
	HAL_SPI_Receive(&_W25QXX_SPI,pBuffer,NumByteToWrite_up_to_SectorSize,100);		
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO,_W25QXX_CS_PIN,GPIO_PIN_SET);
	#if (_W25QXX_DEBUG==1)
	StartTime = HAL_GetTick()-StartTime; 
	for(uint32_t i=0;i<NumByteToWrite_up_to_SectorSize ; i++)
	{
		if((i%8==0)&&(i>2))
		{
			printf("\r\n");
			W25qxx_Delay(10);
		}
		printf("0x%02X,",pBuffer[i]);		
	}	
	printf("\r\n");
	printf("w25qxx ReadSector done after %d ms\r\n",StartTime);
	W25qxx_Delay(100);
	#endif	
	W25qxx_Delay(1);
}
//###################################################################################################################
//void 	W25qxx_ReadBlock	(uint8_t* pBuffer ,uint32_t Block_Address		,uint32_t	NumByteToWrite_up_to_BlockSize)
//{
//	
//}
//###################################################################################################################

