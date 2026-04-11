#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_NODE DT_NODELABEL(i2c1)
#define ADS1115_I2C_ADDRESS 0x48

static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

int main(void) {
    /* Delay to allow USB serial to connect so you don't miss logs */
    k_msleep(3000);

    printk("====================================\n");
    printk("     RAW I2C ADS1115 READER         \n");
    printk("====================================\n");

    if (!device_is_ready(i2c_dev)) {
        printk("I2C bus i2c1 is not ready!\n");
        return 0;
    }

    /* Configure ADS1115: Config Register (0x01) 
     * MSB: 0x42 (AIN0 to GND, FS=4.096V, Continuous mode)
     * LSB: 0x83 (128 SPS, Disable comparator)
     */
    uint8_t config_buf[3] = {0x01, 0x42, 0x83};
    int err = i2c_write(i2c_dev, config_buf, 3, ADS1115_I2C_ADDRESS);
    if (err < 0) {
        printk("Failed to configure ADS1115 (err %d). Check wiring.\n", err);
        return 0;
    }
    printk("ADS1115 configured successfully.\n\n");

    while (1) {
        uint8_t reg_addr = 0x00; // Conversion register
        uint8_t read_buf[2];

        /* Write register address, then read 2 bytes back */
        err = i2c_write_read(i2c_dev, ADS1115_I2C_ADDRESS, &reg_addr, 1, read_buf, 2);
        
        if (err < 0) {
            printk("I2C read failed: %d\n", err);
        } else {
            /* Combine MSB and LSB */
            int16_t adc_val = (read_buf[0] << 8) | read_buf[1];
            
            /* Convert raw reading to voltage (FS = +/- 4.096V, 15-bit resolution = 32768) */
            float voltage = (adc_val * 4.096f) / 32768.0f;
            
            printk("Raw ADS1115 reading: %d  |  Voltage: %.4f V\n", adc_val, (double)voltage);
        }

        k_msleep(1000);
    }
    return 0;
}