#include "tf_card.h"

#include "ff.h"
#include "diskio.h"
#include "pio_spi.h"

#include "pico/stdlib.h"

/*--------------------------------------------------------------------------
   SPI and Pin selection
---------------------------------------------------------------------------*/
static pico_fatfs_spi_config_t _config = {
    spi0,
    CLK_SLOW_DEFAULT,
    CLK_FAST_DEFAULT,
    PIN_SPI0_MISO_DEFAULT,
    PIN_SPI0_CS_DEFAULT,
    PIN_SPI0_SCK_DEFAULT,
    PIN_SPI0_MOSI_DEFAULT,
    true
};

/*--------------------------------------------------------------------------

   Module Private Functions

---------------------------------------------------------------------------*/

/* MMC/SD command */
#define CMD0    (0)         /* GO_IDLE_STATE */
#define CMD1    (1)         /* SEND_OP_COND (MMC) */
#define ACMD41  (0x80+41)   /* SEND_OP_COND (SDC) */
#define CMD8    (8)         /* SEND_IF_COND */
#define CMD9    (9)         /* SEND_CSD */
#define CMD10   (10)        /* SEND_CID */
#define CMD12   (12)        /* STOP_TRANSMISSION */
#define ACMD13  (0x80+13)   /* SD_STATUS (SDC) */
#define CMD16   (16)        /* SET_BLOCKLEN */
#define CMD17   (17)        /* READ_SINGLE_BLOCK */
#define CMD18   (18)        /* READ_MULTIPLE_BLOCK */
#define CMD23   (23)        /* SET_BLOCK_COUNT (MMC) */
#define ACMD23  (0x80+23)   /* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24   (24)        /* WRITE_BLOCK */
#define CMD25   (25)        /* WRITE_MULTIPLE_BLOCK */
#define CMD32   (32)        /* ERASE_ER_BLK_START */
#define CMD33   (33)        /* ERASE_ER_BLK_END */
#define CMD38   (38)        /* ERASE */
#define CMD55   (55)        /* APP_CMD */
#define CMD58   (58)        /* READ_OCR */

/* MMC card type flags (MMC_GET_TYPE) */
#define CT_MMC         0x01            /* MMC ver 3 */
#define CT_SD1         0x02            /* SD ver 1 */
#define CT_SD2         0x04            /* SD ver 2 */
#define CT_SDC         (CT_SD1|CT_SD2) /* SD */
#define CT_BLOCK       0x08            /* Block addressing */

static volatile
DSTATUS Stat = STA_NOINIT;  /* Physical drive status */

static
BYTE CardType;          /* Card type flags */

/* SPI pin configurations */
#if PICO_RP2350A == 0
static uint _pin_miso_conf[2][6] = {
    { 0,  4, 16, 20, 32, 36},  // SPI0_RX
    { 8, 12, 24, 28, 40, 44}   // SPI1_RX
};
static uint _pin_sck_conf[2][6] = {
    { 2,  6, 18, 22, 34, 38},  // SPI0_SCK
    {10, 14, 26, 30, 42, 46}   // SPI1_SCK
};
static uint _pin_mosi_conf[2][6] = {
    { 3,  7, 19, 23, 35, 39},  // SPI0_TX
    {11, 15, 27, 31, 43, 47}   // SPI1_TX
};
#else
static uint _pin_miso_conf[2][4] = {
    { 0,  4, 16, 20},  // SPI0_RX
    { 8, 12, 24, 28}   // SPI1_RX
};
static uint _pin_sck_conf[2][4] = {
    { 2,  6, 18, 22},  // SPI0_SCK
    {10, 14, 26, 26}   // SPI1_SCK
};
static uint _pin_mosi_conf[2][4] = {
    { 3,  7, 19, 23},  // SPI0_TX
    {11, 15, 27, 27}   // SPI1_TX
};
#endif

