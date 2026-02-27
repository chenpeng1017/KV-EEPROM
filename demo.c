#include "eeprom_kv.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *mem;
    uint32_t size;
} mock_eeprom_t;

static bool mock_read(void *ctx, uint32_t addr, uint8_t *buf, uint16_t len) {
    mock_eeprom_t *m = (mock_eeprom_t *)ctx;
    if (addr + len > m->size) return false;
    memcpy(buf, &m->mem[addr], len);
    return true;
}

static bool mock_write(void *ctx, uint32_t addr, const uint8_t *buf, uint16_t len) {
    mock_eeprom_t *m = (mock_eeprom_t *)ctx;
    if (addr + len > m->size) return false;
    memcpy(&m->mem[addr], buf, len);
    return true;
}

int main(void) {
    enum { SLOT_COUNT = 32 };
    uint8_t eeprom_mem[SLOT_COUNT * sizeof(uint8_t) * 64];
    memset(eeprom_mem, 0xFF, sizeof(eeprom_mem));

    uint8_t kv_ram[eeprom_kv_required_ram_bytes(SLOT_COUNT)];
    mock_eeprom_t chip = { .mem = eeprom_mem, .size = sizeof(eeprom_mem) };
    eeprom_kv_t kv;

    if (!eeprom_kv_init(&kv, SLOT_COUNT, 0, mock_read, mock_write, &chip,
                        kv_ram, sizeof(kv_ram), true)) {
        printf("init failed\n");
        return 1;
    }

    eeprom_kv_put(&kv, "device_id", (const uint8_t *)"A1001", 5);
    eeprom_kv_put(&kv, "wifi_ssid", (const uint8_t *)"factory-net", 11);
    eeprom_kv_put(&kv, "wifi_pwd", (const uint8_t *)"12345678", 8);

    for (int i = 0; i < 200; i++) {
        char counter[16];
        char pwd[16];
        int c_len = snprintf(counter, sizeof(counter), "%d", i);
        int p_len = snprintf(pwd, sizeof(pwd), "pwd-%d", i % 17);
        eeprom_kv_put(&kv, "counter", (const uint8_t *)counter, (uint16_t)c_len);
        eeprom_kv_put(&kv, "wifi_pwd", (const uint8_t *)pwd, (uint16_t)p_len);
    }

    char buf[32];
    uint16_t out_len = 0;
    if (eeprom_kv_get(&kv, "device_id", (uint8_t *)buf, sizeof(buf), &out_len)) {
        buf[out_len] = '\0';
        printf("device_id=%s\n", buf);
    }

    eeprom_kv_stats_t st;
    eeprom_kv_stats(&kv, &st);
    printf("stats: slots=%u free=%u used=%u erase_spread=%lu write_spread=%lu keys=%u\n",
           st.slot_count, st.free_slots, st.used_slots,
           (unsigned long)st.erase_spread, (unsigned long)st.write_spread, st.key_count);

    return 0;
}
