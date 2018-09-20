# W25QXX SPI FLASH Library for STM32
<br />
I hope use it and enjoy.
<br />
I use Stm32f407vg and Keil Compiler and Stm32CubeMX wizard.
 <br />
Please Do This ...
<br />
<br />
1) Select "General peripheral Initalizion as a pair of '.c/.h' file per peripheral" on project settings.
<br />
2) Config "w25qxxconif.h".
<br />
3) Call W25qxx_Init(). 
<br />
4) After init, you can watch w25qxx struct.(Chip ID,page size,sector size and ...)
<br />
5) In Read/Write Function, you can put 0 to "NumByteToRead/NumByteToWrite" parameter to maximum.
<br />
6) Dont forget to erase page/sector/block before write.

