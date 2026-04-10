/* Code for ADS1115 Sensor Interface - Channel, Gain, SPS and Duration are inputs with defaults in code*/
#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>

#define ADS1115_ADDR 0x48
#define CONFIG_REG   0x01
#define CONV_REG     0x00

/* Custom UUIDs */
#define SVC_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
static struct bt_uuid_128 nadi_svc_uuid = BT_UUID_INIT_128(SVC_UUID);

// Characteristics for: Gain (f1), Channel (f2), SPS (f3), Duration (f4)
static struct bt_uuid_128 char_uuids[] = {
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)),
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2)),
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3)),
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4))
};

static const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(arduino_i2c));

/* System State Struct */
struct sensor_config {
    uint8_t gain_idx;      // 0-5
    uint8_t channel;       // 0-3
    uint8_t sps_idx;       // 0-7 (8 to 860 SPS)
    uint32_t duration_ms;  // 0 = Continuous
    int64_t start_time;
} cfg = {4, 0, 4, 0, 0};   // Defaults: G8, CH0, 128SPS, Continuous

void sync_ads1115() {
    if (!device_is_ready(i2c_dev)) return;

    // Bits 14-12 (MUX), 11-9 (PGA), 8 (Mode=0), 7-5 (Data Rate)
    uint16_t mux = (0x4 + cfg.channel) << 12;
    uint16_t pga = (cfg.gain_idx << 9);
    uint16_t dr  = (cfg.sps_idx << 5);
    uint16_t config = 0x0003 | mux | pga | dr; // 0x0003 includes default comp bits

    uint8_t tx[3] = {CONFIG_REG, (config >> 8), (config & 0xFF)};
    i2c_write(i2c_dev, tx, 3, ADS1115_ADDR);
    cfg.start_time = k_uptime_get(); // Reset recording timer on change
}

/* BLE Callbacks */
static ssize_t on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *data = (uint8_t *)buf;
    if (attr->uuid == &char_uuids[0].uuid) cfg.gain_idx = data[0];
    else if (attr->uuid == &char_uuids[1].uuid) cfg.channel = data[0];
    else if (attr->uuid == &char_uuids[2].uuid) cfg.sps_idx = data[0];
    else if (attr->uuid == &char_uuids[3].uuid) cfg.duration_ms = data[0] * 1000;

    sync_ads1115();
    return len;
}

BT_GATT_SERVICE_DEFINE(nadi_svc,
    BT_GATT_PRIMARY_SERVICE(&nadi_svc_uuid),
    BT_GATT_CHARACTERISTIC(&char_uuids[0].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
    BT_GATT_CHARACTERISTIC(&char_uuids[1].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
    BT_GATT_CHARACTERISTIC(&char_uuids[2].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
    BT_GATT_CHARACTERISTIC(&char_uuids[3].uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE, NULL, on_write, NULL),
);

void main(void) {
    sync_ads1115();
    bt_enable(NULL);

    uint8_t reg = CONV_REG, rx[2];
    while (1) {
        // Check Duration Logic
        if (cfg.duration_ms == 0 || (k_uptime_get() - cfg.start_time) < cfg.duration_ms) {
            if (i2c_write_read(i2c_dev, ADS1115_ADDR, &reg, 1, rx, 2) == 0) {
                int16_t val = (rx[0] << 8) | rx[1];
                printk(">CH[%d]|G[%d]|SPS[%d]:%d\n", cfg.channel, cfg.gain_idx, cfg.sps_idx, val);
            }
        }
        k_msleep(10); // 100Hz default polling
    }
}
