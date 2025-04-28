#include "benchmark.h"

#include <cstdint>
#include <cstdio>

#include "hardware/adc.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "tf_card.h"
#include "ff.h"

const uint32_t PIN_LED = 25;  // only for Pico
bool _picoW = false;
bool _led = false;

#if defined(PICO_RP2350A)
#define PICO_STR "Pico 2 (rp2350)"
#define PICOW_STR "Pico 2 W (rp2350)"
#else
#define PICO_STR "Pico (rp2040)"
#define PICOW_STR "Pico W (rp2040)"
#endif

// Set PRE_ALLOCATE true to pre-allocate file clusters.
const bool PRE_ALLOCATE = true;

// Set SKIP_FIRST_LATENCY true if the first read/write to the SD can
// be avoid by writing a file header or reading the first record.
const bool SKIP_FIRST_LATENCY = true;

// Size of read/write.
const size_t BUF_SIZE = 512;

// File size in MB where MB = 1,000,000 bytes.
const uint32_t FILE_SIZE_MB = 5;

// Write pass count.
const uint8_t WRITE_COUNT = 2;

// Read pass count.
const uint8_t READ_COUNT = 2;
//==============================================================================
// End of configuration constants.
//------------------------------------------------------------------------------
// File size in bytes.
const uint32_t FILE_SIZE = 1000000UL*FILE_SIZE_MB;

// Insure 4-byte alignment.
uint32_t buf32[(BUF_SIZE + 3)/4];
uint8_t* buf = (uint8_t*)buf32;

static bool _check_pico_w()
{
    adc_init();
    auto dir = gpio_get_dir(29);
    auto fnc = gpio_get_function(29);
    adc_gpio_init(29);
    adc_select_input(3);
    auto adc29 = adc_read();
    gpio_set_function(29, fnc);
    gpio_set_dir(29, dir);

    dir = gpio_get_dir(25);
    fnc = gpio_get_function(25);
    gpio_init(25);
    gpio_set_dir(25, GPIO_IN);
    auto gp25 = gpio_get(25);
    gpio_set_function(25, fnc);
    gpio_set_dir(25, dir);

    if (gp25) {
        return true; // Can't tell, so assume yes
    } else if (adc29 < 200) {
        return true; // PicoW
    } else {
        return false;
    }
}

static void _set_led(bool flag)
{
    if (_picoW) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, flag);
    } else {
        gpio_put(PIN_LED, flag);
    }
    _led = flag;
}

static void _toggle_led()
{
    _set_led(!_led);
}

static void _error_blink(int count)
{
    while (true) {
        for (int i = 0; i < count; i++) {
            _set_led(true);
            sleep_ms(250);
            _set_led(false);
            sleep_ms(250);
        }
        _set_led(false);
        sleep_ms(500);
    }
}

