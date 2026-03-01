#include "nova.h"

size_t nova_bytes_allocated = 0;
size_t nova_gc_threshold = 1024 * 1024;  /* 1 MB initial threshold */
Obj   *nova_gc_objects = NULL;
bool   nova_gc_stress = false;

/* Track all environments for GC marking */
static Environment **env_roots = NULL;
static int env_root_count = 0;
static int env_root_capacity = 0;

void *nova_alloc(size_t size) {
    nova_bytes_allocated += size;
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return ptr;
}

void *nova_realloc(void *ptr, size_t old_size, size_t new_size) {
    nova_bytes_allocated += new_size - old_size;
    void *result = realloc(ptr, new_size);
    if (result == NULL && new_size > 0) {
        fprintf(stderr, "Out of memory\n");
        exit(1);
    }
    return result;
}

void nova_free(void *ptr, size_t size) {
    nova_bytes_allocated -= size;
    free(ptr);
}

void nova_gc_init(void) {
    nova_gc_objects = NULL;
    nova_bytes_allocated = 0;
}

void nova_gc_register(Obj *obj) {
    obj->next = nova_gc_objects;
    obj->marked = false;
    nova_gc_objects = obj;
}

void nova_gc_mark_value(NovaValue value) {
    if (IS_OBJ(value)) {
        nova_gc_mark_object(AS_OBJ(value));
    }
}

static void mark_table(NovaTable *table) {
    for (int i = 0; i < table->capacity; i++) {
        TableEntry *entry = &table->entries[i];
        if (entry->key != NULL) {
            nova_gc_mark_object((Obj*)entry->key);
            nova_gc_mark_value(entry->value);
        }
    }
}

static void mark_environment(Environment *env) {
    while (env != NULL) {
        if (env->marked) return;
        env->marked = true;
        mark_table(&env->values);
        env = env->parent;
    }
}

void nova_gc_mark_object(Obj *obj) {
    if (obj == NULL || obj->marked) return;
    obj->marked = true;

    switch (obj->type) {
        case OBJ_STRING:
            break;
        case OBJ_LIST: {
            ObjList *list = (ObjList*)obj;
            for (int i = 0; i < list->count; i++) {
                nova_gc_mark_value(list->items[i]);
            }
            break;
        }
        case OBJ_DICT: {
            ObjDict *dict = (ObjDict*)obj;
            mark_table(&dict->table);
            break;
        }
        case OBJ_FUNCTION: {
            ObjFunction *fn = (ObjFunction*)obj;
            nova_gc_mark_object((Obj*)fn->name);
            if (fn->closure) mark_environment(fn->closure);
            break;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            nova_gc_mark_object((Obj*)klass->name);
            if (klass->parent) nova_gc_mark_object((Obj*)klass->parent);
            mark_table(&klass->methods);
            break;
        }
        case OBJ_INSTANCE: {
            ObjInstance *inst = (ObjInstance*)obj;
            nova_gc_mark_object((Obj*)inst->klass);
            mark_table(&inst->fields);
            break;
        }
        case OBJ_BOUND_METHOD: {
            ObjBoundMethod *bm = (ObjBoundMethod*)obj;
            nova_gc_mark_object((Obj*)bm->instance);
            nova_gc_mark_object((Obj*)bm->method);
            break;
        }
        case OBJ_BUILTIN:
            break;
    }
}

void nova_gc_mark_table(NovaTable *table) {
    mark_table(table);
}

static void free_object(Obj *obj) {
    switch (obj->type) {
        case OBJ_STRING: {
            ObjString *s = (ObjString*)obj;
            nova_free(obj, sizeof(ObjString) + s->length + 1);
            return;
        }
        case OBJ_LIST: {
            ObjList *list = (ObjList*)obj;
            if (list->items) {
                nova_free(list->items, sizeof(NovaValue) * list->capacity);
            }
            nova_free(obj, sizeof(ObjList));
            return;
        }
        case OBJ_DICT: {
            ObjDict *dict = (ObjDict*)obj;
            nova_table_free(&dict->table);
            nova_free(obj, sizeof(ObjDict));
            return;
        }
        case OBJ_FUNCTION: {
            ObjFunction *fn = (ObjFunction*)obj;
            if (fn->params) {
                /* Params are arena-allocated strings, don't free them */
                nova_free(fn->params, sizeof(char*) * fn->arity);
            }
            nova_free(obj, sizeof(ObjFunction));
            return;
        }
        case OBJ_CLASS: {
            ObjClass *klass = (ObjClass*)obj;
            nova_table_free(&klass->methods);
            nova_free(obj, sizeof(ObjClass));
            return;
        }
        case OBJ_INSTANCE: {
            ObjInstance *inst = (ObjInstance*)obj;
            nova_table_free(&inst->fields);
            nova_free(obj, sizeof(ObjInstance));
            return;
        }
        case OBJ_BOUND_METHOD: {
            nova_free(obj, sizeof(ObjBoundMethod));
            return;
        }
        case OBJ_BUILTIN: {
            nova_free(obj, sizeof(ObjBuiltin));
            return;
        }
    }
}

void nova_gc_collect(void) {
    /* Sweep: free all unmarked objects */
    Obj **obj = &nova_gc_objects;
    while (*obj) {
        if (!(*obj)->marked) {
            Obj *unreached = *obj;
            *obj = unreached->next;
            free_object(unreached);
        } else {
            (*obj)->marked = false;
            obj = &(*obj)->next;
        }
    }
}

void nova_gc_shutdown(void) {
    /* Free all remaining objects */
    Obj *obj = nova_gc_objects;
    while (obj) {
        Obj *next = obj->next;
        free_object(obj);
        obj = next;
    }
    nova_gc_objects = NULL;
    if (env_roots) {
        free(env_roots);
        env_roots = NULL;
        env_root_count = 0;
        env_root_capacity = 0;
    }
}