/* SPI PIO inst (default) */
static pio_spi_inst_t _pio_spi = {
    .pio    = SPI_PIO_DEFAULT_PIO,
    .sm     = SPI_PIO_DEFAULT_SM,
    .cs_pin = 0
};
static io_rw_32* _reg_clkdiv = NULL;
static io_rw_32  _pio_clkdiv_slow = 4096 << 16;
static io_rw_32  _pio_clkdiv_fast =  256 << 16;
#define PIO_CLKDIV_LIMIT (0x00018000)  // fractional div x1.5 (6 system clock syscles per 1 SCK cycle)

static inline uint32_t _millis(void)
{
    return to_ms_since_boot(get_absolute_time());
}

/*-----------------------------------------------------------------------*/
/* SPI controls (Platform dependent)                                     */
/*-----------------------------------------------------------------------*/

static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);
    asm volatile("nop \n nop \n nop"); // FIXME
}

static void FCLK_SLOW(void)
{
    if (_config.spi_inst != NULL) {
        spi_set_baudrate(_config.spi_inst, _config.clk_slow);
    } else if (_reg_clkdiv != NULL) {
        *_reg_clkdiv = _pio_clkdiv_slow;
    }
}

static void FCLK_FAST(void)
{
    if (_config.spi_inst != NULL) {
        spi_set_baudrate(_config.spi_inst, _config.clk_fast);
    } else if (_reg_clkdiv != NULL) {
        *_reg_clkdiv = _pio_clkdiv_fast;
    }
}

static void CS_HIGH(void)
{
    cs_deselect(_config.pin_cs);
}

static void CS_LOW(void)
{
    cs_select(_config.pin_cs);
}

/* Initialize for SPI PIO */
static void pico_fatfs_init_spi_pio(void)
{
    gpio_set_dir(_config.pin_sck,  GPIO_OUT);
    gpio_set_dir(_config.pin_miso, GPIO_IN);
    gpio_set_dir(_config.pin_mosi, GPIO_OUT);
    gpio_set_dir(_config.pin_cs,   GPIO_OUT);

    /* chip _select invalid*/
    CS_HIGH();

    _pio_spi.cs_pin = _config.pin_cs;

    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);

    uint offset = pio_add_program(_pio_spi.pio, &spi_cpha0_program);
    pio_spi_init(_pio_spi.pio, _pio_spi.sm, offset,
        8,       // 8 bits per SPI frame
        (float) f_clk_sys / (_config.clk_slow / KHZ) / 4,  // 4 PIO input clock cycles to generate one SPI clock cycle
        false,   // CPHA = 0
        false,   // CPOL = 0
        _config.pin_sck,
        _config.pin_mosi,
        _config.pin_miso
    );
    // Get CLKDIV register and slow/fast settings
    _reg_clkdiv      = &(_pio_spi.pio->sm[_pio_spi.sm].clkdiv);
    _pio_clkdiv_slow = *_reg_clkdiv;
    _pio_clkdiv_fast = (io_rw_32) ((float) _pio_clkdiv_slow * _config.clk_slow / _config.clk_fast);
    _pio_clkdiv_fast &= 0xffffff00;
    if (_pio_clkdiv_fast < PIO_CLKDIV_LIMIT) {
        _pio_clkdiv_fast = PIO_CLKDIV_LIMIT;
    }

    _config.clk_fast = (uint64_t) f_clk_sys * 1000 * 256 / (_pio_clkdiv_fast / 256)  / 4;
    _config.clk_slow = (uint64_t) f_clk_sys * 1000 * 256 / (_pio_clkdiv_slow / 256)  / 4;
}

