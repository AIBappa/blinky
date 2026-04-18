#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* 1. Hardware Definitions */
#define SPI_NODE DT_NODELABEL(ads1220)

// SPI Config: Mode 1 (CPHA=1, CPOL=0), 8-bit words
static const struct spi_dt_spec ads_spi = SPI_DT_SPEC_GET(SPI_NODE, 
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPHA, 0);

// DRDY Config
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec drdy_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, drdy_gpios);
static struct gpio_callback drdy_cb_data;
static K_SEM_DEFINE(drdy_sem, 0, 1);

void drdy_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    k_sem_give(&drdy_sem);
}

/* 2. ADS1220 Commands */
#define ADS1220_CMD_RESET     0x06  // Reset command
#define ADS1220_CMD_START     0x08  // Start/Sync command
#define ADS1220_CMD_RDATA     0x10  // Read Data command
#define ADS1220_CMD_RREG      0x20  // Read Register command base (0x20 + (num_bytes-1)<<2)
#define ADS1220_CMD_WREG_REG1 0x44  // Write 1 register starting at Reg 1
#define ADS1220_REG1_TEMP_ON  0x02  // Bit 1 = 1 (Enables Temp Sensor)

int main(void) {
    int err;
    /* Give the USB CDC ACM terminal time to connect */
    k_msleep(2500); 

    printk("\n--- ADS1220 Temperature Checker (Zephyr) ---\n");

    if (!device_is_ready(ads_spi.bus)) {
        printk("Error: SPI bus device not ready.\n");
        return 0;
    }

    if (!gpio_is_ready_dt(&drdy_pin)) {
        printk("Error: DRDY pin not ready.\n");
        return 0;
    }

    err = gpio_pin_configure_dt(&drdy_pin, GPIO_INPUT);
    if (err < 0) {
        printk("Error configuring DRDY pin: %d\n", err);
        return 0;
    }

    err = gpio_pin_interrupt_configure_dt(&drdy_pin, GPIO_INT_EDGE_TO_ACTIVE);
    if (err < 0) {
        printk("Error configuring DRDY interrupt: %d\n", err);
        return 0;
    }

    gpio_init_callback(&drdy_cb_data, drdy_isr, BIT(drdy_pin.pin));
    gpio_add_callback(drdy_pin.port, &drdy_cb_data);

    /* 3. Initialization & Diagnostics */
    printk("Resetting ADS1220...\n");
    uint8_t reset_cmd = ADS1220_CMD_RESET;
    struct spi_buf tx_reset_buf = { .buf = &reset_cmd, .len = 1 };
    struct spi_buf_set tx_reset_set = { .buffers = &tx_reset_buf, .count = 1 };
    err = spi_write_dt(&ads_spi, &tx_reset_set);
    if (err) {
        printk("SPI Write Error (Reset): %d\n", err);
    }
    k_msleep(50); // Wait for reset to complete

    printk("Reading ADS1220 Registers to verify SPI connection...\n");
    // To read registers, we send the RREG command byte, followed by 4 dummy bytes to clock the data out.
    uint8_t tx_rreg[5] = { ADS1220_CMD_RREG | 0x03, 0x00, 0x00, 0x00, 0x00 };
    uint8_t rx_rreg[5] = { 0 };
    
    struct spi_buf tx_rreg_buf = { .buf = tx_rreg, .len = 5 };
    struct spi_buf_set tx_rreg_set = { .buffers = &tx_rreg_buf, .count = 1 };
    struct spi_buf rx_rreg_buf = { .buf = rx_rreg, .len = 5 };
    struct spi_buf_set rx_rreg_set = { .buffers = &rx_rreg_buf, .count = 1 };
    
    // We use a single transceive call so CS stays asserted
    err = spi_transceive_dt(&ads_spi, &tx_rreg_set, &rx_rreg_set);
    if (err) {
        printk("SPI Transceive Error (RREG): %d\n", err);
    }

    printk("Registers [0-3]: 0x%02X 0x%02X 0x%02X 0x%02X\n", 
           rx_rreg[1], rx_rreg[2], rx_rreg[3], rx_rreg[4]);
           
    if (rx_rreg[1] == 0x00 || rx_rreg[1] == 0xFF) {
        printk("WARNING: Registers read as 0x00 or 0xFF. SPI wiring might be wrong or board is unpowered!\n");
    }

    printk("Enabling Temperature Mode...\n");
    // Command 0x40 is WREG starting at register 0.
    // 0x44 is WREG starting at register 1, writing 1 register (0x40 | (1 << 2) | (0)).
    // Let's write configuration register 1 to enable temperature sensor (bit 1) and continuous conversion mode (bit 2)
    // ADS1220_REG1_TEMP_ON | 0x04 = 0x02 | 0x04 = 0x06
    uint8_t init_tx[] = { ADS1220_CMD_WREG_REG1, ADS1220_REG1_TEMP_ON | 0x04 };
    struct spi_buf tx_buf = { .buf = init_tx, .len = sizeof(init_tx) };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

    err = spi_write_dt(&ads_spi, &tx_set);
    if (err) {
        printk("SPI Write Error (WREG): %d\n", err);
        return 0;
    }
    printk("ADS1220 configured for Temperature Mode (Continuous).\n");

    /* 4. Main Sampling Loop */
    while (1) {
        uint8_t start_cmd = ADS1220_CMD_START;
        struct spi_buf tx_start_buf = { .buf = &start_cmd, .len = 1 };
        struct spi_buf_set tx_start_set = { .buffers = &tx_start_buf, .count = 1 };
        err = spi_write_dt(&ads_spi, &tx_start_set);
        if (err) {
            printk("SPI Write Error (Start): %d\n", err);
        }

        if (k_sem_take(&drdy_sem, K_MSEC(500)) == 0) {
            // ✅ Explicit 4-byte transaction: cmd + 3 data bytes
            uint8_t tx_rdata[4] = { ADS1220_CMD_RDATA, 0x00, 0x00, 0x00 };
            uint8_t rx_rdata[4] = { 0 };

            struct spi_buf tx_rdata_buf = { .buf = tx_rdata, .len = 4 };
            struct spi_buf_set tx_rdata_set = { .buffers = &tx_rdata_buf, .count = 1 };
            struct spi_buf rx_rdata_buf = { .buf = rx_rdata, .len = 4 };
            struct spi_buf_set rx_rdata_set = { .buffers = &rx_rdata_buf, .count = 1 };

            err = spi_transceive_dt(&ads_spi, &tx_rdata_set, &rx_rdata_set);

            if (err == 0) {
                // rx_rdata[0] is the byte received during cmd (discard it)
                int32_t raw_val = ((int32_t)rx_rdata[1] << 16) | ((int32_t)rx_rdata[2] << 8) | rx_rdata[3];
                
                // ✅ Sign-extend to 32 bits before shifting
                if (raw_val & 0x800000) {         // if bit 23 is set, it's negative
                    raw_val |= 0xFF000000;        // sign-extend into upper byte
                }
                
                float tempC = (float)(raw_val >> 10) * 0.03125f;
                printk("Internal Temperature: %.2f C\n", (double)tempC);
            } else {
                printk("SPI Transceive Error: %d\n", err);
            }
        } else {
            int current_drdy = gpio_pin_get_dt(&drdy_pin);
            printk("Timeout waiting for DRDY! Current DRDY pin state: %d\n", current_drdy);
        }
        
        k_msleep(1000);
    }
    
    return 0;
}