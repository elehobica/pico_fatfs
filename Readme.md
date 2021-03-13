# Raspberry Pi Pico FatFs Test

## Overview
This project is an implementation example of FatFs on Raspberry Pi Pico.
This project supports:
* FatFs R0.14a_p2 ([http://elm-chan.org/fsw/ff/00index_e.html](http://elm-chan.org/fsw/ff/00index_e.html))
* SD card access by SPI interface
* SD, SDHC, SDXC cards
* FAT16, FAT32, exFAT formats
* write / read speed benchmark

## Supported Board
* Raspberry Pi Pico

## Ciruit Diagram
![Circuit Diagram](doc/Pico_FatFs_Test_Schematic.png)

## Pin Assignment
### microSD card connector

| Pico Pin # | Pin Name | Function | microSD connector |
----|----|----|----
|  4 | GP2 | SPI0_SCK | CLK (5) |
|  5 | GP3 | SPI0_TX | CMD (3) |
|  6 | GP4 | SPI0_RX | DAT0 (7) |
|  7 | GP5 | SPI0_CSn | CD/DAT3 (2) |
|  8 | GND | GND | VSS (6) |
| 36 | 3V3(OUT) | 3.3V | VDD (4) |

#### Caution
* SPI0_TX and SPI0_RX needs to be pull-ed up with 10Kohm.
* Wire length between Pico and SD card is very sensitive. Short wiring as possible is desired, otherwise errors such as Mount error, Preallocation error and Write fail will occur.

### Serial (CP2102 module)
| Pico Pin # | Pin Name | Function | CP2102 module |
----|----|----|----
|  1 | GP0 | UART0_TX | RXD |
|  2 | GP1 | UART0_RX | TXD |
|  3 | GND | GND | GND |

## How to build
* See ["Getting started with Raspberry Pi Pico"](https://datasheets.raspberrypi.org/pico/getting-started-with-pico.pdf)
* Build is confirmed only in Developer Command Prompt for VS 2019 and Visual Studio Code on Windows enviroment
* Put "pico-sdk" and "pico-examples" on the same level with this project folder.
```
> git clone -b master https://github.com/raspberrypi/pico-sdk.git
> cd pico-sdk
> git submodule update -i
> cd ..
> git clone -b master https://github.com/raspberrypi/pico-examples.git
> 
> git clone -b main https://github.com/elehobica/pico_fatfs_test.git
```
* Lanuch "Developer Command Prompt for VS 2019"
```
> cd pico_fatfs_test
> mkdir build
> cd build
> cmake -G "NMake Makefiles" ..
> nmake
```
* Put "sine_wave.uf2" on RPI-RP2 drive

## Benchmark Result
* PQI microSD 1GB
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is FAT16
Card size:    1.02 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
72.8148, 188153, 3175, 7028
72.7216, 188240, 3177, 7037

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
740.6934, 1591, 665, 690
740.8031, 1589, 665, 690
```

* Memorex microSD 2GB
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is FAT16
Card size:    2.00 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
79.2543, 222555, 2452, 6456
78.1408, 229029, 2457, 6549

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
757.2978, 2044, 533, 675
757.4125, 2039, 533, 675
```

* Transcend microSDHC 32GB (C4)
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is FAT32
Card size:   31.90 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
273.3709, 209797, 947, 1871
287.3379, 208416, 968, 1780

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
851.7343, 1192, 539, 600
851.8794, 1189, 539, 600
```

* Toshiba microSDXC 64GB (UHS-I C10)
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is EXFAT
Card size:   61.89 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
149.3869, 180241, 2065, 3424
155.4869, 173010, 2069, 3291

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
620.7698, 960, 621, 824
620.4617, 959, 621, 824
```

* SanDisk microSDXC Ultra A1 64GB (UHS-I U1)
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is EXFAT
Card size:   63.83 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
464.5679, 19560, 881, 1101
471.6234, 19881, 761, 1084

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
1238.1575, 422, 394, 412
1238.1575, 422, 399, 412
```

* SanDisk microSDXC Ultra A1 512GB (UHS-I U1)
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is EXFAT
Card size:  511.80 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
430.8212, 24998, 951, 1187
428.5318, 24974, 953, 1193

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
1195.5237, 443, 413, 427
1195.5237, 444, 415, 427
```

* SanDisk microSDXC Ultra A2 1TB (UHS-I U3)
```
=====================
== pico_fatfs_test ==
=====================
mount ok
Type is EXFAT
Card size: 1023.74 GB (GB = 1E9 bytes)

FILE_SIZE_MB = 5
BUF_SIZE = 512 bytes
Starting write test, please wait.

write speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
426.9217, 6290, 963, 1198
427.0676, 32321, 958, 1197

Starting read test, please wait.

read speed and latency
speed,max,min,avg
KB/Sec,usec,usec,usec
1192.9563, 445, 409, 428
1192.9563, 445, 413, 428
```
