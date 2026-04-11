#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_NODE            DT_NODELABEL(i2c1)
#define ADS1115_ADDR        0x48
#define ADS1115_REG_CONV    0x00
#define ADS1115_REG_CONFIG  0x01

int main(void) {
    /* Simply wait for USB to enumerate and terminal to connect */
    k_msleep(3000);

    printk("\n\nADS1115 I2C Reader starting...\n");

    const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

    if (!device_is_ready(i2c_dev)) {
        while (1) {
            printk("ERROR: I2C not ready - check wiring\n");
            k_msleep(2000);
        }
    }
    printk("I2C ready.\n");

    /* AIN0 vs GND, FS=4.096V, Continuous mode, 128SPS */
    uint8_t config[2] = {0x42, 0x83};
    int err = i2c_burst_write(i2c_dev, ADS1115_ADDR,
                              ADS1115_REG_CONFIG, config, 2);
    if (err < 0) {
        while (1) {
            printk("ERROR: ADS1115 config failed (%d) - check SDA/SCL\n", err);
            k_msleep(2000);
        }
    }
    printk("ADS1115 configured OK.\n\n");

    while (1) {
        uint8_t buf[2];
        err = i2c_burst_read(i2c_dev, ADS1115_ADDR,
                             ADS1115_REG_CONV, buf, 2);
        if (err < 0) {
            printk("Read error: %d\n", err);
        } else {
            int16_t raw     = (buf[0] << 8) | buf[1];
            float voltage   = (raw * 4.096f) / 32768.0f;
            printk("Raw: %6d  |  Voltage: %.4f V\n", raw, (double)voltage);
        }
        k_msleep(1000);
    }
    return 0;
}