#include "eeprom_kv.h"

#include <limits.h>
#include <string.h>

#define SLOT_FLAG_FREE  0xFFu
#define SLOT_FLAG_VALID 0xA5u
#define SLOT_FLAG_STALE 0x00u

typedef struct {
    uint8_t flag;
    uint8_t key_len;
    uint16_t value_len;
    uint32_t version;
    char key[EEPROM_KV_MAX_KEY_LEN + 1];
    uint8_t value[EEPROM_KV_MAX_VALUE_LEN];
} kv_slot_image_t;

size_t eeprom_kv_required_ram_bytes(uint16_t slot_count) {
    return sizeof(eeprom_kv_index_entry_t) * slot_count +
           sizeof(uint32_t) * slot_count +
           sizeof(uint32_t) * slot_count;
}

uint16_t eeprom_kv_slot_size(void) {
    return (uint16_t)sizeof(kv_slot_image_t);
}

static uint16_t key_len_bounded(const char *s) {
    uint16_t n = 0;
    while (s[n] != '\0') {
        n++;
        if (n > EEPROM_KV_MAX_KEY_LEN) return n;
    }
    return n;
}

static bool slot_read(const eeprom_kv_t *kv, uint16_t slot, kv_slot_image_t *out) {
    uint32_t addr = kv->eeprom_base + ((uint32_t)slot * kv->slot_size);
    return kv->read_cb(kv->io_ctx, addr, (uint8_t *)out, kv->slot_size);
}

static bool slot_write(const eeprom_kv_t *kv, uint16_t slot, const kv_slot_image_t *in) {
    uint32_t addr = kv->eeprom_base + ((uint32_t)slot * kv->slot_size);
    return kv->write_cb(kv->io_ctx, addr, (const uint8_t *)in, kv->slot_size);
}

static bool slot_mark_stale(eeprom_kv_t *kv, uint16_t slot) {
    uint8_t stale = SLOT_FLAG_STALE;
    uint32_t addr = kv->eeprom_base + ((uint32_t)slot * kv->slot_size);
    return kv->write_cb(kv->io_ctx, addr, &stale, 1);
}

static bool slot_erase(eeprom_kv_t *kv, uint16_t slot) {
    kv_slot_image_t img;
    memset(&img, 0xFF, sizeof(img));
    if (!slot_write(kv, slot, &img)) return false;
    kv->erase_count[slot]++;
    return true;
}

static int16_t find_index_entry(const eeprom_kv_t *kv, const char *key) {
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (kv->index[i].used && strcmp(kv->index[i].key, key) == 0) return (int16_t)i;
    }
    return -1;
}

static int16_t alloc_index_entry(eeprom_kv_t *kv, const char *key) {
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (!kv->index[i].used) {
            kv->index[i].used = true;
            kv->index[i].slot = -1;
            strcpy(kv->index[i].key, key);
            return (int16_t)i;
        }
    }
    return -1;
}

static uint16_t free_slot_count(const eeprom_kv_t *kv) {
    uint16_t free_n = 0;
    kv_slot_image_t img;
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (slot_read(kv, i, &img) && img.flag == SLOT_FLAG_FREE) {
            free_n++;
        }
    }
    return free_n;
}

static uint16_t select_slot_for_write(const eeprom_kv_t *kv) {
    uint16_t best = UINT16_MAX;
    kv_slot_image_t img;
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (!slot_read(kv, i, &img)) continue;
        if (img.flag == SLOT_FLAG_FREE) {
            if (best == UINT16_MAX ||
                kv->erase_count[i] < kv->erase_count[best] ||
                (kv->erase_count[i] == kv->erase_count[best] && kv->write_count[i] < kv->write_count[best])) {
                best = i;
            }
        }
    }
    return best;
}

