#ifndef _TF_CARD_H_
#define _TF_CARD_H_

#include "pico.h"
#include "pico/stdlib.h"
#include "hardware/structs/iobank0.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"

#include "ff.h"
#include "diskio.h"

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC         0x01            /* MMC ver 3 */
#define CT_SD1         0x02            /* SD ver 1 */
#define CT_SD2         0x04            /* SD ver 2 */
#define CT_SDC         (CT_SD1|CT_SD2) /* SD */
#define CT_BLOCK       0x08            /* Block addressing */

/* SPI pin assignment */
#define PIN_SPI0_CS     5
#define PIN_SPI0_SCK    6
#define PIN_SPI0_MISO   4
#define PIN_SPI0_MOSI   3

#endif