/* Initialize SPI */
void pico_fatfs_init_spi(void)
{
    /* GPIO pin configuration */
    /* pull up of MISO is MUST (internal pull up or 10Kohm external pull up is recommended) */
    /* Set drive strength and slew rate if needed to meet wire condition */

    gpio_init(_config.pin_sck);
    gpio_disable_pulls(_config.pin_sck);
    //gpio_set_drive_strength(_config.pin_sck, PADS_BANK0_GPIO0_DRIVE_VALUE_4MA); // 2mA, 4mA (default), 8mA, 12mA
    //gpio_set_slew_rate(_config.pin_sck, 0); // 0: SLOW (default), 1: FAST

    gpio_init(_config.pin_miso);
    if (_config.pullup) {
        gpio_pull_up(_config.pin_miso);
    } else {
        gpio_disable_pulls(_config.pin_miso);
    }
    //gpio_set_schmitt(_config.pin_miso, 1); // 0: Off, 1: On (default)

    gpio_init(_config.pin_mosi);
    if (_config.pullup) {
        gpio_pull_up(_config.pin_mosi);
    } else {
        gpio_disable_pulls(_config.pin_mosi);
    }
    //gpio_set_drive_strength(_config.pin_mosi, PADS_BANK0_GPIO0_DRIVE_VALUE_4MA); // 2mA, 4mA (default), 8mA, 12mA
    //gpio_set_slew_rate(_config.pin_mosi, 0); // 0: SLOW (default), 1: FAST

    gpio_init(_config.pin_cs);
    gpio_disable_pulls(_config.pin_cs);
    //gpio_set_drive_strength(_config.pin_cs, PADS_BANK0_GPIO0_DRIVE_VALUE_4MA); // 2mA, 4mA (default), 8mA, 12mA
    //gpio_set_slew_rate(_config.pin_cs, 0); // 0: SLOW (default), 1: FAST

    // Jump to PIO SPI if spi_inst is NULL
    if (_config.spi_inst == NULL) {
        pico_fatfs_init_spi_pio();
        return;
    }

    gpio_set_function(_config.pin_sck,  GPIO_FUNC_SPI);
    gpio_set_function(_config.pin_miso, GPIO_FUNC_SPI);
    gpio_set_function(_config.pin_mosi, GPIO_FUNC_SPI);
    gpio_set_dir(_config.pin_cs, GPIO_OUT);

    /* chip _select invalid*/
    CS_HIGH();

    spi_init(_config.spi_inst, _config.clk_slow);

    /* SPI parameter config */
    spi_set_format(_config.spi_inst,
        8, /* data_bits */
        SPI_CPOL_0, /* cpol */
        SPI_CPHA_0, /* cpha */
        SPI_MSB_FIRST /* order */
    );

    FCLK_FAST();
    _config.clk_fast = spi_get_baudrate(_config.spi_inst);
    FCLK_SLOW();
    _config.clk_slow = spi_get_baudrate(_config.spi_inst);
}

/* Exchange a byte */
static
BYTE xchg_spi (
    BYTE dat    /* Data to send */
)
{
    uint8_t* buff = (uint8_t *) &dat;
    if (_config.spi_inst != NULL) {
        spi_write_read_blocking(_config.spi_inst, buff, buff, 1);
    } else {
        pio_spi_write8_read8_blocking(&_pio_spi, buff, buff, 1);
    }
    return (BYTE) *buff;
}


/* Receive multiple byte */
static
void rcvr_spi_multi (
    BYTE* buff,     /* Pointer to data buffer */
    UINT btr        /* Number of bytes to receive (even number) */
)
{
    uint8_t* b = (uint8_t *) buff;
    if (_config.spi_inst != NULL) {
        spi_read_blocking(_config.spi_inst, 0xff, b, btr);
    } else {
        uint8_t src[btr];
        for (int i = 0; i < btr; i++) { src[i] = 0xff; }
        pio_spi_write8_read8_blocking(&_pio_spi, src, b, btr);
    }
}


/*-----------------------------------------------------------------------*/
/* Wait for card ready                                                   */
/*-----------------------------------------------------------------------*/

static
int wait_ready (    /* 1:Ready, 0:Timeout */
    UINT wt         /* Timeout [ms] */
)
{
    BYTE d;

    uint32_t t = _millis();
    do {
        d = xchg_spi(0xFF);
        /* This loop takes a time. Insert rot_rdq() here for multitask envilonment. */
    } while (d != 0xFF && _millis() < t + wt);  /* Wait for card goes ready or timeout */

    return (d == 0xFF) ? 1 : 0;
}



/*-----------------------------------------------------------------------*/
/* Deselect card and release SPI                                         */
/*-----------------------------------------------------------------------*/

