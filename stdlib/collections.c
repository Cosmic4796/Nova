#include "nova.h"
#include <stdlib.h>

/* ── map(list, func) ── */
static NovaValue coll_map(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "map() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);
    ObjList *result = nova_list_new();

    for (int i = 0; i < src->count; i++) {
        NovaValue args[1] = { src->items[i] };
        NovaValue val = nova_call_value(interp, argv[1], 1, args, line);
        nova_list_push(result, val);
    }
    return NOVA_OBJ(result);
}

/* ── filter(list, func) ── */
static NovaValue coll_filter(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "filter() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);
    ObjList *result = nova_list_new();

    for (int i = 0; i < src->count; i++) {
        NovaValue args[1] = { src->items[i] };
        NovaValue test = nova_call_value(interp, argv[1], 1, args, line);
        bool truthy = false;
        if (IS_BOOL(test)) truthy = AS_BOOL(test);
        else if (IS_NONE(test)) truthy = false;
        else if (IS_INT(test)) truthy = AS_INT(test) != 0;
        else if (IS_FLOAT(test)) truthy = AS_FLOAT(test) != 0.0;
        else if (IS_STRING(test)) truthy = AS_STRING(test)->length > 0;
        else if (IS_LIST(test)) truthy = AS_LIST(test)->count > 0;
        else truthy = true;
        if (truthy) nova_list_push(result, src->items[i]);
    }
    return NOVA_OBJ(result);
}

/* ── reduce(list, func, initial) ── */
static NovaValue coll_reduce(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "reduce() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);
    NovaValue acc = argv[2];

    for (int i = 0; i < src->count; i++) {
        NovaValue args[2] = { acc, src->items[i] };
        acc = nova_call_value(interp, argv[1], 2, args, line);
    }
    return acc;
}

/* ── sort helpers ── */
static double sort_as_number(NovaValue v) {
    if (IS_INT(v)) return (double)AS_INT(v);
    if (IS_FLOAT(v)) return AS_FLOAT(v);
    return 0.0;
}

static int sort_compare(const void *a, const void *b) {
    NovaValue va = *(const NovaValue *)a;
    NovaValue vb = *(const NovaValue *)b;

    bool a_num = IS_INT(va) || IS_FLOAT(va);
    bool b_num = IS_INT(vb) || IS_FLOAT(vb);

    /* Numbers come before strings */
    if (a_num && !b_num) return -1;
    if (!a_num && b_num) return 1;

    if (a_num && b_num) {
        double da = sort_as_number(va);
        double db = sort_as_number(vb);
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }

    if (IS_STRING(va) && IS_STRING(vb))
        return strcmp(AS_CSTRING(va), AS_CSTRING(vb));

    return 0;
}

/* ── sort(list) ── */
static NovaValue coll_sort(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "sort() expects a list");
    ObjList *src = AS_LIST(argv[0]);
    ObjList *result = nova_list_new();

    for (int i = 0; i < src->count; i++)
        nova_list_push(result, src->items[i]);

    if (result->count > 1)
        qsort(result->items, result->count, sizeof(NovaValue), sort_compare);

    return NOVA_OBJ(result);
}

/* ── sort_by(list, func) ── */
static NovaValue coll_sort_by(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "sort_by() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);
    int n = src->count;

    /* Build array of {key, original_value} and sort by key */
    typedef struct { NovaValue key; NovaValue val; } KeyVal;
    KeyVal *pairs = malloc(sizeof(KeyVal) * (n > 0 ? n : 1));

    for (int i = 0; i < n; i++) {
        NovaValue args[1] = { src->items[i] };
        pairs[i].key = nova_call_value(interp, argv[1], 1, args, line);
        pairs[i].val = src->items[i];
    }

    /* Simple insertion sort (safe — no static state needed) */
    for (int i = 1; i < n; i++) {
        KeyVal tmp = pairs[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = sort_compare(&pairs[j].key, &tmp.key);
            if (cmp <= 0) break;
            pairs[j + 1] = pairs[j];
            j--;
        }
        pairs[j + 1] = tmp;
    }

    ObjList *result = nova_list_new();
    for (int i = 0; i < n; i++)
        nova_list_push(result, pairs[i].val);

    free(pairs);
    return NOVA_OBJ(result);
}

/* ── reverse(list) ── */
static NovaValue coll_reverse(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "reverse() expects a list");
    ObjList *src = AS_LIST(argv[0]);
    ObjList *result = nova_list_new();

    for (int i = src->count - 1; i >= 0; i--)
        nova_list_push(result, src->items[i]);

    return NOVA_OBJ(result);
}

/* ── zip(list1, list2) ── */
static NovaValue coll_zip(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]) || !IS_LIST(argv[1]))
        nova_type_error(line, "zip() expects two lists");
    ObjList *a = AS_LIST(argv[0]);
    ObjList *b = AS_LIST(argv[1]);
    int len = a->count < b->count ? a->count : b->count;

    ObjList *result = nova_list_new();
    for (int i = 0; i < len; i++) {
        ObjList *pair = nova_list_new();
        nova_list_push(pair, a->items[i]);
        nova_list_push(pair, b->items[i]);
        nova_list_push(result, NOVA_OBJ(pair));
    }
    return NOVA_OBJ(result);
}

