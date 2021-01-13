ESP8266 TCP Client Sample Application
==============

This project represents typical ESP8266 TCP Client application maintaining basic functionality
to transmit sample single-byte of data to corresponding TCP Server side.
Source byte is generated based on 3 GPIO pins input levels, represented as 3 less-significant bits.
Demo application shows how 3 corresponding push buttons can be used to form this sample byte of data.

This application allows to demonstrate basic data transfer concepts between ESP Client node
and ESP Server node (over TCP protocol).
This particular repository contains ESP8266 TCP Client side implementation
(server side sample implementation is available at: https://github.com/sigma-prj/esp-tcp-server ).

The following topics are described and implemented within application's source code:
 * How to manage WiFi sessions (to set target WiFi session name, WiFi security mode and WiFi password)
 * How to handle\log current WiFi connection state
 * How to configure target server IP address to connect to (and port number)
 * How to transmit sample data over to server node
 * How to configure certain ESP GPIOs to input mode
 * How to poll GPIOs pins state using timer

Requirements and Dependencies
-----------------------------

This application is targeted to be built under ESP8266_NONOS_SDK platform.
Target ESP8266 NON OS SDK is available for downloading from https://www.espressif.com .
Application repository project files can be placed to any subfolder under SDK root folder.

Build and UART Logs Configuration
-----------------------------

In order to build this project, the standard ESP SDK procedure can be followed.
Build process can be triggered by execution of default target on a project's root Makefile
with specific set of arguments.

Typical Makefile target execution sample and arguments set can be found at ESP SDK example:
```
[esp-sdk]/examples/peripheral_test/gen_misc.sh
```

Particular shell command and argumets may depends on a specific ESP board configuration.
Below is the typical example to trigger build on a certain ESP configuration:

```sh
make COMPILE=gcc BOOT=none APP=0 SPI_SPEED=20 SPI_MODE=DIO SPI_SIZE_MAP=4 FLAVOR=release
```

This application also can be built with output debug UART logs enabled.
For these purposes, special symbol UART_DEBUG_LOGS can be defined in build configuration:

```sh
make COMPILE=gcc BOOT=none APP=0 SPI_SPEED=20 SPI_MODE=DIO SPI_SIZE_MAP=4 FLAVOR=release UNIVERSAL_TARGET_DEFINES=-DUART_DEBUG_LOGS
```

Flashing Compiled Binaries to ESP Chip
-----------------------------

Under scripts folder the following SH files are located:

```
scripts/erase_mem_non_ota_manual_rst.sh
scripts/flash_mem_non_ota_manual_rst.sh
```

These can be used to flash compiled binaries into ESP chip memory.
Erase script also allows to clean-up ESP memory before flashing actual application.
Once binaries are compiled, shell script can be triggered directly either it can be executed through corresponding Makefile targets:

```sh

# To erase ESP memory
make image_erase
# To flash actual application into ESP memory
make image_flash


```

Note: These scripts are coming with 'no_reset' option defined, suggesting that ESP chip needs to be reset manually before each erase\flash step.
In case of ESP module is used with autoreset hardware feature - there is a need to remove corresponding 'no_reset' command line arguments within SH files.

Sample Schema Design with Push-Buttons Input
-----------------------------

Schema sample design and other additional information about this project can be found on the following URL:

http://www.sigmaprj.com/esp8266-tcp-client-server.html

This demo example suggests having 3 push buttons used as GPIO PINs (12, 13, 14) input.

Application demo video is also available at the following YouTube link (starting at 27:09):

License
-----------------------------

ESP8266 TCP Client Sample Application

Project is distributed under GNU GENERAL PUBLIC LICENSE 3.0

Copyright (C) 2021 - www.sigmaprj.com

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

ESPRESSIF MIT License

Copyright (c) 2015 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>

Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case, it is free of charge, to any person obtaining a copy of this software and associated documentation files (the Software), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