static
void deselect (void)
{
    CS_HIGH();      /* Set CS# high */
    xchg_spi(0xFF); /* Dummy clock (force DO hi-z for multiple slave SPI) */
}



/*-----------------------------------------------------------------------*/
/* Select card and wait for ready                                        */
/*-----------------------------------------------------------------------*/

static
int _select (void)  /* 1:OK, 0:Timeout */
{
    CS_LOW();       /* Set CS# low */
    xchg_spi(0xFF); /* Dummy clock (force DO enabled) */
    if (wait_ready(500)) return 1;  /* Wait for card ready */

    deselect();
    return 0;   /* Timeout */
}



/*-----------------------------------------------------------------------*/
/* Receive a data packet from the MMC                                    */
/*-----------------------------------------------------------------------*/

static
int rcvr_datablock (    /* 1:OK, 0:Error */
    BYTE* buff,         /* Data buffer */
    UINT btr            /* Data block length (byte) */
)
{
    BYTE token;

    const uint32_t timeout = 200;
    uint32_t t = _millis();
    do {                            /* Wait for DataStart token in timeout of 200ms */
        token = xchg_spi(0xFF);
        /* This loop will take a time. Insert rot_rdq() here for multitask envilonment. */
    } while (token == 0xFF && _millis() < t + timeout);
    if(token != 0xFE) return 0;     /* Function fails if invalid DataStart token or timeout */

    rcvr_spi_multi(buff, btr);      /* Store trailing data to the buffer */
    xchg_spi(0xFF); xchg_spi(0xFF);         /* Discard CRC */

    return 1;                       /* Function succeeded */
}


/*-----------------------------------------------------------------------*/
/* Send a command packet to the MMC                                      */
/*-----------------------------------------------------------------------*/

static
BYTE send_cmd (     /* Return value: R1 resp (bit7==1:Failed to send) */
    BYTE cmd,       /* Command index */
    DWORD arg       /* Argument */
)
{
    BYTE n, res;


    if (cmd & 0x80) {   /* Send a CMD55 prior to ACMD<n> */
        cmd &= 0x7F;
        res = send_cmd(CMD55, 0);
        if (res > 1) return res;
    }

    /* Select the card and wait for ready except to stop multiple block read */
    if (cmd != CMD12) {
        deselect();
        if (!_select()) return 0xFF;
    }

    /* Send command packet */
    xchg_spi(0x40 | cmd);               /* Start + command index */
    xchg_spi((BYTE)(arg >> 24));        /* Argument[31..24] */
    xchg_spi((BYTE)(arg >> 16));        /* Argument[23..16] */
    xchg_spi((BYTE)(arg >> 8));         /* Argument[15..8] */
    xchg_spi((BYTE)arg);                /* Argument[7..0] */
    n = 0x01;                           /* Dummy CRC + Stop */
    if (cmd == CMD0) n = 0x95;          /* Valid CRC for CMD0(0) */
    if (cmd == CMD8) n = 0x87;          /* Valid CRC for CMD8(0x1AA) */
    xchg_spi(n);

    /* Receive command resp */
    if (cmd == CMD12) xchg_spi(0xFF);   /* Diacard following one byte when CMD12 */
    n = 10;                             /* Wait for response (10 bytes max) */
    do {
        res = xchg_spi(0xFF);
    } while ((res & 0x80) && --n);

    return res;                         /* Return received response */
}

/*--------------------------------------------------------------------------

   Public Functions

---------------------------------------------------------------------------*/


