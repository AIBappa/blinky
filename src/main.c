#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/drivers/uart.h>

/* Grab the specific ADS1115 channel from the device tree */
static const struct adc_dt_spec adc_channel = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

int main(void) {
    int err;
    int16_t sample_buffer;

    /* Wait for the USB Serial connection to be opened by the user */
    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    uint32_t dtr = 0;
    while (!dtr) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        k_sleep(K_MSEC(100));
    }
    /* Small delay so the terminal doesn't miss the first printk */
    k_msleep(1000);

    printk("====================================\n");
    printk("       ADS1115 SENSOR READER        \n");
    printk("====================================\n");

    /* Check if the ADS1115 device is ready on the I2C bus */
    if (!adc_is_ready_dt(&adc_channel)) {
        printk("FATAL ERROR: ADS1115 device not ready!\n");
        printk("Check your wiring, power, and I2C address (0x48).\n");
        return 0;
    }

    /* Configure the channel with the settings from app.overlay (Gain, Acq Time, etc.) */
    err = adc_channel_setup_dt(&adc_channel);
    if (err < 0) {
        printk("Could not setup channel (%d)\n", err);
        return 0;
    }

    printk("ADS1115 is initialized and ready. Reading values...\n\n");

    /* Prepare the sequence structure for reading */
    struct adc_sequence sequence = {
        .buffer = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
    };

    while (1) {
        /* Ask the ADC API to read a sample using the DT spec */
        err = adc_read_dt(&adc_channel, &sequence);
        if (err < 0) {
            printk("Failed to read from ADC (%d)\n", err);
        } else {
            /* We have a raw 15-bit value from the ADS1115! */
            printk("Raw ADS1115 Reading: %d\n", sample_buffer);
        }

        k_msleep(1000); // Read every 1 second
    }
    return 0;
}