static bool rebuild_index(eeprom_kv_t *kv) {
    memset(kv->index, 0, sizeof(eeprom_kv_index_entry_t) * kv->slot_count);
    kv->global_version = 1;

    kv_slot_image_t img;
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (!slot_read(kv, i, &img)) return false;
        if (img.flag != SLOT_FLAG_VALID) continue;
        if (img.key_len == 0 || img.key_len > EEPROM_KV_MAX_KEY_LEN || img.value_len > EEPROM_KV_MAX_VALUE_LEN) continue;
        img.key[img.key_len] = '\0';

        if (img.version >= kv->global_version) kv->global_version = img.version + 1;

        int16_t idx = find_index_entry(kv, img.key);
        if (idx < 0) {
            idx = alloc_index_entry(kv, img.key);
            if (idx < 0) return false;
            kv->index[idx].slot = (int16_t)i;
        } else {
            kv_slot_image_t cur;
            if (!slot_read(kv, (uint16_t)kv->index[idx].slot, &cur)) return false;
            if (img.version > cur.version) {
                kv->index[idx].slot = (int16_t)i;
            }
        }
    }
    return true;
}

bool eeprom_kv_init(eeprom_kv_t *kv,
                    uint16_t slot_count,
                    uint32_t eeprom_base,
                    eeprom_kv_read_fn read_cb,
                    eeprom_kv_write_fn write_cb,
                    void *io_ctx,
                    void *ram_buffer,
                    size_t ram_buffer_len,
                    bool format_on_init) {
    if (kv == NULL || read_cb == NULL || write_cb == NULL || ram_buffer == NULL || slot_count < 8) return false;

    size_t need = eeprom_kv_required_ram_bytes(slot_count);
    if (ram_buffer_len < need) return false;

    memset(kv, 0, sizeof(*kv));
    kv->slot_count = slot_count;
    kv->slot_size = eeprom_kv_slot_size();
    kv->eeprom_base = eeprom_base;
    kv->read_cb = read_cb;
    kv->write_cb = write_cb;
    kv->io_ctx = io_ctx;

    uint8_t *p = (uint8_t *)ram_buffer;
    kv->index = (eeprom_kv_index_entry_t *)p;
    p += sizeof(eeprom_kv_index_entry_t) * slot_count;
    kv->erase_count = (uint32_t *)p;
    p += sizeof(uint32_t) * slot_count;
    kv->write_count = (uint32_t *)p;
    memset(ram_buffer, 0, need);

    if (format_on_init) {
        for (uint16_t i = 0; i < slot_count; i++) {
            if (!slot_erase(kv, i)) return false;
        }
    }

    return rebuild_index(kv);
}

bool eeprom_kv_put(eeprom_kv_t *kv, const char *key, const uint8_t *value, uint16_t value_len) {
    if (kv == NULL || key == NULL || value == NULL) return false;

    uint16_t key_len = key_len_bounded(key);
    if (key_len == 0 || key_len > EEPROM_KV_MAX_KEY_LEN || value_len > EEPROM_KV_MAX_VALUE_LEN) return false;

    uint16_t slot = select_slot_for_write(kv);
    if (slot == UINT16_MAX) {
        eeprom_kv_compact(kv);
        slot = select_slot_for_write(kv);
        if (slot == UINT16_MAX) return false;
    }

    kv_slot_image_t img;
    memset(&img, 0xFF, sizeof(img));
    img.flag = SLOT_FLAG_VALID;
    img.key_len = (uint8_t)key_len;
    img.value_len = value_len;
    img.version = kv->global_version++;
    memcpy(img.key, key, key_len);
    memcpy(img.value, value, value_len);

    if (!slot_write(kv, slot, &img)) return false;
    kv->write_count[slot]++;

    int16_t idx = find_index_entry(kv, key);
    if (idx >= 0 && kv->index[idx].slot >= 0) {
        if (!slot_mark_stale(kv, (uint16_t)kv->index[idx].slot)) return false;
    }

    if (idx < 0) {
        idx = alloc_index_entry(kv, key);
        if (idx < 0) return false;
    }
    kv->index[idx].slot = (int16_t)slot;

    if (free_slot_count(kv) < ((kv->slot_count / 8) + 1)) eeprom_kv_compact(kv);
    return true;
}

bool eeprom_kv_get(const eeprom_kv_t *kv, const char *key, uint8_t *out, uint16_t out_cap, uint16_t *out_len) {
    if (kv == NULL || key == NULL || out == NULL || out_len == NULL) return false;

    int16_t idx = find_index_entry(kv, key);
    if (idx < 0 || kv->index[idx].slot < 0) return false;

    kv_slot_image_t img;
    if (!slot_read(kv, (uint16_t)kv->index[idx].slot, &img)) return false;
    if (img.flag != SLOT_FLAG_VALID || img.value_len > out_cap) return false;

    memcpy(out, img.value, img.value_len);
    *out_len = img.value_len;
    return true;
}