/*-----------------------------------------------------------------------*/
/* Initialize disk drive                                                 */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
    BYTE drv        /* Physical drive number (0) */
)
{
    BYTE n, cmd, ty, ocr[4];
    const uint32_t timeout = 1000; /* Initialization timeout = 1 sec */
    uint32_t t;


    if (drv) return STA_NOINIT;         /* Supports only drive 0 */
    pico_fatfs_init_spi();              /* Initialize SPI */
    sleep_ms(10);

    if (Stat & STA_NODISK) return Stat; /* Is card existing in the soket? */

    FCLK_SLOW();
    for (n = 10; n; n--) xchg_spi(0xFF);    /* Send 80 dummy clocks */

    ty = 0;
    if (send_cmd(CMD0, 0) == 1) {           /* Put the card SPI/Idle state */
        t = _millis();
        if (send_cmd(CMD8, 0x1AA) == 1) {   /* SDv2? */
            for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);    /* Get 32 bit return value of R7 resp */
            if (ocr[2] == 0x01 && ocr[3] == 0xAA) {             /* Is the card supports vcc of 2.7-3.6V? */
                while (_millis() < t + timeout && send_cmd(ACMD41, 1UL << 30)) ;    /* Wait for end of initialization with ACMD41(HCS) */
                if (_millis() < t + timeout && send_cmd(CMD58, 0) == 0) {       /* Check CCS bit in the OCR */
                    for (n = 0; n < 4; n++) ocr[n] = xchg_spi(0xFF);
                    ty = (ocr[0] & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;  /* Card id SDv2 */
                }
            }
        } else {    /* Not SDv2 card */
            if (send_cmd(ACMD41, 0) <= 1)   {   /* SDv1 or MMC? */
                ty = CT_SD1; cmd = ACMD41;  /* SDv1 (ACMD41(0)) */
            } else {
                ty = CT_MMC; cmd = CMD1;    /* MMCv3 (CMD1(0)) */
            }
            while (_millis() < t + timeout && send_cmd(cmd, 0)) ;       /* Wait for end of initialization */
            if (_millis() >= t + timeout || send_cmd(CMD16, 512) != 0)  /* Set block length: 512 */
                ty = 0;
        }
    }
    CardType = ty;  /* Card type */
    deselect();

    if (ty) {           /* OK */
        FCLK_FAST();            /* Set fast clock */
        Stat &= ~STA_NOINIT;    /* Clear STA_NOINIT flag */
    } else {            /* Failed */
        Stat = STA_NOINIT;
    }

    return Stat;
}



/*-----------------------------------------------------------------------*/
/* Get disk status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
    BYTE drv        /* Physical drive number (0) */
)
{
    if (drv) return STA_NOINIT;     /* Supports only drive 0 */

    return Stat;    /* Return disk status */
}



/*-----------------------------------------------------------------------*/
/* Read sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
    BYTE drv,       /* Physical drive number (0) */
    BYTE* buff,     /* Pointer to the data buffer to store read data */
    LBA_t sector,   /* Start sector number (LBA) */
    UINT count      /* Number of sectors to read (1..128) */
)
{
    if (drv || !count) return RES_PARERR;       /* Check parameter */
    if (Stat & STA_NOINIT) return RES_NOTRDY;   /* Check if drive is ready */

    if (!(CardType & CT_BLOCK)) sector *= 512;  /* LBA ot BA conversion (byte addressing cards) */

    if (count == 1) {   /* Single sector read */
        if ((send_cmd(CMD17, sector) == 0)  /* READ_SINGLE_BLOCK */
            && rcvr_datablock(buff, 512)) {
            count = 0;
        }
    }
    else {              /* Multiple sector read */
        if (send_cmd(CMD18, sector) == 0) { /* READ_MULTIPLE_BLOCK */
            do {
                if (!rcvr_datablock(buff, 512)) break;
                buff += 512;
            } while (--count);
            send_cmd(CMD12, 0);             /* STOP_TRANSMISSION */
        }
    }
    deselect();

    return count ? RES_ERROR : RES_OK;  /* Return result */
}



#if !FF_FS_READONLY && !FF_FS_NORTC
/* get the current time */
__attribute__((weak))
DWORD get_fattime (void)
{
    return 0;
}
#endif

#if FF_FS_READONLY == 0
/* Transmit multiple byte */
static
void xmit_spi_multi (
    const BYTE* buff,       /* Pointer to data buffer */
    UINT btx        /* Number of bytes to transmit (even number) */
)
{
    const uint8_t* b = (const uint8_t *) buff;
    if (_config.spi_inst != NULL) {
        spi_write_blocking(_config.spi_inst, b, btx);
    } else {
        pio_spi_write8_blocking(&_pio_spi, b, btx);
    }
}

