#include "eeprom_kv.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_put_get_delete(void) {
    enum { SLOT_COUNT = 16 };
    uint8_t storage[eeprom_kv_required_bytes(SLOT_COUNT)];
    eeprom_kv_t kv;
    assert(eeprom_kv_init(&kv, SLOT_COUNT, storage, sizeof(storage)));

    assert(eeprom_kv_put(&kv, "a", (const uint8_t *)"1", 1));
    assert(eeprom_kv_put(&kv, "b", (const uint8_t *)"2", 1));

    uint8_t out[8];
    uint16_t out_len = 0;
    assert(eeprom_kv_get(&kv, "a", out, sizeof(out), &out_len));
    assert(out_len == 1 && out[0] == '1');

    assert(eeprom_kv_delete(&kv, "a"));
    assert(!eeprom_kv_get(&kv, "a", out, sizeof(out), &out_len));
}

static void test_update_latest(void) {
    enum { SLOT_COUNT = 16 };
    uint8_t storage[eeprom_kv_required_bytes(SLOT_COUNT)];
    eeprom_kv_t kv;
    assert(eeprom_kv_init(&kv, SLOT_COUNT, storage, sizeof(storage)));

    assert(eeprom_kv_put(&kv, "k", (const uint8_t *)"old", 3));
    assert(eeprom_kv_put(&kv, "k", (const uint8_t *)"new", 3));

    uint8_t out[8];
    uint16_t out_len = 0;
    assert(eeprom_kv_get(&kv, "k", out, sizeof(out), &out_len));
    assert(out_len == 3);
    assert(memcmp(out, "new", 3) == 0);
}

static void test_wear_leveling_spread(void) {
    enum { SLOT_COUNT = 32 };
    uint8_t storage[eeprom_kv_required_bytes(SLOT_COUNT)];
    eeprom_kv_t kv;
    assert(eeprom_kv_init(&kv, SLOT_COUNT, storage, sizeof(storage)));

    for (int i = 0; i < 300; i++) {
        char rolling[16];
        char cfg[16];
        int rolling_len = snprintf(rolling, sizeof(rolling), "%d", i);
        int cfg_len = snprintf(cfg, sizeof(cfg), "v%d", i % 9);
        assert(eeprom_kv_put(&kv, "rolling", (const uint8_t *)rolling, (uint16_t)rolling_len));
        assert(eeprom_kv_put(&kv, "cfg", (const uint8_t *)cfg, (uint16_t)cfg_len));
    }

    eeprom_kv_stats_t st;
    eeprom_kv_stats(&kv, &st);
    assert(st.erase_spread <= 3);
    assert(st.write_spread <= 3);
}

int main(void) {
    test_put_get_delete();
    test_update_latest();
    test_wear_leveling_spread();
    return 0;
}