/* ── enumerate(list) ── */
static NovaValue coll_enumerate(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "enumerate() expects a list");
    ObjList *src = AS_LIST(argv[0]);
    ObjList *result = nova_list_new();

    for (int i = 0; i < src->count; i++) {
        ObjList *pair = nova_list_new();
        nova_list_push(pair, NOVA_INT(i));
        nova_list_push(pair, src->items[i]);
        nova_list_push(result, NOVA_OBJ(pair));
    }
    return NOVA_OBJ(result);
}

/* ── find(list, func) ── */
static NovaValue coll_find(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "find() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);

    for (int i = 0; i < src->count; i++) {
        NovaValue args[1] = { src->items[i] };
        NovaValue test = nova_call_value(interp, argv[1], 1, args, line);
        bool truthy = false;
        if (IS_BOOL(test)) truthy = AS_BOOL(test);
        else if (IS_NONE(test)) truthy = false;
        else if (IS_INT(test)) truthy = AS_INT(test) != 0;
        else if (IS_FLOAT(test)) truthy = AS_FLOAT(test) != 0.0;
        else if (IS_STRING(test)) truthy = AS_STRING(test)->length > 0;
        else if (IS_LIST(test)) truthy = AS_LIST(test)->count > 0;
        else truthy = true;
        if (truthy) return src->items[i];
    }
    return NOVA_NONE();
}

/* ── every(list, func) ── */
static NovaValue coll_every(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "every() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);

    for (int i = 0; i < src->count; i++) {
        NovaValue args[1] = { src->items[i] };
        NovaValue test = nova_call_value(interp, argv[1], 1, args, line);
        bool truthy = false;
        if (IS_BOOL(test)) truthy = AS_BOOL(test);
        else if (IS_NONE(test)) truthy = false;
        else if (IS_INT(test)) truthy = AS_INT(test) != 0;
        else if (IS_FLOAT(test)) truthy = AS_FLOAT(test) != 0.0;
        else if (IS_STRING(test)) truthy = AS_STRING(test)->length > 0;
        else if (IS_LIST(test)) truthy = AS_LIST(test)->count > 0;
        else truthy = true;
        if (!truthy) return NOVA_BOOL(false);
    }
    return NOVA_BOOL(true);
}

/* ── some(list, func) ── */
static NovaValue coll_some(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "some() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);

    for (int i = 0; i < src->count; i++) {
        NovaValue args[1] = { src->items[i] };
        NovaValue test = nova_call_value(interp, argv[1], 1, args, line);
        bool truthy = false;
        if (IS_BOOL(test)) truthy = AS_BOOL(test);
        else if (IS_NONE(test)) truthy = false;
        else if (IS_INT(test)) truthy = AS_INT(test) != 0;
        else if (IS_FLOAT(test)) truthy = AS_FLOAT(test) != 0.0;
        else if (IS_STRING(test)) truthy = AS_STRING(test)->length > 0;
        else if (IS_LIST(test)) truthy = AS_LIST(test)->count > 0;
        else truthy = true;
        if (truthy) return NOVA_BOOL(true);
    }
    return NOVA_BOOL(false);
}

/* ── flat(list) ── */
static NovaValue coll_flat(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "flat() expects a list");
    ObjList *src = AS_LIST(argv[0]);
    ObjList *result = nova_list_new();

    for (int i = 0; i < src->count; i++) {
        if (IS_LIST(src->items[i])) {
            ObjList *inner = AS_LIST(src->items[i]);
            for (int j = 0; j < inner->count; j++)
                nova_list_push(result, inner->items[j]);
        } else {
            nova_list_push(result, src->items[i]);
        }
    }
    return NOVA_OBJ(result);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "map",       NOVA_OBJ(nova_builtin_new("map",       coll_map,       2)));
    nova_env_define(env, "filter",    NOVA_OBJ(nova_builtin_new("filter",    coll_filter,    2)));
    nova_env_define(env, "reduce",    NOVA_OBJ(nova_builtin_new("reduce",    coll_reduce,    3)));
    nova_env_define(env, "sort",      NOVA_OBJ(nova_builtin_new("sort",      coll_sort,      1)));
    nova_env_define(env, "sort_by",   NOVA_OBJ(nova_builtin_new("sort_by",   coll_sort_by,   2)));
    nova_env_define(env, "reverse",   NOVA_OBJ(nova_builtin_new("reverse",   coll_reverse,   1)));
    nova_env_define(env, "zip",       NOVA_OBJ(nova_builtin_new("zip",       coll_zip,       2)));
    nova_env_define(env, "enumerate", NOVA_OBJ(nova_builtin_new("enumerate", coll_enumerate, 1)));
    nova_env_define(env, "find",      NOVA_OBJ(nova_builtin_new("find",      coll_find,      2)));
    nova_env_define(env, "every",     NOVA_OBJ(nova_builtin_new("every",     coll_every,     2)));
    nova_env_define(env, "some",      NOVA_OBJ(nova_builtin_new("some",      coll_some,      2)));
    nova_env_define(env, "flat",      NOVA_OBJ(nova_builtin_new("flat",      coll_flat,      1)));
}
