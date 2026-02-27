#ifndef EEPROM_KV_H
#define EEPROM_KV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EEPROM_KV_MAX_KEY_LEN   16
#define EEPROM_KV_MAX_VALUE_LEN 32

typedef struct {
    uint16_t slot_count;
    uint16_t free_slots;
    uint16_t used_slots;
    uint32_t max_erase;
    uint32_t min_erase;
    uint32_t erase_spread;
    uint32_t max_write;
    uint32_t min_write;
    uint32_t write_spread;
    uint16_t key_count;
} eeprom_kv_stats_t;

typedef struct eeprom_kv {
    uint16_t slot_count;
    uint32_t global_version;
    void *slots;
    void *index;
    uint32_t *erase_count;
    uint32_t *write_count;
} eeprom_kv_t;

size_t eeprom_kv_required_bytes(uint16_t slot_count);

bool eeprom_kv_init(eeprom_kv_t *kv, uint16_t slot_count, void *buffer, size_t buffer_len);
bool eeprom_kv_put(eeprom_kv_t *kv, const char *key, const uint8_t *value, uint16_t value_len);
bool eeprom_kv_get(const eeprom_kv_t *kv, const char *key, uint8_t *out, uint16_t out_cap, uint16_t *out_len);
bool eeprom_kv_delete(eeprom_kv_t *kv, const char *key);
void eeprom_kv_compact(eeprom_kv_t *kv);
void eeprom_kv_stats(const eeprom_kv_t *kv, eeprom_kv_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
