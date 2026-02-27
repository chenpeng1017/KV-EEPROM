#include "eeprom_kv.h"

#include <limits.h>
#include <string.h>

typedef struct {
    char key[EEPROM_KV_MAX_KEY_LEN + 1];
    uint8_t value[EEPROM_KV_MAX_VALUE_LEN];
    uint16_t value_len;
    uint32_t version;
    bool valid;
    bool occupied;
} kv_slot_t;

typedef struct {
    char key[EEPROM_KV_MAX_KEY_LEN + 1];
    int16_t slot;
    bool used;
} index_entry_t;

#define SLOTS(kv) ((kv_slot_t *)((kv)->slots))
#define INDEX(kv) ((index_entry_t *)((kv)->index))

size_t eeprom_kv_required_bytes(uint16_t slot_count) {
    return sizeof(kv_slot_t) * slot_count +
           sizeof(index_entry_t) * slot_count +
           sizeof(uint32_t) * slot_count +
           sizeof(uint32_t) * slot_count;
}

static uint16_t key_len_bounded(const char *s) {
    uint16_t n = 0;
    while (s[n] != '\0') {
        n++;
        if (n > EEPROM_KV_MAX_KEY_LEN) {
            return n;
        }
    }
    return n;
}

static int16_t find_index_entry(const eeprom_kv_t *kv, const char *key) {
    index_entry_t *idx = INDEX(kv);
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (idx[i].used && strcmp(idx[i].key, key) == 0) {
            return (int16_t)i;
        }
    }
    return -1;
}

static int16_t alloc_index_entry(eeprom_kv_t *kv, const char *key) {
    index_entry_t *idx = INDEX(kv);
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (!idx[i].used) {
            idx[i].used = true;
            strcpy(idx[i].key, key);
            idx[i].slot = -1;
            return (int16_t)i;
        }
    }
    return -1;
}

static uint16_t free_slot_count(const eeprom_kv_t *kv) {
    kv_slot_t *slots = SLOTS(kv);
    uint16_t n = 0;
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (!slots[i].occupied) {
            n++;
        }
    }
    return n;
}

static void erase_slot(eeprom_kv_t *kv, uint16_t idx) {
    kv_slot_t *slots = SLOTS(kv);
    memset(&slots[idx], 0, sizeof(kv_slot_t));
    kv->erase_count[idx]++;
}

static void write_slot(eeprom_kv_t *kv, uint16_t idx, const char *key, const uint8_t *value, uint16_t value_len, uint32_t version) {
    kv_slot_t *slots = SLOTS(kv);
    kv_slot_t *slot = &slots[idx];
    memset(slot, 0, sizeof(*slot));
    strcpy(slot->key, key);
    memcpy(slot->value, value, value_len);
    slot->value_len = value_len;
    slot->version = version;
    slot->valid = true;
    slot->occupied = true;
    kv->write_count[idx]++;
}

static uint16_t select_slot_for_write(const eeprom_kv_t *kv) {
    kv_slot_t *slots = SLOTS(kv);
    uint16_t best = UINT16_MAX;
    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (!slots[i].occupied) {
            if (best == UINT16_MAX ||
                kv->erase_count[i] < kv->erase_count[best] ||
                (kv->erase_count[i] == kv->erase_count[best] && kv->write_count[i] < kv->write_count[best])) {
                best = i;
            }
        }
    }
    return best;
}

static void clear_index(eeprom_kv_t *kv) {
    memset(INDEX(kv), 0, sizeof(index_entry_t) * kv->slot_count);
}

bool eeprom_kv_init(eeprom_kv_t *kv, uint16_t slot_count, void *buffer, size_t buffer_len) {
    if (kv == NULL || buffer == NULL || slot_count < 8) {
        return false;
    }
    size_t need = eeprom_kv_required_bytes(slot_count);
    if (buffer_len < need) {
        return false;
    }

    memset(kv, 0, sizeof(*kv));
    kv->slot_count = slot_count;
    kv->global_version = 1;

    uint8_t *p = (uint8_t *)buffer;
    kv->slots = (void *)p;
    p += sizeof(kv_slot_t) * slot_count;
    kv->index = (void *)p;
    p += sizeof(index_entry_t) * slot_count;
    kv->erase_count = (uint32_t *)p;
    p += sizeof(uint32_t) * slot_count;
    kv->write_count = (uint32_t *)p;

    memset(buffer, 0, need);
    return true;
}

