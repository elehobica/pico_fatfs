#ifndef _TF_CARD_H_
#define _TF_CARD_H_

#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/structs/iobank0.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"

#include "ff.h"
#include "diskio.h"

/* SPI pin assignment */
#define PIN_SPI0_CS     5
#define PIN_SPI0_SCK    2
#define PIN_SPI0_MISO   4
#define PIN_SPI0_MOSI   3
/*
#define PIN_SPI0_CS     5
#define PIN_SPI0_SCK    6
#define PIN_SPI0_MISO   4
#define PIN_SPI0_MOSI   3
*/
/*
#define PIN_SPI0_CS     17
#define PIN_SPI0_SCK    18
#define PIN_SPI0_MISO   16
#define PIN_SPI0_MOSI   19
*/

#endif
