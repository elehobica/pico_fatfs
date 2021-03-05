#include <cstdint>
#include <cstdio>

#include "pico/stdlib.h"
#include "fatfs/tf_card.h"

unsigned char image[12800];
FATFS fs;

int main() {
    stdio_init_all();
    const uint32_t LED_PIN = 25;
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    printf("Hello, world!\n");
    {
        uint8_t mount_is_ok = 1; /* 0: mount successful ; 1: mount failed */
        int offset = 0;
        FIL fil;
        FRESULT fr;     /* FatFs return code */
        UINT br;

        fr = f_mount(&fs, "", 1);
        if (fr == 0) {
            mount_is_ok = 0;
            printf("mount ok\n");
        } else {
            mount_is_ok = 1;
            printf("mount failed\n");
        }

        if (mount_is_ok == 0)
        {
            //while(1)
            {
                offset = 0;
                fr = f_open(&fil, "logo.bin", FA_READ);
                if (fr) printf("open error: %d!\n\r", (int)fr);
                f_lseek(&fil, offset);
                fr = f_read(&fil, image, sizeof(image), &br);
                offset += 12800;
                f_lseek(&fil, offset);
                fr = f_read(&fil, image, sizeof(image), &br);
            }
        }
    }

    while (true) {
        gpio_put(LED_PIN, 1);
        sleep_ms(250);
        gpio_put(LED_PIN, 0);
        sleep_ms(250);
    }

    return 0;
}