void eeprom_kv_compact(eeprom_kv_t *kv) {
    if (kv == NULL) {
        return;
    }

    typedef struct {
        char key[EEPROM_KV_MAX_KEY_LEN + 1];
        uint8_t value[EEPROM_KV_MAX_VALUE_LEN];
        uint16_t value_len;
        uint32_t version;
    } live_t;

    kv_slot_t *slots = SLOTS(kv);
    index_entry_t *idx = INDEX(kv);
    live_t live[kv->slot_count];
    uint16_t live_count = 0;

    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (idx[i].used && idx[i].slot >= 0) {
            kv_slot_t *s = &slots[idx[i].slot];
            if (s->occupied && s->valid) {
                strcpy(live[live_count].key, idx[i].key);
                memcpy(live[live_count].value, s->value, s->value_len);
                live[live_count].value_len = s->value_len;
                live[live_count].version = s->version;
                live_count++;
            }
        }
    }

    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (slots[i].occupied || slots[i].valid) {
            erase_slot(kv, i);
        }
    }

    clear_index(kv);

    for (uint16_t step = 0; step < live_count; step++) {
        uint16_t oldest_i = step;
        for (uint16_t j = step + 1; j < live_count; j++) {
            if (live[j].version < live[oldest_i].version) {
                oldest_i = j;
            }
        }
        if (oldest_i != step) {
            live_t tmp = live[step];
            live[step] = live[oldest_i];
            live[oldest_i] = tmp;
        }
    }

    for (uint16_t i = 0; i < live_count; i++) {
        uint16_t slot = select_slot_for_write(kv);
        if (slot == UINT16_MAX) {
            break;
        }
        write_slot(kv, slot, live[i].key, live[i].value, live[i].value_len, live[i].version);
        int16_t index_pos = alloc_index_entry(kv, live[i].key);
        if (index_pos >= 0) {
            INDEX(kv)[index_pos].slot = (int16_t)slot;
        }
    }
}

bool eeprom_kv_put(eeprom_kv_t *kv, const char *key, const uint8_t *value, uint16_t value_len) {
    if (kv == NULL || key == NULL || value == NULL) {
        return false;
    }
    uint16_t klen = key_len_bounded(key);
    if (klen == 0 || klen > EEPROM_KV_MAX_KEY_LEN || value_len > EEPROM_KV_MAX_VALUE_LEN) {
        return false;
    }

    uint16_t slot = select_slot_for_write(kv);
    if (slot == UINT16_MAX) {
        eeprom_kv_compact(kv);
        slot = select_slot_for_write(kv);
        if (slot == UINT16_MAX) {
            return false;
        }
    }

    int16_t idx = find_index_entry(kv, key);
    write_slot(kv, slot, key, value, value_len, kv->global_version++);

    if (idx >= 0 && INDEX(kv)[idx].slot >= 0) {
        SLOTS(kv)[INDEX(kv)[idx].slot].valid = false;
    }

    if (idx < 0) {
        idx = alloc_index_entry(kv, key);
        if (idx < 0) {
            return false;
        }
    }
    INDEX(kv)[idx].slot = (int16_t)slot;

    if (free_slot_count(kv) < ((kv->slot_count / 8) + 1)) {
        eeprom_kv_compact(kv);
    }
    return true;
}

bool eeprom_kv_get(const eeprom_kv_t *kv, const char *key, uint8_t *out, uint16_t out_cap, uint16_t *out_len) {
    if (kv == NULL || key == NULL || out == NULL || out_len == NULL) {
        return false;
    }
    int16_t idx = find_index_entry(kv, key);
    if (idx < 0 || INDEX(kv)[idx].slot < 0) {
        return false;
    }

    const kv_slot_t *slot = &SLOTS(kv)[INDEX(kv)[idx].slot];
    if (!slot->occupied || !slot->valid || slot->value_len > out_cap) {
        return false;
    }

    memcpy(out, slot->value, slot->value_len);
    *out_len = slot->value_len;
    return true;
}

bool eeprom_kv_delete(eeprom_kv_t *kv, const char *key) {
    if (kv == NULL || key == NULL) {
        return false;
    }
    int16_t idx = find_index_entry(kv, key);
    if (idx < 0) {
        return false;
    }
    if (INDEX(kv)[idx].slot >= 0) {
        SLOTS(kv)[INDEX(kv)[idx].slot].valid = false;
    }
    INDEX(kv)[idx].used = false;
    INDEX(kv)[idx].slot = -1;
    INDEX(kv)[idx].key[0] = '\0';
    return true;
}

void eeprom_kv_stats(const eeprom_kv_t *kv, eeprom_kv_stats_t *out) {
    if (kv == NULL || out == NULL) {
        return;
    }

    uint32_t max_e = 0;
    uint32_t min_e = UINT_MAX;
    uint32_t max_w = 0;
    uint32_t min_w = UINT_MAX;
    uint16_t key_count = 0;

    for (uint16_t i = 0; i < kv->slot_count; i++) {
        if (kv->erase_count[i] > max_e) max_e = kv->erase_count[i];
        if (kv->erase_count[i] < min_e) min_e = kv->erase_count[i];
        if (kv->write_count[i] > max_w) max_w = kv->write_count[i];
        if (kv->write_count[i] < min_w) min_w = kv->write_count[i];
        if (INDEX(kv)[i].used) key_count++;
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
