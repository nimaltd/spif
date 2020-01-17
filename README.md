# W25QXX SPI FLASH Library for STM32
[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](YOUR_EMAIL_CODE)
<br />
I hope use it and enjoy.
<br />
I use Stm32f407vg and Keil Compiler and Stm32CubeMX wizard.
 <br />
Please Do This ...
<br />
<br />
1) Enable SPI and a Gpio as output(CS pin).Connect WP and HOLD to VCC.
<br />
2) Select "General peripheral Initalizion as a pair of '.c/.h' file per peripheral" on project settings.
<br />
3) Config "w25qxxConfig.h".
<br />
4) Call W25qxx_Init(). 
<br />
5) After init, you can watch w25qxx struct.(Chip ID,page size,sector size and ...)
<br />
6) In Read/Write Function, you can put 0 to "NumByteToRead/NumByteToWrite" parameter to maximum.
<br />
7) Dont forget to erase page/sector/block before write.

