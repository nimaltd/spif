## W25QXX SPI FLASH Library for STM32
* http://www.github.com/NimaLTD   
* https://www.instagram.com/github.nimaltd/   
* https://www.youtube.com/channel/UCUhY7qY1klJm1d2kulr9ckw   

* Enable SPI and a Gpio as output(CS pin).Connect WP and HOLD to VCC.
* Select software CS pin.
* Config `w25qxxConf.h`.
* Call `W25qxx_Init()`. 
* After init, you can watch `w25qxx` struct.(Chip ID,page size,sector size and ...)
* In Read/Write Function, you can put 0 to `NumByteToRead/NumByteToWrite` parameter to maximum.
* Dont forget to erase page/sector/block before write.

