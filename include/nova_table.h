#ifndef NOVA_TABLE_H
#define NOVA_TABLE_H

#include "nova_common.h"
#include "nova_value.h"

typedef struct {
    ObjString *key;
    NovaValue value;
} TableEntry;

struct NovaTable {
    int count;
    int capacity;
    TableEntry *entries;
};

void nova_table_init(NovaTable *table);
void nova_table_free(NovaTable *table);
bool nova_table_get(NovaTable *table, ObjString *key, NovaValue *value);
bool nova_table_set(NovaTable *table, ObjString *key, NovaValue value);
bool nova_table_delete(NovaTable *table, ObjString *key);
void nova_table_copy(NovaTable *from, NovaTable *to);

/* String interning */
ObjString *nova_table_find_string(NovaTable *table, const char *chars,
                                  int length, uint32_t hash);

/* Iteration support */
typedef struct {
    NovaTable *table;
    int index;
} TableIter;

void nova_table_iter_init(TableIter *iter, NovaTable *table);
bool nova_table_iter_next(TableIter *iter, ObjString **key, NovaValue *value);

#endif
