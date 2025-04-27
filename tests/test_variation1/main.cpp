#include "../common/benchmark.h"

int main()
{
    // modify below if customized configuration is needed
    //   Pin assignments for Pimoroni Pico DV demo base board
    pico_fatfs_spi_config_t config = {
        spi0,  // if unmatched SPI pin assignments with spi0/spi1 or explicitly designated as NULL, SPI PIO will be configured 
        CLK_SLOW_DEFAULT,
        CLK_FAST_DEFAULT,
        19,    // MISO (SPIx_RX)
        22,    // CS
         5,    // SCK  (SPIx_SCK)
        18,    // MOSI (SPIx_TX)
        true   // use internal pullup
    };

    while (true) {
        benchmark(config);
    }

    return 0;
}