#include "nova.h"

/* Global string intern table */
static NovaTable intern_table;
static bool intern_initialized = false;

static void ensure_intern_table(void) {
    if (!intern_initialized) {
        nova_table_init(&intern_table);
        intern_initialized = true;
    }
}

static uint32_t hash_string(const char *key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

static ObjString *allocate_string(const char *chars, int length, uint32_t hash) {
    ObjString *str = (ObjString*)nova_alloc(sizeof(ObjString) + length + 1);
    str->obj.type = OBJ_STRING;
    str->obj.marked = false;
    str->length = length;
    str->hash = hash;
    memcpy(str->chars, chars, length);
    str->chars[length] = '\0';

    nova_gc_register((Obj*)str);

    /* Intern it */
    ensure_intern_table();
    nova_table_set(&intern_table, str, NOVA_NONE());

    return str;
}

ObjString *nova_string_copy(const char *chars, int length) {
    uint32_t hash = hash_string(chars, length);

    ensure_intern_table();
    ObjString *interned = nova_table_find_string(&intern_table, chars, length, hash);
    if (interned != NULL) return interned;

    return allocate_string(chars, length, hash);
}

ObjString *nova_string_take(char *chars, int length) {
    uint32_t hash = hash_string(chars, length);

    ensure_intern_table();
    ObjString *interned = nova_table_find_string(&intern_table, chars, length, hash);
    if (interned != NULL) {
        free(chars);
        return interned;
    }

    ObjString *str = allocate_string(chars, length, hash);
    free(chars);
    return str;
}

ObjString *nova_string_concat(ObjString *a, ObjString *b) {
    int length = a->length + b->length;
    char *buf = malloc(length + 1);
    memcpy(buf, a->chars, a->length);
    memcpy(buf + a->length, b->chars, b->length);
    buf[length] = '\0';
    ObjString *result = nova_string_take(buf, length);
    return result;
}

ObjString *nova_string_repeat(ObjString *s, int64_t n) {
    if (n <= 0) return nova_string_copy("", 0);
    int length = s->length * (int)n;
    char *buf = malloc(length + 1);
    for (int64_t i = 0; i < n; i++) {
        memcpy(buf + i * s->length, s->chars, s->length);
    }
    buf[length] = '\0';
    return nova_string_take(buf, length);
}

ObjString *nova_string_format(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return nova_string_copy(buf, len);
}

/* List */
ObjList *nova_list_new(void) {
    ObjList *list = (ObjList*)nova_alloc(sizeof(ObjList));
    list->obj.type = OBJ_LIST;
    list->obj.marked = false;
    list->count = 0;
    list->capacity = 0;
    list->items = NULL;
    nova_gc_register((Obj*)list);
    return list;
}

void nova_list_push(ObjList *list, NovaValue value) {
    if (list->count >= list->capacity) {
        int old_cap = list->capacity;
        list->capacity = old_cap < 8 ? 8 : old_cap * 2;
        list->items = nova_realloc(list->items,
                                   sizeof(NovaValue) * old_cap,
                                   sizeof(NovaValue) * list->capacity);
    }
    list->items[list->count++] = value;
}

NovaValue nova_list_pop(ObjList *list) {
    if (list->count == 0) return NOVA_NONE();
    return list->items[--list->count];
}

/* Dict */
ObjDict *nova_dict_new(void) {
    ObjDict *dict = (ObjDict*)nova_alloc(sizeof(ObjDict));
    dict->obj.type = OBJ_DICT;
    dict->obj.marked = false;
    nova_table_init(&dict->table);
    nova_gc_register((Obj*)dict);
    return dict;
}

/* Function */
ObjFunction *nova_function_new(ObjString *name, int arity, char **params,
                               AstNode *body, Environment *closure) {
    ObjFunction *fn = (ObjFunction*)nova_alloc(sizeof(ObjFunction));
    fn->obj.type = OBJ_FUNCTION;
    fn->obj.marked = false;
    fn->name = name;
    fn->arity = arity;
    fn->params = params;
    fn->body = body;
    fn->closure = closure;
    fn->is_lambda = false;
    nova_gc_register((Obj*)fn);
    return fn;
}

/* Class */
ObjClass *nova_class_new(ObjString *name, ObjClass *parent) {
    ObjClass *klass = (ObjClass*)nova_alloc(sizeof(ObjClass));
    klass->obj.type = OBJ_CLASS;
    klass->obj.marked = false;
    klass->name = name;
    klass->parent = parent;
    nova_table_init(&klass->methods);
    nova_gc_register((Obj*)klass);
    return klass;
}

ObjFunction *nova_class_find_method(ObjClass *klass, ObjString *name) {
    NovaValue method;
    if (nova_table_get(&klass->methods, name, &method)) {
        return AS_FUNCTION(method);
    }
    if (klass->parent != NULL) {
        return nova_class_find_method(klass->parent, name);
    }
    return NULL;
}

/* Instance */
ObjInstance *nova_instance_new(ObjClass *klass) {
    ObjInstance *inst = (ObjInstance*)nova_alloc(sizeof(ObjInstance));
    inst->obj.type = OBJ_INSTANCE;
    inst->obj.marked = false;
    inst->klass = klass;
    nova_table_init(&inst->fields);
    nova_gc_register((Obj*)inst);
    return inst;
}

NovaValue nova_instance_get(ObjInstance *inst, const char *name, int line) {
    ObjString *key = nova_string_copy(name, strlen(name));
    NovaValue value;
    if (nova_table_get(&inst->fields, key, &value)) {
        return value;
    }
    ObjFunction *method = nova_class_find_method(inst->klass, key);
    if (method != NULL) {
        return NOVA_OBJ(nova_bound_method_new(inst, method));
    }
    nova_runtime_error(line, "'%s' instance has no attribute '%s'",
                       inst->klass->name->chars, name);
    return NOVA_NONE(); /* unreachable */
}

void nova_instance_set(ObjInstance *inst, const char *name, NovaValue value) {
    ObjString *key = nova_string_copy(name, strlen(name));
    nova_table_set(&inst->fields, key, value);
}

/* Bound Method */
ObjBoundMethod *nova_bound_method_new(ObjInstance *instance, ObjFunction *method) {
    ObjBoundMethod *bm = (ObjBoundMethod*)nova_alloc(sizeof(ObjBoundMethod));
    bm->obj.type = OBJ_BOUND_METHOD;
    bm->obj.marked = false;
    bm->instance = instance;
    bm->method = method;
    nova_gc_register((Obj*)bm);
    return bm;
}

/* Builtin */
ObjBuiltin *nova_builtin_new(const char *name, BuiltinFn fn, int arity) {
    ObjBuiltin *b = (ObjBuiltin*)nova_alloc(sizeof(ObjBuiltin));
    b->obj.type = OBJ_BUILTIN;
    b->obj.marked = false;
    b->name = name;
    b->fn = fn;
    b->arity = arity;
    nova_gc_register((Obj*)b);
    return b;
}
