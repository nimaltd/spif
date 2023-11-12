## W25QXX SPI FLASH Library for STM32
* http://www.github.com/NimaLTD   
* https://www.instagram.com/github.nimaltd/   
* https://www.youtube.com/@nimaltd

* Enable SPIs and a GPIOs as output (CS pin).Connect WP and HOLD to VCC.
* Select software CS pin.
* Config `w25qxxConf.h`.
* Define one descriptor of type `w25qxx_peripheral` for each W25QXX SPI flash in your project
* Call `W25qxx_Init()` on each descriptor defined
* After init, you can watch `w25qxx` struct.(Chip ID,page size,sector size and ...)
* In Read/Write Function, you can put 0 to `NumByteToRead/NumByteToWrite` parameter to maximum.
* Dont forget to erase page/sector/block before write.

