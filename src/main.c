#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* 1. Hardware Definitions */
// This matches the "ads1220" label in your app.overlay
#define SPI_NODE DT_NODELABEL(ads1220)

// SPI Config: Mode 1 (CPHA=1), 8-bit words, 1MHz
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
#define ADS1220_CMD_WREG_REG1 0x44  // Write 1 register starting at Reg 1
#define ADS1220_CMD_RDATA     0x10  // Read Data command
#define ADS1220_CMD_START     0x08  // Start/Sync command
#define ADS1220_REG1_TEMP_ON  0x02  // Bit 1 = 1 (Enables Temp Sensor)

int main(void) {
    int err;

    printk("\n--- ADS1220 Temperature Checker (Zephyr) ---\n");

    // Check if the SPI bus device is ready
    if (!device_is_ready(ads_spi.bus)) {
        printk("Error: SPI bus device not ready.\n");
        return 0;
    }

    // Check and configure DRDY pin
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

    /* 3. Initialization: Turn on Temperature Sensor */
    uint8_t init_tx[] = { ADS1220_CMD_WREG_REG1, ADS1220_REG1_TEMP_ON };
    struct spi_buf tx_buf = { .buf = init_tx, .len = sizeof(init_tx) };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

    err = spi_write_dt(&ads_spi, &tx_set);
    if (err) {
        printk("SPI Write Error: %d\n", err);
        return 0;
    }
    printk("ADS1220 configured for Temperature Mode.\n");

    /* 4. Main Sampling Loop */
    while (1) {
        // Send Start/Sync command to start conversion
        uint8_t start_cmd = ADS1220_CMD_START;
        struct spi_buf tx_start_buf = { .buf = &start_cmd, .len = 1 };
        struct spi_buf_set tx_start_set = { .buffers = &tx_start_buf, .count = 1 };
        spi_write_dt(&ads_spi, &tx_start_set);

        // Wait for DRDY to go active (low) indicating data is ready
        if (k_sem_take(&drdy_sem, K_MSEC(500)) == 0) {
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
        } else {
            printk("Timeout waiting for DRDY!\n");
        }
        
        // Wait before starting the next conversion
        k_msleep(1000);
    }
    
    return 0;
}