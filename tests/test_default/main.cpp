#include "../common/benchmark.h"

int main()
{
    // modify below if customized configuration is needed
    pico_fatfs_spi_config_t config = {
        spi0,  // if unmatched SPI pin assignments with spi0/spi1 or explicitly designated as NULL, SPI PIO will be configured 
        CLK_SLOW_DEFAULT,
        CLK_FAST_DEFAULT,
        PIN_SPI0_MISO_DEFAULT,  // SPIx_RX
        PIN_SPI0_CS_DEFAULT,
        PIN_SPI0_SCK_DEFAULT,   // SPIx_SCK
        PIN_SPI0_MOSI_DEFAULT,  // SPIx_TX
        true   // use internal pullup
    };

    while (true) {
        benchmark(config);
    }

    return 0;
}
