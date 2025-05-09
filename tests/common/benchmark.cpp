#include "benchmark.h"

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

using namespace std;

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

    cout << endl;

    // Pico / Pico W dependencies
    _picoW = _check_pico_w();
    if (_picoW) {
        if (cyw43_arch_init()) {  // this is needed for driving LED
            cout << "cyw43 init failed" << endl;
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
    cout << "Type any character to start (*: Auto select from SPI / SPI PIO, p: SPI PIO)" << endl;
    while (!uart_is_readable_within_us(uart0, 1000)) {};

    int chr;
    while ((chr = getchar_timeout_us(0)) == PICO_ERROR_TIMEOUT) {}

    if (chr == 'p') {
        _config.spi_inst = NULL;
    }

    cout << "=====================" << endl;
    cout << "== pico_fatfs_test ==" << endl;
    cout << "=====================" << endl;

    if (_picoW) {
        cout << PICOW_STR << endl;
    } else {
        cout << PICO_STR << endl;
    }

    bool spi_configured = pico_fatfs_set_config(&_config);
    if (spi_configured) {
        cout << "SPI configured" << endl;
    } else {
        // modify if customized configuration for SPI PIO is needed
        pico_fatfs_config_spi_pio(SPI_PIO_DEFAULT_PIO, SPI_PIO_DEFAULT_SM);
        cout << "SPI PIO configured" << endl;
    }
    cout << "Configured clk_slow: " << fixed << setw(6) << setprecision(2) 
         << pico_fatfs_get_clk_slow_freq() / 1e3 << " KHz, clk_fast: " << setw(5) << setprecision(2) 
         << pico_fatfs_get_clk_fast_freq() / 1e6 << " MHz" << endl;


    for (int i = 0; i < 5; i++) {
        fr = f_mount(&fs, "", 1);
        if (fr == FR_OK) { break; }
        cout << "mount error " << fr << " -> retry " << i << endl;
        pico_fatfs_reboot_spi();
    }
    if (fr != FR_OK) {
        cout << "mount error " << fr << endl;
        _error_blink(1);
    }
    cout << "Operation  clk_slow: " << fixed << setw(6) << setprecision(2) 
         << pico_fatfs_get_clk_slow_freq() / 1e3 << " KHz, clk_fast: " << setw(5) << setprecision(2) 
         << pico_fatfs_get_clk_fast_freq() / 1e6 << " MHz" << endl;
    cout << "mount ok" << endl;

    switch (fs.fs_type) {
        case FS_FAT12:
            cout << "Type is FAT12" << endl;
            break;
        case FS_FAT16:
            cout << "Type is FAT16" << endl;
            break;
        case FS_FAT32:
            cout << "Type is FAT32" << endl;
            break;
        case FS_EXFAT:
            cout << "Type is EXFAT" << endl;
            break;
        default:
            cout << "Type is unknown" << endl;
            break;
    }
    cout << "Card size: " << fixed << setw(7) << setprecision(2) 
         << fs.csize * fs.n_fatent * 512E-9 << " GB (GB = 1E9 bytes)" << endl << endl;

#if 0
    // Print CID
    BYTE cid[16];
    disk_ioctl(0, MMC_GET_CID, cid);
    cout << "Manufacturer ID: " << hex << setw(2) << setfill('0') << (int) cid[0] << endl;
    cout << "OEM ID: " << hex << setw(2) << setfill('0') << (int) cid[1] 
         << setw(2) << setfill('0') << (int) cid[2] << endl;
    cout << "Product: " << cid[3] << cid[4] << cid[5] << cid[6] << cid[7] << endl;
    cout << dec << "Version: " << (int) (cid[8] >> 4) & 0xf << "." << (int) cid[8] & 0xf << endl;
    cout << "Serial number: " << hex << setw(2) << setfill('0') << (int) cid[9]
         << setw(2) << setfill('0') << (int) cid[10]
         << setw(2) << setfill('0') << (int) cid[11]
         << setw(2) << setfill('0') << (int) cid[12] << endl;
    cout << dec << "Manufacturing date : " << (int) cid[14] & 0xf << "/"
         << ((int) cid[13] & 0xf)*16 + ((int) (cid[14] >> 2) & 0xf) + 2000 << endl << endl;
#endif

    fr = f_open(&fil, "bench.dat", FA_READ | FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        cout << "open error " << fr << endl;
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

    cout << "FILE_SIZE_MB = " << FILE_SIZE_MB << endl;
    cout << "BUF_SIZE = " << BUF_SIZE << " bytes" << endl;
    cout << "Starting write test, please wait." << endl << endl;

    // do write test
    uint32_t n = FILE_SIZE/BUF_SIZE;
    cout << "write speed and latency" << endl;
    cout << "speed,max,min,avg" << endl;
    cout << "KB/Sec,usec,usec,usec" << endl;
    for (uint8_t nTest = 0; nTest < WRITE_COUNT; nTest++) {
        fr = f_lseek(&fil, 0);
        if (fr != FR_OK) {
            cout << "lseek error " << fr << endl;
            _error_blink(3);
        }
        fr = f_truncate(&fil);
        if (fr != FR_OK) {
            cout << "truncate error " << fr << endl;
            _error_blink(4);
        }
        if (PRE_ALLOCATE) {
            fr = f_expand(&fil, FILE_SIZE, 0);
            if (fr != FR_OK) {
                cout << "preallocate error " << fr << endl;
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
                cout << "write failed " << fr << " " << bw << endl;
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
            cout << "f_sync failed " << fr << endl;
            _error_blink(7);
        }
        t = to_ms_since_boot(get_absolute_time()) - t;
        s = f_size(&fil);
        cout << fixed << setprecision(4) << setw(7) << s/t << ", " << maxLatency << ", " << minLatency << ", " << totalLatency/n << endl;
    }

    cout << endl;
    cout << "Starting read test, please wait." << endl << endl;
    cout << "read speed and latency" << endl;
    cout << "speed,max,min,avg" << endl;
    cout << "KB/Sec,usec,usec,usec" << endl;

    // do read test
    for (uint8_t nTest = 0; nTest < READ_COUNT; nTest++) {
        fr = f_rewind(&fil);
        if (fr != FR_OK) {
            cout << "rewind failed " << fr << endl;
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
                cout << "read failed " << fr << " " << br << endl;
                _error_blink(9);
            }
            m = to_us_since_boot(get_absolute_time()) - m;
            totalLatency += m;
            if (buf[BUF_SIZE-1] != '\n') {
                cout << "data check error";
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
        cout << fixed << setprecision(4) << setw(7) << s/t << ", " << maxLatency << ", " << minLatency << ", " << totalLatency/n << endl;
    }

    cout << endl << "Done" << endl;

    cout << "Save log to file? (y/n): ";
    
    int response;
    while (true) {
        while (!uart_is_readable(uart0)) {
            sleep_ms(10);
        }
        response = uart_getc(uart0);
        
        cout << (char)response;
        
        if (response == 'y' || response == 'Y' || response == 'n' || response == 'N') {
            cout << endl;
            break;
        }
    }
    
    if (response == 'y' || response == 'Y') {
        string filename = spi_configured ? "benchmark_SPI.log" : "benchmark_SPI_PIO.log";
        
        cout << "Saving log to " << filename << "..." << endl;
        
        FIL log_file;
        fr = f_open(&log_file, filename.c_str(), FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) {
            cout << "Error opening log file: " << fr << endl;
        } else {
            string header = "=== Benchmark Results ===\n";
            header += spi_configured ? "SPI Mode\n" : "SPI PIO Mode\n";
            header += "FILE_SIZE_MB = " + to_string(FILE_SIZE_MB) + "\n";
            header += "BUF_SIZE = " + to_string(BUF_SIZE) + " bytes\n\n";
            
            UINT bw;
            fr = f_write(&log_file, header.c_str(), header.length(), &bw);
            
            string write_results = "Write speed and latency\n";
            write_results += "speed,max,min,avg\n";
            write_results += "KB/Sec,usec,usec,usec\n";
            
            fr = f_write(&log_file, write_results.c_str(), write_results.length(), &bw);
            
            string read_results = "\nRead speed and latency\n";
            read_results += "speed,max,min,avg\n";
            read_results += "KB/Sec,usec,usec,usec\n";
            
            fr = f_write(&log_file, read_results.c_str(), read_results.length(), &bw);
            
            f_close(&log_file);
            cout << "Log saved successfully." << endl;
        }
    }
    
    sleep_ms(250);
    f_close(&fil);

    return 0;
}
