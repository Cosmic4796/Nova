#ifndef NOVA_MEMORY_H
#define NOVA_MEMORY_H

#include "nova_common.h"
#include "nova_value.h"

/* Allocation */
void *nova_alloc(size_t size);
void *nova_realloc(void *ptr, size_t old_size, size_t new_size);
void  nova_free(void *ptr, size_t size);

/* GC */
void nova_gc_init(void);
void nova_gc_shutdown(void);
void nova_gc_register(Obj *obj);
void nova_gc_collect(void);
void nova_gc_mark_value(NovaValue value);
void nova_gc_mark_object(Obj *obj);
void nova_gc_mark_table(NovaTable *table);

/* Track allocated bytes */
extern size_t nova_bytes_allocated;
extern size_t nova_gc_threshold;
extern Obj   *nova_gc_objects;      /* linked list of all heap objects */

/* GC stress test mode */
extern bool nova_gc_stress;

#endif
