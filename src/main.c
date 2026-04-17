#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

/* 1. Hardware Definitions */
// This matches the "ads1220" label in your app.overlay
#define SPI_NODE DT_NODELABEL(ads1220)

// SPI Config: Mode 1 (CPHA=1), 8-bit words, 1MHz
static const struct spi_dt_spec ads_spi = SPI_DT_SPEC_GET(SPI_NODE, 
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPHA, 0);

/* 2. ADS1220 Commands */
#define ADS1220_CMD_WREG_REG1 0x44  // Write 1 register starting at Reg 1
#define ADS1220_CMD_RDATA     0x10  // Read Data command
#define ADS1220_REG1_TEMP_ON  0x02  // Bit 1 = 1 (Enables Temp Sensor)

void main(void) {
    int err;

    printk("\n--- ADS1220 Temperature Checker (Zephyr) ---\n");

    // Check if the SPI bus device is ready
    if (!device_is_ready(ads_spi.bus)) {
        printk("Error: SPI bus device not ready.\n");
        return;
    }

    /* 3. Initialization: Turn on Temperature Sensor */
    uint8_t init_tx[] = { ADS1220_CMD_WREG_REG1, ADS1220_REG1_TEMP_ON };
    struct spi_buf tx_buf = { .buf = init_tx, .len = sizeof(init_tx) };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

    err = spi_write_dt(&ads_spi, &tx_set);
    if (err) {
        printk("SPI Write Error: %d\n", err);
        return;
    }
    printk("ADS1220 configured for Temperature Mode.\n");

    /* 4. Main Sampling Loop */
    while (1) {
        // Conversion time + buffer
        k_msleep(500);

        uint8_t cmd = ADS1220_CMD_RDATA;
        uint8_t rx_raw[3] = {0, 0, 0};

        struct spi_buf tx_cmd_buf = { .buf = &cmd, .len = 1 };
        struct spi_buf_set tx_cmd_set = { .buffers = &tx_cmd_buf, .count = 1 };

        struct spi_buf rx_data_buf = { .buf = rx_raw, .len = 3 };
        struct spi_buf_set rx_data_set = { .buffers = &rx_data_buf, .count = 1 };

        /* spi_transceive_dt pulls CS low, sends 0x10, 
           then immediately reads 3 bytes before raising CS.
        */
        err = spi_transceive_dt(&ads_spi, &tx_cmd_set, &rx_data_set);

        if (err == 0) {
            // Combine three 8-bit responses into a 24-bit integer
            int32_t raw_val = (rx_raw[0] << 16) | (rx_raw[1] << 8) | rx_raw[2];
            
            // Temperature is a 14-bit value, left-justified in the 24-bit result
            // Shift right by 10 to get the 14-bit int, then multiply by LSB (0.03125)
            float tempC = (float)(raw_val >> 10) * 0.03125f;

            printk("Internal Temperature: %.2f C\n", (double)tempC);
        } else {
            printk("SPI Transceive Error: %d\n", err);
        }
    }
}