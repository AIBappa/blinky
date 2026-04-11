#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_NODE DT_NODELABEL(xiao_i2c)

int main(void) {
    k_msleep(3000);

    printk("\nI2C Scanner starting...\n");

    const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

    if (!device_is_ready(i2c_dev)) {
        printk("ERROR: I2C not ready\n");
        return 0;
    }

    printk("Scanning 0x08 to 0x77...\n");
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t dummy;
        int ret = i2c_read(i2c_dev, &dummy, 1, addr);
        if (ret == 0) {
            printk(">>> Device found at: 0x%02X <<<\n", addr);
            found++;
        }
    }

    if (found == 0) {
        printk("No devices found!\n");
        printk("Check: VDD, GND, SDA, SCL connections\n");
    } else {
        printk("Scan complete. Found %d device(s).\n", found);
    }

    /* Repeat scan every 5 seconds */
    while (1) {
        k_msleep(5000);
        printk("\nRescanning...\n");
        found = 0;
        for (uint8_t addr = 0x08; addr < 0x78; addr++) {
            uint8_t dummy;
            int ret = i2c_read(i2c_dev, &dummy, 1, addr);
            if (ret == 0) {
                printk(">>> Device found at: 0x%02X <<<\n", addr);
                found++;
            }
        }
        if (found == 0) {
            printk("Still nothing found.\n");
        }
    }
    return 0;
}