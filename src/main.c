#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#include <zephyr/drivers/uart.h>

#define I2C_NODE DT_NODELABEL(i2c1)
#define ADS1115_I2C_ADDRESS 0x48

#define ADS1115_REG_CONV   0x00
#define ADS1115_REG_CONFIG 0x01

int main(void) {
    /* Wait for the USB Serial console to be connected (up to 5 seconds) */
    const struct device *console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;
    if (device_is_ready(console_dev)) {
        for (int i = 0; i < 50; i++) {
            uart_line_ctrl_get(console_dev, UART_LINE_CTRL_DTR, &dtr);
            if (dtr) {
                break;
            }
            k_msleep(100);
        }
    }
    k_msleep(1000); // Give terminal a second to render

    const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

    printk("====================================\n");
    printk("   RAW I2C ADS1115 READER (BURST)   \n");
    printk("====================================\n");

    if (!device_is_ready(i2c_dev)) {
        printk("I2C bus i2c1 is not ready!\n");
        return 0;
    }

    /* Configure ADS1115: Config Register (0x01) 
     * We want to write 2 bytes to this register:
     * MSB: 0x42 (AIN0 to GND, FS=4.096V, Continuous mode)
     * LSB: 0x83 (128 SPS, Disable comparator)
     */
    uint8_t config_bytes[2] = {0x42, 0x83};
    int err = i2c_burst_write(i2c_dev, ADS1115_I2C_ADDRESS, ADS1115_REG_CONFIG, config_bytes, 2);
    if (err < 0) {
        printk("Failed to configure ADS1115 (err %d). Check wiring.\n", err);
        return 0;
    }
    printk("ADS1115 configured successfully via burst write.\n\n");

    while (1) {
        uint8_t read_buf[2];

        /* Burst Read from the Conversion Register (0x00) */
        err = i2c_burst_read(i2c_dev, ADS1115_I2C_ADDRESS, ADS1115_REG_CONV, read_buf, 2);
        
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