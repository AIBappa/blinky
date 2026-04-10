/* Code for ADS1115 Sensor Interface - Channel, Gain, SPS and Duration are inputs with defaults in code*/
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <errno.h> // Required for error codes

/* Custom UUIDs */
#define SVC_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
static struct bt_uuid_128 wav_svc_uuid = BT_UUID_INIT_128(SVC_UUID);

// Characteristics for: Gain (f1), Channel (f2), SPS (f3), Duration (f4)
static struct bt_uuid_128 char_uuids[] = {
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)),
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)),
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3)),
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4))
};

/* Reference the ADS1115 node from the devicetree */
static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(ads1115));

/* Gain Map: 0: 2/3, 1: 1, 2: 2, 3: 4, 4: 8, 5: 16 */
static const enum adc_gain gain_map[] = {
    ADC_GAIN_2_3, ADC_GAIN_1, ADC_GAIN_2, 
    ADC_GAIN_4, ADC_GAIN_8, ADC_GAIN_16
};

/* System State Struct */
struct sensor_config {
    uint8_t gain_idx;      // 0-5
    uint8_t channel;       // 0-3
    uint8_t sps_idx;       // 0-7 (Ignored by Zephyr ADC API by default)
    uint32_t duration_ms;  // 0 = Continuous
    int64_t start_time;
} cfg = {1, 0, 4, 0, 0};   // Defaults: G1 (+/-4.096V), CH0, Continuous

/* Zephyr ADC Channel Configuration */
struct adc_channel_cfg channel_cfg = {
    .reference = ADC_REF_INTERNAL,
    .gain = ADC_GAIN_1,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 0,
    .differential = 0
};

void sync_ads1115() {
    if (!device_is_ready(adc_dev)) {
        printk("Error: ADC device not ready\n");
        return;
    }

    channel_cfg.channel_id = cfg.channel;
    if (cfg.gain_idx <= 5) {
        channel_cfg.gain = gain_map[cfg.gain_idx];
    }

    int err = adc_channel_setup(adc_dev, &channel_cfg);
    if (err) {
        printk("ADC Setup Error: %d\n", err);
    } else {
        cfg.start_time = k_uptime_get(); 
        printk("ADS1115 Setup: CH%d, GainIdx%d\n", cfg.channel, cfg.gain_idx);
    }
}

/* BLE Callbacks */
static ssize_t on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *data = (uint8_t *)buf;
    if (bt_uuid_cmp(attr->uuid, &char_uuids[0].uuid) == 0) cfg.gain_idx = data[0];
    else if (bt_uuid_cmp(attr->uuid, &char_uuids[1].uuid) == 0) cfg.channel = data[0];
    else if (bt_uuid_cmp(attr->uuid, &char_uuids[2].uuid) == 0) cfg.sps_idx = data[0];
    else if (bt_uuid_cmp(attr->uuid, &char_uuids[3].uuid) == 0) cfg.duration_ms = data[0] * 1000;

    sync_ads1115();
    return len;
}

BT_GATT_SERVICE_DEFINE(wav_svc,
    BT_GATT_PRIMARY_SERVICE(&wav_svc_uuid),
    BT_GATT_CHARACTERISTIC(&char_uuids[0].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
    BT_GATT_CHARACTERISTIC(&char_uuids[1].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
    BT_GATT_CHARACTERISTIC(&char_uuids[2].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
    BT_GATT_CHARACTERISTIC(&char_uuids[3].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
);

int main(void) {
    printk("Starting waveform acquisition via Zephyr ADC API...\n");
    sync_ads1115();
    bt_enable(NULL);

    int16_t sample_buffer;
    struct adc_sequence sequence = {
        .buffer      = &sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        // The Zephyr ADS1X1X driver requires the resolution to be set to 15 for the ADS1115
        .resolution  = 15,
    };

    while (1) {
        if (cfg.duration_ms == 0 || (k_uptime_get() - cfg.start_time) < cfg.duration_ms) {
            // Tell the sequence to read from the currently active channel bitmask
            sequence.channels = BIT(cfg.channel); 

            int err = adc_read(adc_dev, &sequence);
            
            if (err == 0) {
                printk(">CH[%d]|G[%d]:%d\n", cfg.channel, cfg.gain_idx, sample_buffer);
            } else {
                // If the sensor is unplugged or wiring is bad, you'll see an ADC error here
                printk("ADC Read Error: %d\n", err);
                k_msleep(500); // Slow down the loop on error to avoid flooding logs
            }
        }
        k_msleep(10); 
    }
	return 0; // Standard for int main
}