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

typedef bool (*eeprom_kv_read_fn)(void *ctx, uint32_t addr, uint8_t *buf, uint16_t len);
typedef bool (*eeprom_kv_write_fn)(void *ctx, uint32_t addr, const uint8_t *buf, uint16_t len);

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

typedef struct {
    char key[EEPROM_KV_MAX_KEY_LEN + 1];
    int16_t slot;
    bool used;
} eeprom_kv_index_entry_t;

typedef struct eeprom_kv {
    uint32_t eeprom_base;
    uint16_t slot_count;
    uint16_t slot_size;
    uint32_t global_version;

    eeprom_kv_read_fn read_cb;
    eeprom_kv_write_fn write_cb;
    void *io_ctx;

    eeprom_kv_index_entry_t *index;
    uint32_t *erase_count;
    uint32_t *write_count;
} eeprom_kv_t;

size_t eeprom_kv_required_ram_bytes(uint16_t slot_count);
uint16_t eeprom_kv_slot_size(void);

bool eeprom_kv_init(eeprom_kv_t *kv,
                    uint16_t slot_count,
                    uint32_t eeprom_base,
                    eeprom_kv_read_fn read_cb,
                    eeprom_kv_write_fn write_cb,
                    void *io_ctx,
                    void *ram_buffer,
                    size_t ram_buffer_len,
                    bool format_on_init);

bool eeprom_kv_put(eeprom_kv_t *kv, const char *key, const uint8_t *value, uint16_t value_len);
bool eeprom_kv_get(const eeprom_kv_t *kv, const char *key, uint8_t *out, uint16_t out_cap, uint16_t *out_len);
bool eeprom_kv_delete(eeprom_kv_t *kv, const char *key);
void eeprom_kv_compact(eeprom_kv_t *kv);
void eeprom_kv_stats(const eeprom_kv_t *kv, eeprom_kv_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
