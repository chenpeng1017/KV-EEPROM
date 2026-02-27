#include "eeprom_kv.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    enum { SLOT_COUNT = 32 };
    uint8_t storage[eeprom_kv_required_bytes(SLOT_COUNT)];
    eeprom_kv_t kv;
    if (!eeprom_kv_init(&kv, SLOT_COUNT, storage, sizeof(storage))) {
        printf("init failed\n");
        return 1;
    }

    eeprom_kv_put(&kv, "device_id", (const uint8_t *)"A1001", 5);
    eeprom_kv_put(&kv, "wifi_ssid", (const uint8_t *)"factory-net", 11);
    eeprom_kv_put(&kv, "wifi_pwd", (const uint8_t *)"12345678", 8);

    char buf[32];
    uint16_t out_len = 0;

    for (int i = 0; i < 200; i++) {
        char counter[16];
        char pwd[16];
        int c_len = snprintf(counter, sizeof(counter), "%d", i);
        int p_len = snprintf(pwd, sizeof(pwd), "pwd-%d", i % 17);
        eeprom_kv_put(&kv, "counter", (const uint8_t *)counter, (uint16_t)c_len);
        eeprom_kv_put(&kv, "wifi_pwd", (const uint8_t *)pwd, (uint16_t)p_len);
    }

    if (eeprom_kv_get(&kv, "device_id", (uint8_t *)buf, sizeof(buf), &out_len)) {
        buf[out_len] = '\0';
        printf("device_id=%s\n", buf);
    }
    if (eeprom_kv_get(&kv, "wifi_pwd", (uint8_t *)buf, sizeof(buf), &out_len)) {
        buf[out_len] = '\0';
        printf("wifi_pwd=%s\n", buf);
    }
    if (eeprom_kv_get(&kv, "counter", (uint8_t *)buf, sizeof(buf), &out_len)) {
        buf[out_len] = '\0';
        printf("counter=%s\n", buf);
    }

    eeprom_kv_stats_t st;
    eeprom_kv_stats(&kv, &st);
    printf("stats: slots=%u free=%u used=%u erase_spread=%lu write_spread=%lu keys=%u\n",
           st.slot_count, st.free_slots, st.used_slots,
           (unsigned long)st.erase_spread, (unsigned long)st.write_spread, st.key_count);

    return 0;
}
