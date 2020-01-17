If you like this library, please donate:      [[!Donate](https://img.shields.io/badge/donate-WebMoney-green)](Z121342638771)


## W25QXX SPI FLASH Library for STM32
1. Enable SPI and a Gpio as output(CS pin).Connect WP and HOLD to VCC.
2. Select `General peripheral Initalizion as a pair of '.c/.h' file per peripheral` on project settings.
3. Config `w25qxxConf.h`.
4. Call `W25qxx_Init()`. 
5. After init, you can watch `w25qxx` struct.(Chip ID,page size,sector size and ...)
6. In Read/Write Function, you can put 0 to `NumByteToRead/NumByteToWrite` parameter to maximum.
7. Dont forget to erase page/sector/block before write.