/*-----------------------------------------------------------------------*/
/* Transmit a data packet to the MMC                                     */
/*-----------------------------------------------------------------------*/

static
int xmit_datablock (    /* 1:OK, 0:Error */
    const BYTE* buff, /* 512 byte data block to be transmitted */
    BYTE token /* Data/Stop token */
)
{
    BYTE resp;
    if (!wait_ready(500)) return 0;
    xchg_spi(token); /* Xmit data token */
    if (token != 0xFD) { /* Is data token */
        xmit_spi_multi(buff, 512); /* Xmit the data block to the MMC */
        xchg_spi(0xFF); /* CRC (Dummy) */
        xchg_spi(0xFF);
        resp = xchg_spi(0xFF); /* Reveive data response */
        if ((resp & 0x1F) != 0x05) /* If not accepted, return with error */
            return 0;
    }
    return 1;
}

/*-----------------------------------------------------------------------*/
/* Write sector(s)                                                       */
/*-----------------------------------------------------------------------*/

DRESULT disk_write (
    BYTE drv,           /* Physical drive number (0) */
    const BYTE* buff,   /* Ponter to the data to write */
    LBA_t sector,       /* Start sector number (LBA) */
    UINT count          /* Number of sectors to write (1..128) */
)
{
    if (drv || !count) return RES_PARERR;       /* Check parameter */
    if (Stat & STA_NOINIT) return RES_NOTRDY;   /* Check drive status */
    if (Stat & STA_PROTECT) return RES_WRPRT;   /* Check write protect */

    if (!(CardType & CT_BLOCK)) sector *= 512;  /* LBA ==> BA conversion (byte addressing cards) */

    if (!_select()) return RES_NOTRDY;

    if (count == 1) {   /* Single sector write */
        if ((send_cmd(CMD24, sector) == 0)  /* WRITE_BLOCK */
            && xmit_datablock(buff, 0xFE)) {
            count = 0;
        }
    }
    else {              /* Multiple sector write */
        if (CardType & CT_SDC) send_cmd(ACMD23, count); /* Predefine number of sectors */
        if (send_cmd(CMD25, sector) == 0) { /* WRITE_MULTIPLE_BLOCK */
            do {
                if (!xmit_datablock(buff, 0xFC)) break;
                buff += 512;
            } while (--count);
            if (!xmit_datablock(0, 0xFD)) count = 1;    /* STOP_TRAN token */
        }
    }
    deselect();

    return count ? RES_ERROR : RES_OK;  /* Return result */
}
#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous drive controls other than data read/write               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
    BYTE drv,       /* Physical drive number (0) */
    BYTE cmd,       /* Control command code */
    void* buff      /* Pointer to the conrtol data */
)
{
    DRESULT res;
    BYTE n, csd[16];
    DWORD* dp, st, ed, csize;


    if (drv) return RES_PARERR;                 /* Check parameter */
    if (Stat & STA_NOINIT) return RES_NOTRDY;   /* Check if drive is ready */

    res = RES_ERROR;

    switch (cmd) {
    case CTRL_SYNC :        /* Wait for end of internal write process of the drive */
        if (_select()) res = RES_OK;
        break;

    case GET_SECTOR_COUNT : /* Get drive capacity in unit of sector (DWORD) */
        if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
            if ((csd[0] >> 6) == 1) {   /* SDC ver 2.00 */
                csize = csd[9] + ((WORD)csd[8] << 8) + ((DWORD)(csd[7] & 63) << 16) + 1;
                *(DWORD*)buff = csize << 10;
            } else {                    /* SDC ver 1.XX or MMC ver 3 */
                n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
                csize = (csd[8] >> 6) + ((WORD)csd[7] << 2) + ((WORD)(csd[6] & 3) << 10) + 1;
                *(DWORD*)buff = csize << (n - 9);
            }
            res = RES_OK;
        }
        break;

    case GET_BLOCK_SIZE :   /* Get erase block size in unit of sector (DWORD) */
        if (CardType & CT_SD2) {    /* SDC ver 2.00 */
            if (send_cmd(ACMD13, 0) == 0) { /* Read SD status */
                xchg_spi(0xFF);
                if (rcvr_datablock(csd, 16)) {              /* Read partial block */
                    for (n = 64 - 16; n; n--) xchg_spi(0xFF);   /* Purge trailing data */
                    *(DWORD*)buff = 16UL << (csd[10] >> 4);
                    res = RES_OK;
                }
            }
        } else {                    /* SDC ver 1.XX or MMC */
            if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {  /* Read CSD */
                if (CardType & CT_SD1) {    /* SDC ver 1.XX */
                    *(DWORD*)buff = (((csd[10] & 63) << 1) + ((WORD)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
                } else {                    /* MMC */
                    *(DWORD*)buff = ((WORD)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
                }
                res = RES_OK;
            }
        }
        break;

    case CTRL_TRIM :    /* Erase a block of sectors (used when _USE_ERASE == 1) */
        if (!(CardType & CT_SDC)) break;                /* Check if the card is SDC */
        if (disk_ioctl(drv, MMC_GET_CSD, csd)) break;   /* Get CSD */
        if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break; /* Check if sector erase can be applied to the card */
        dp = buff; st = dp[0]; ed = dp[1];              /* Load sector block */
        if (!(CardType & CT_BLOCK)) {
            st *= 512; ed *= 512;
        }
        if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) { /* Erase sector block */
            res = RES_OK;   /* FatFs does not check result of this command */
        }
        break;

    default:
        res = RES_PARERR;
    }

    deselect();

    return res;
}

bool pico_fatfs_set_config(pico_fatfs_spi_config_t* config)
{
    _config = *config;

    if (_config.spi_inst == NULL) {
        return false;
    } else if (_config.spi_inst != spi0 && _config.spi_inst != spi1) {
        _config.spi_inst = NULL;
        return false;
    }

    // SPI function pin assignment check
    bool miso_ok = false;
    bool sck_ok  = false;
    bool mosi_ok = false;
    int spi_id = (_config.spi_inst == spi0) ? 0 : 1;

    for (int i = 0; i < sizeof(_pin_miso_conf[spi_id])/sizeof(_pin_miso_conf[spi_id][0]); i++) {
        if (_config.pin_miso == _pin_miso_conf[spi_id][i]) {
            miso_ok = true;
            break;
        }
    }
    for (int i = 0; i < sizeof(_pin_sck_conf[spi_id])/sizeof(_pin_sck_conf[spi_id][0]); i++) {
        if (_config.pin_sck == _pin_sck_conf[spi_id][i]) {
            sck_ok = true;
            break;
        }
    }
    for (int i = 0; i < sizeof(_pin_mosi_conf[spi_id])/sizeof(_pin_mosi_conf[spi_id][0]); i++) {
        if (_config.pin_mosi == _pin_mosi_conf[spi_id][i]) {
            mosi_ok = true;
            break;
        }
    }
    if (!miso_ok || !sck_ok || !mosi_ok) {
        _config.spi_inst = NULL;
        return false;
    }
    return true;
}

void pico_fatfs_config_spi_pio(PIO pio, uint sm)
{
    _pio_spi.pio = pio;
    _pio_spi.sm  = sm;
}

int pico_fatfs_reboot_spi(void)
{
    // Send 32 cycles of CS low
    FCLK_SLOW();
    CS_HIGH();
    sleep_ms(10);
    CS_LOW();
    for (int i = 0; i < 4; i++) {
        xchg_spi(0xFF); /* Dummy clock (force DO enabled) */
    }
    CS_HIGH();
    sleep_ms(10);

    return _select();
}

uint pico_fatfs_get_clk_slow_freq(void)
{
    return _config.clk_slow;
}

uint pico_fatfs_get_clk_fast_freq(void)
{
    return _config.clk_fast;
}
