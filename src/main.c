#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define SPI_NODE DT_NODELABEL(ads1220)
static const struct spi_dt_spec ads_spi = SPI_DT_SPEC_GET(SPI_NODE, 
    SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_MODE_CPHA, 0);

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)
static const struct gpio_dt_spec drdy_pin = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, drdy_gpios);

static struct gpio_callback drdy_cb_data;
static K_SEM_DEFINE(drdy_sem, 0, 1);

void drdy_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    k_sem_give(&drdy_sem);
}

#define ADS1220_CMD_RESET     0x06
#define ADS1220_CMD_START     0x08
#define ADS1220_CMD_RDATA     0x10
#define ADS1220_CMD_RREG      0x20
#define ADS1220_CMD_WREG_REG1 0x44
#define ADS1220_REG1_TEMP_ON  0x02

int main(void) {
    int err;
    k_msleep(2500); 

    printk("\n==========================================\n");
    printk("--- ADS1220 Temperature Checker (Zephyr) ---\n");
    printk("==========================================\n");

    if (!device_is_ready(ads_spi.bus) || !gpio_is_ready_dt(&drdy_pin)) {
        printk("Error: Devices not ready.\n");
        return 0;
    }

    gpio_pin_configure_dt(&drdy_pin, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&drdy_pin, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&drdy_cb_data, drdy_isr, BIT(drdy_pin.pin));
    gpio_add_callback(drdy_pin.port, &drdy_cb_data);

    /* Let ADS1220 power up */
    k_msleep(100); 

    printk("Resetting ADS1220...\n");
    uint8_t reset_cmd = ADS1220_CMD_RESET;
    struct spi_buf tx_reset_buf = { .buf = &reset_cmd, .len = 1 };
    struct spi_buf_set tx_reset_set = { .buffers = &tx_reset_buf, .count = 1 };
    err = spi_write_dt(&ads_spi, &tx_reset_set);
    if (err) { printk("SPI Write Error (Reset): %d\n", err); }
    k_msleep(100); 

    printk("Reading ADS1220 Registers to verify SPI connection...\n");
    uint8_t tx_rreg[16] = { ADS1220_CMD_RREG | 0x03 }; 
    uint8_t rx_rreg[16] = { 0 };
    
    struct spi_buf tx_rreg_buf = { .buf = tx_rreg, .len = 16 };
    struct spi_buf_set tx_rreg_set = { .buffers = &tx_rreg_buf, .count = 1 };
    struct spi_buf rx_rreg_buf = { .buf = rx_rreg, .len = 16 };
    struct spi_buf_set rx_rreg_set = { .buffers = &rx_rreg_buf, .count = 1 };
    
    err = spi_transceive_dt(&ads_spi, &tx_rreg_set, &rx_rreg_set);

    printk("Raw Register Dump (16 bytes read):\n");
    for(int i = 0; i < 16; i++) {
        printk("Byte %d: 0x%02X\n", i, rx_rreg[i]);
    }

    printk("Enabling Temperature Mode (Continuous)...\n");
    uint8_t init_tx[] = { ADS1220_CMD_WREG_REG1, ADS1220_REG1_TEMP_ON | 0x04 };
    struct spi_buf tx_buf = { .buf = init_tx, .len = sizeof(init_tx) };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    spi_write_dt(&ads_spi, &tx_set);

    printk("Starting Continuous Conversions...\n");
    uint8_t start_cmd = ADS1220_CMD_START;
    struct spi_buf tx_start_buf = { .buf = &start_cmd, .len = 1 };
    struct spi_buf_set tx_start_set = { .buffers = &tx_start_buf, .count = 1 };
    spi_write_dt(&ads_spi, &tx_start_set);

    k_msleep(50);

    /* Main Sampling Loop */
    while (1) {
        k_sem_reset(&drdy_sem);

        if (k_sem_take(&drdy_sem, K_MSEC(1000)) == 0) {
            uint8_t tx_rdata[4] = { ADS1220_CMD_RDATA, 0x00, 0x00, 0x00 };
            uint8_t rx_rdata[4] = { 0 };

            struct spi_buf tx_rdata_buf = { .buf = tx_rdata, .len = 4 };
            struct spi_buf_set tx_rdata_set = { .buffers = &tx_rdata_buf, .count = 1 };
            struct spi_buf rx_rdata_buf = { .buf = rx_rdata, .len = 4 };
            struct spi_buf_set rx_rdata_set = { .buffers = &rx_rdata_buf, .count = 1 };

            err = spi_transceive_dt(&ads_spi, &tx_rdata_set, &rx_rdata_set);

            if (err == 0) {
                int32_t raw_val = ((int32_t)rx_rdata[1] << 16) | ((int32_t)rx_rdata[2] << 8) | rx_rdata[3];
                if (raw_val & 0x800000) { raw_val |= 0xFF000000; }
                float tempC = (float)(raw_val >> 10) * 0.03125f;
                printk("Internal Temperature: %.2f C (Raw: 0x%06X)\n", (double)tempC, (raw_val & 0xFFFFFF));
            }
        } else {
            printk("Timeout! DRDY Logical State: %d\n", gpio_pin_get_dt(&drdy_pin));
        }
        
        k_msleep(1000);
    }
    return 0;
}