int benchmark(pico_fatfs_spi_config_t config)
{
    pico_fatfs_spi_config_t _config = config;

    FATFS fs;
    FIL fil;
    FRESULT fr;     /* FatFs return code */
    UINT br;
    UINT bw;

    float s;
    uint32_t t;
    uint32_t maxLatency;
    uint32_t minLatency;
    uint32_t totalLatency;
    bool skipLatency;

    stdio_init_all();

    // Initialise UART 0
    uart_init(uart0, 115200);
    // Set the GPIO pin mux to the UART - 0 is TX, 1 is RX
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);

    printf("\r\n");

    // Pico / Pico W dependencies
    _picoW = _check_pico_w();
    if (_picoW) {
        if (cyw43_arch_init()) {  // this is needed for driving LED
            printf("cyw43 init failed\r\n");
            return 1;
        }
    } else {
        // LED
        gpio_init(PIN_LED);
        gpio_set_dir(PIN_LED, GPIO_OUT);
    }
    _set_led(false);


    // Discard any input.
    while (uart_is_readable(uart0)) {
        uart_getc(uart0);
    }
    printf("Type any character to start (*: Auto select from SPI / SPI PIO, p: SPI PIO)\r\n");
    while (!uart_is_readable_within_us(uart0, 1000)) {};

    int chr;
    while ((chr = getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {}

    if (chr == 'p') {
        _config.spi_inst = NULL;
    }

    printf("=====================\r\n");
    printf("== pico_fatfs_test ==\r\n");
    printf("=====================\r\n");

    if (_picoW) {
        printf(PICOW_STR"\r\n");
    } else {
        printf(PICO_STR"\r\n");
    }

    bool spi_configured = pico_fatfs_set_config(&_config);
    if (spi_configured) {
        printf("SPI configured\r\n");
    } else {
        // modify if customized configuration for SPI PIO is needed
        pico_fatfs_config_spi_pio(SPI_PIO_DEFAULT_PIO, SPI_PIO_DEFAULT_SM);
        printf("SPI PIO configured\r\n");
    }
    printf("Configured clk_slow: %6.2f KHz, clk_fast: %5.2f MHz\r\n", pico_fatfs_get_clk_slow_freq() / 1e3, pico_fatfs_get_clk_fast_freq() / 1e6);


    for (int i = 0; i < 5; i++) {
        fr = f_mount(&fs, "", 1);
        if (fr == FR_OK) { break; }
        printf("mount error %d -> retry %d\r\n", fr, i);
        pico_fatfs_reboot_spi();
    }
    if (fr != FR_OK) {
        printf("mount error %d\r\n", fr);
        _error_blink(1);
    }
    printf("Operation  clk_slow: %6.2f KHz, clk_fast: %5.2f MHz\r\n", pico_fatfs_get_clk_slow_freq() / 1e3, pico_fatfs_get_clk_fast_freq() / 1e6);
    printf("mount ok\r\n");

    switch (fs.fs_type) {
        case FS_FAT12:
            printf("Type is FAT12\r\n");
            break;
        case FS_FAT16:
            printf("Type is FAT16\r\n");
            break;
        case FS_FAT32:
            printf("Type is FAT32\r\n");
            break;
        case FS_EXFAT:
            printf("Type is EXFAT\r\n");
            break;
        default:
            printf("Type is unknown\r\n");
            break;
    }
    printf("Card size: %7.2f GB (GB = 1E9 bytes)\r\n\r\n", fs.csize * fs.n_fatent * 512E-9);

#if 0
    // Print CID
    BYTE cid[16];
    disk_ioctl(0, MMC_GET_CID, cid);
    printf("Manufacturer ID: %02x\r\n", (int) cid[0]);
    printf("OEM ID: %02x%02x\r\n", (int) cid[1], (int) cid[2]);
    printf("Product: %c%c%c%c%c\r\n", cid[3], cid[4], cid[5], cid[6], cid[7]);
    printf("Version: %d.%d\r\n", (int) (cid[8] >> 4) & 0xf,  (int) cid[8] & 0xf);
    printf("Serial number: %02x%02x%02x%02x\r\n", (int) cid[9], (int) cid[10], (int) cid[11], (int) cid[12]);
    printf("Manufacturing date : %d/%d\r\n\r\n", (int) cid[14] & 0xf, ((int) cid[13] & 0xf)*16 + ((int) (cid[14] >> 2) & 0xf) + 2000);
#endif

    fr = f_open(&fil, "bench.dat", FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("open error %d\r\n", fr);
        _error_blink(2);
    }

    // fill buf with known data
    if (BUF_SIZE > 1) {
        for (size_t i = 0; i < (BUF_SIZE - 2); i++) {
            buf[i] = 'A' + (i % 26);
        }
        buf[BUF_SIZE-2] = '\r';
    }
    buf[BUF_SIZE-1] = '\n';

    printf("FILE_SIZE_MB = %d\r\n", FILE_SIZE_MB);
    printf("BUF_SIZE = %d bytes\r\n", BUF_SIZE);
    printf("Starting write test, please wait.\r\n\r\n");

    // do write test
    uint32_t n = FILE_SIZE/BUF_SIZE;
    printf("write speed and latency\r\n");
    printf("speed,max,min,avg\r\n");
    printf("KB/Sec,usec,usec,usec\r\n");
    for (uint8_t nTest = 0; nTest < WRITE_COUNT; nTest++) {
        fr = f_lseek(&fil, 0);
        if (fr != FR_OK) {
            printf("lseek error %d\r\n", fr);
            _error_blink(3);
        }
        fr = f_truncate(&fil);
        if (fr != FR_OK) {
            printf("truncate error %d\r\n", fr);
            _error_blink(4);
        }
        if (PRE_ALLOCATE) {
            fr = f_expand(&fil, FILE_SIZE, 0);
            if (fr != FR_OK) {
                printf("preallocate error %d\r\n", fr);
                _error_blink(5);
            }
        }
        maxLatency = 0;
        minLatency = 9999999;
        totalLatency = 0;
        skipLatency = SKIP_FIRST_LATENCY;
        t = to_ms_since_boot(get_absolute_time());
        for (uint32_t i = 0; i < n; i++) {
            uint32_t m = to_us_since_boot(get_absolute_time());
            fr = f_write(&fil, buf, BUF_SIZE, &bw);
            if (fr != FR_OK || bw != BUF_SIZE) {
                printf("write failed %d %d\r\n", fr, bw);
                _error_blink(6);
            }
            m = to_us_since_boot(get_absolute_time()) - m;
            totalLatency += m;
            if (skipLatency) {
                // Wait until first write to SD, not just a copy to the cache.
                skipLatency = f_tell(&fil) < 512;
            } else {
                if (maxLatency < m) {
                    maxLatency = m;
                }
                if (minLatency > m) {
                    minLatency = m;
                }
            }
            if (i % 10 == 0) _toggle_led();
        }
        fr = f_sync(&fil);
        if (fr != FR_OK) {
            printf("f_sync failed %d\r\n", fr);
            _error_blink(7);
        }
        t = to_ms_since_boot(get_absolute_time()) - t;
        s = f_size(&fil);
        printf("%7.4f, %d, %d, %d\r\n", s/t, maxLatency, minLatency, totalLatency/n);
    }

    printf("\r\n");
    printf("Starting read test, please wait.\r\n\r\n");
    printf("read speed and latency\r\n");
    printf("speed,max,min,avg\r\n");
    printf("KB/Sec,usec,usec,usec\r\n");

    // do read test
    for (uint8_t nTest = 0; nTest < READ_COUNT; nTest++) {
        fr = f_rewind(&fil);
        if (fr != FR_OK) {
            printf("rewind failed %d\r\n", fr);
            _error_blink(8);
        }
        maxLatency = 0;
        minLatency = 9999999;
        totalLatency = 0;
        skipLatency = SKIP_FIRST_LATENCY;
        t = to_ms_since_boot(get_absolute_time());
        for (uint32_t i = 0; i < n; i++) {
            buf[BUF_SIZE-1] = 0;
            uint32_t m = to_us_since_boot(get_absolute_time());
            fr = f_read(&fil, buf, BUF_SIZE, &br);
            if (fr != FR_OK || br != BUF_SIZE) {
                printf("read failed %d %d\r\n", fr, br);
                _error_blink(9);
            }
            m = to_us_since_boot(get_absolute_time()) - m;
            totalLatency += m;
            if (buf[BUF_SIZE-1] != '\n') {
                printf("data check error");
                _error_blink(10);
            }
            if (skipLatency) {
                skipLatency = false;
            } else {
                if (maxLatency < m) {
                    maxLatency = m;
                }
                if (minLatency > m) {
                    minLatency = m;
                }
            }
            if (i % 10 == 0) _toggle_led();
        }
        t = to_ms_since_boot(get_absolute_time()) - t;
        s = f_size(&fil);
        printf("%7.4f, %d, %d, %d\r\n", s/t, maxLatency, minLatency, totalLatency/n);
    }

    f_close(&fil);
    printf("\r\nDone\r\n");

    return 0;
}