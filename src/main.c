#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/* Get the raw I2C bus device (i2c1) */
static const struct device *i2c_bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));

int main(void) {
    printk("====================================\n");
    printk("   XIAO I2C1 (D4/D5) BUS SCANNER    \n");
    printk("====================================\n");

    if (!device_is_ready(i2c_bus)) {
        printk("FATAL ERROR: I2C bus not ready!\n");
        return 0;
    }

    printk("I2C bus ready. Starting scan...\n");

    while (1) {
        printk("\nScanning...\n");
        uint8_t count = 0;
        
        for (uint8_t addr = 0x01; addr <= 0x7F; addr++) {
            uint8_t dummy_data = 0;
            // Attempt to read 0 bytes to see if the address ACKs
            int err = i2c_write(i2c_bus, &dummy_data, 0, addr);

            if (err == 0) {
                printk("---> FOUND DEVICE AT ADDRESS: 0x%02X\n", addr);
                count++;
            }
        }

        if (count == 0) {
            printk("NO DEVICES FOUND. Check wiring, power, and pull-up resistors!\n");
        } else {
            printk("Scan complete. Found %d device(s).\n", count);
        }

        k_msleep(3000); // Scan every 3 seconds
    }
    return 0;
}