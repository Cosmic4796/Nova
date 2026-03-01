#include "nova.h"

#define TABLE_MAX_LOAD 0.75

void nova_table_init(NovaTable *table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void nova_table_free(NovaTable *table) {
    if (table->entries) {
        nova_free(table->entries, sizeof(TableEntry) * table->capacity);
    }
    nova_table_init(table);
}


static TableEntry *find_entry(TableEntry *entries, int capacity, ObjString *key) {
    uint32_t index = key->hash & (capacity - 1);
    TableEntry *tombstone = NULL;

    for (;;) {
        TableEntry *entry = &entries[index];
        if (entry->key == NULL) {
            if (IS_NONE(entry->value)) {
                /* Empty entry */
                return tombstone != NULL ? tombstone : entry;
            } else {
                /* Tombstone */
                if (tombstone == NULL) tombstone = entry;
            }
        } else if (entry->key == key) {
            /* Found the key (pointer comparison — strings are interned) */
            return entry;
        }
        index = (index + 1) & (capacity - 1);
    }
}

static void adjust_capacity(NovaTable *table, int capacity) {
    TableEntry *entries = nova_alloc(sizeof(TableEntry) * capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NOVA_NONE();
    }

    /* Re-insert existing entries */
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        TableEntry *entry = &table->entries[i];
        if (entry->key == NULL) continue;

        TableEntry *dest = find_entry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++;
    }

    if (table->entries) {
        nova_free(table->entries, sizeof(TableEntry) * table->capacity);
    }
    table->entries = entries;
    table->capacity = capacity;
}

bool nova_table_get(NovaTable *table, ObjString *key, NovaValue *value) {
    if (table->count == 0) return false;

    TableEntry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

bool nova_table_set(NovaTable *table, ObjString *key, NovaValue value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = table->capacity < 8 ? 8 : table->capacity * 2;
        adjust_capacity(table, capacity);
    }

    TableEntry *entry = find_entry(table->entries, table->capacity, key);
    bool is_new = entry->key == NULL;
    if (is_new && IS_NONE(entry->value)) table->count++;

    entry->key = key;
    entry->value = value;
    return is_new;
}

bool nova_table_delete(NovaTable *table, ObjString *key) {
    if (table->count == 0) return false;

    TableEntry *entry = find_entry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    /* Place a tombstone */
    entry->key = NULL;
    entry->value = NOVA_BOOL(true);
    return true;
}

void nova_table_copy(NovaTable *from, NovaTable *to) {
    for (int i = 0; i < from->capacity; i++) {
        TableEntry *entry = &from->entries[i];
        if (entry->key != NULL) {
            nova_table_set(to, entry->key, entry->value);
        }
    }
}

ObjString *nova_table_find_string(NovaTable *table, const char *chars,
                                  int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash & (table->capacity - 1);
    for (;;) {
        TableEntry *entry = &table->entries[index];
        if (entry->key == NULL) {
            if (IS_NONE(entry->value)) return NULL;
        } else if (entry->key->length == length &&
                   entry->key->hash == hash &&
                   memcmp(entry->key->chars, chars, length) == 0) {
            return entry->key;
        }
        index = (index + 1) & (table->capacity - 1);
    }
}

void nova_table_iter_init(TableIter *iter, NovaTable *table) {
    iter->table = table;
    iter->index = 0;
}

bool nova_table_iter_next(TableIter *iter, ObjString **key, NovaValue *value) {
    while (iter->index < iter->table->capacity) {
        TableEntry *entry = &iter->table->entries[iter->index];
        iter->index++;
        if (entry->key != NULL) {
            *key = entry->key;
            *value = entry->value;
            return true;
        }
    }
    return false;
}