bool eeprom_kv_delete(eeprom_kv_t *kv, const char *key) {
    if (kv == NULL || key == NULL) return false;

    int16_t idx = find_index_entry(kv, key);
    if (idx < 0 || kv->index[idx].slot < 0) return false;

    if (!slot_mark_stale(kv, (uint16_t)kv->index[idx].slot)) return false;
    kv->index[idx].used = false;
    kv->index[idx].slot = -1;
    kv->index[idx].key[0] = '\0';
    return true;
}

void eeprom_kv_compact(eeprom_kv_t *kv) {
    if (kv == NULL) return;

    typedef struct {
        char key[EEPROM_KV_MAX_KEY_LEN + 1];
        uint8_t value[EEPROM_KV_MAX_VALUE_LEN];
        uint16_t value_len;
        uint32_t version;
    } live_t;

    live_t live[kv->slot_count];
    uint16_t live_count = 0;
    kv_slot_image_t img;

    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (kv->index[i].used && kv->index[i].slot >= 0) {
            if (slot_read(kv, (uint16_t)kv->index[i].slot, &img) && img.flag == SLOT_FLAG_VALID) {
                strcpy(live[live_count].key, kv->index[i].key);
                memcpy(live[live_count].value, img.value, img.value_len);
                live[live_count].value_len = img.value_len;
                live[live_count].version = img.version;
                live_count++;
            }
        }
    }

    for (uint16_t i = 0; i < kv->slot_count; i++) {
        (void)slot_erase(kv, i);
    }
    memset(kv->index, 0, sizeof(eeprom_kv_index_entry_t) * kv->slot_count);

    for (uint16_t i = 0; i < live_count; i++) {
        uint16_t oldest = i;
        for (uint16_t j = i + 1; j < live_count; j++) {
            if (live[j].version < live[oldest].version) oldest = j;
        }
        if (oldest != i) {
            live_t tmp = live[i];
            live[i] = live[oldest];
            live[oldest] = tmp;
        }
    }

    for (uint16_t i = 0; i < live_count; i++) {
        uint16_t slot = select_slot_for_write(kv);
        if (slot == UINT16_MAX) break;

        memset(&img, 0xFF, sizeof(img));
        img.flag = SLOT_FLAG_VALID;
        img.key_len = (uint8_t)strlen(live[i].key);
        img.value_len = live[i].value_len;
        img.version = live[i].version;
        memcpy(img.key, live[i].key, img.key_len);
        memcpy(img.value, live[i].value, img.value_len);

        if (slot_write(kv, slot, &img)) {
            kv->write_count[slot]++;
            int16_t idx = alloc_index_entry(kv, live[i].key);
            if (idx >= 0) kv->index[idx].slot = (int16_t)slot;
        }
    }

    if (!rebuild_index(kv)) {
        memset(kv->index, 0, sizeof(eeprom_kv_index_entry_t) * kv->slot_count);
    }
}

void eeprom_kv_stats(const eeprom_kv_t *kv, eeprom_kv_stats_t *out) {
    if (kv == NULL || out == NULL) return;

    uint32_t max_e = 0, min_e = UINT_MAX, max_w = 0, min_w = UINT_MAX;
    uint16_t key_count = 0;

    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (kv->erase_count[i] > max_e) max_e = kv->erase_count[i];
        if (kv->erase_count[i] < min_e) min_e = kv->erase_count[i];
        if (kv->write_count[i] > max_w) max_w = kv->write_count[i];
        if (kv->write_count[i] < min_w) min_w = kv->write_count[i];
        if (kv->index[i].used) key_count++;
    }

    out->slot_count = kv->slot_count;
    out->free_slots = free_slot_count(kv);
    out->used_slots = kv->slot_count - out->free_slots;
    out->max_erase = max_e;
    out->min_erase = (min_e == UINT_MAX) ? 0 : min_e;
    out->erase_spread = out->max_erase - out->min_erase;
    out->max_write = max_w;
    out->min_write = (min_w == UINT_MAX) ? 0 : min_w;
    out->write_spread = out->max_write - out->min_write;
    out->key_count = key_count;
}
