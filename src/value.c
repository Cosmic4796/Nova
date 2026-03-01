#include "nova.h"

char *nova_repr(NovaValue value) {
    char buf[256];
    switch (value.type) {
        case VAL_NONE:
            return strdup("none");
        case VAL_BOOL:
            return strdup(AS_BOOL(value) ? "true" : "false");
        case VAL_INT:
            snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(value));
            return strdup(buf);
        case VAL_FLOAT: {
            double d = AS_FLOAT(value);
            /* If the float is an exact integer, print without decimal */
            if (d == (double)(int64_t)d && d >= -1e15 && d <= 1e15) {
                snprintf(buf, sizeof(buf), "%lld", (long long)(int64_t)d);
            } else {
                /* Match Python's repr: use shortest representation that round-trips */
                snprintf(buf, sizeof(buf), "%.16g", d);
                /* If that doesn't round-trip, use more digits */
                double check;
                sscanf(buf, "%lf", &check);
                if (check != d) {
                    snprintf(buf, sizeof(buf), "%.17g", d);
                }
            }
            return strdup(buf);
        }
        case VAL_OBJ: {
            Obj *obj = AS_OBJ(value);
            switch (obj->type) {
                case OBJ_STRING:
                    return strdup(((ObjString*)obj)->chars);
                case OBJ_LIST: {
                    ObjList *list = (ObjList*)obj;
                    /* Build [a, b, c] representation */
                    size_t total = 2; /* [] */
                    char **reprs = malloc(sizeof(char*) * list->count);
                    for (int i = 0; i < list->count; i++) {
                        reprs[i] = nova_repr(list->items[i]);
                        total += strlen(reprs[i]);
                        if (i > 0) total += 2; /* ", " */
                    }
                    char *result = malloc(total + 1);
                    char *p = result;
                    *p++ = '[';
                    for (int i = 0; i < list->count; i++) {
                        if (i > 0) { *p++ = ','; *p++ = ' '; }
                        int len = strlen(reprs[i]);
                        memcpy(p, reprs[i], len);
                        p += len;
                        free(reprs[i]);
                    }
                    *p++ = ']';
                    *p = '\0';
                    free(reprs);
                    return result;
                }
                case OBJ_DICT: {
                    ObjDict *dict = (ObjDict*)obj;
                    /* Build {key: val, ...} */
                    size_t total = 2;
                    int count = 0;
                    char **parts = NULL;

                    TableIter iter;
                    ObjString *key;
                    NovaValue val;
                    nova_table_iter_init(&iter, &dict->table);
                    while (nova_table_iter_next(&iter, &key, &val)) {
                        count++;
                        parts = realloc(parts, sizeof(char*) * count);
                        char *kr = nova_repr(NOVA_OBJ(key));
                        char *vr = nova_repr(val);
                        int len = strlen(kr) + strlen(vr) + 4;
                        parts[count-1] = malloc(len + 1);
                        snprintf(parts[count-1], len + 1, "%s: %s", kr, vr);
                        total += strlen(parts[count-1]);
                        if (count > 1) total += 2;
                        free(kr);
                        free(vr);
                    }
                    char *result = malloc(total + 1);
                    char *p = result;
                    *p++ = '{';
                    for (int i = 0; i < count; i++) {
                        if (i > 0) { *p++ = ','; *p++ = ' '; }
                        int len = strlen(parts[i]);
                        memcpy(p, parts[i], len);
                        p += len;
                        free(parts[i]);
                    }
                    *p++ = '}';
                    *p = '\0';
                    free(parts);
                    return result;
                }
                case OBJ_FUNCTION: {
                    ObjFunction *fn = (ObjFunction*)obj;
                    snprintf(buf, sizeof(buf), "<func %s>", fn->name->chars);
                    return strdup(buf);
                }
                case OBJ_CLASS: {
                    ObjClass *klass = (ObjClass*)obj;
                    snprintf(buf, sizeof(buf), "<class %s>", klass->name->chars);
                    return strdup(buf);
                }
                case OBJ_INSTANCE: {
                    ObjInstance *inst = (ObjInstance*)obj;
                    snprintf(buf, sizeof(buf), "<%s instance>",
                             inst->klass->name->chars);
                    return strdup(buf);
                }
                case OBJ_BOUND_METHOD: {
                    ObjBoundMethod *bm = (ObjBoundMethod*)obj;
                    snprintf(buf, sizeof(buf), "<bound method %s>",
                             bm->method->name->chars);
                    return strdup(buf);
                }
                case OBJ_BUILTIN: {
                    ObjBuiltin *b = (ObjBuiltin*)obj;
                    snprintf(buf, sizeof(buf), "<builtin %s>", b->name);
                    return strdup(buf);
                }
            }
        }
    }
    return strdup("<unknown>");
}

char *nova_display(NovaValue value) {
    if (IS_STRING(value)) {
        ObjString *s = AS_STRING(value);
        char *buf = malloc(s->length + 3);
        buf[0] = '"';
        memcpy(buf + 1, s->chars, s->length);
        buf[s->length + 1] = '"';
        buf[s->length + 2] = '\0';
        return buf;
    }
    return nova_repr(value);
}

bool nova_truthy(NovaValue value) {
    switch (value.type) {
        case VAL_NONE:  return false;
        case VAL_BOOL:  return AS_BOOL(value);
        case VAL_INT:   return AS_INT(value) != 0;
        case VAL_FLOAT: return AS_FLOAT(value) != 0.0;
        case VAL_OBJ: {
            Obj *obj = AS_OBJ(value);
            if (obj->type == OBJ_STRING)
                return ((ObjString*)obj)->length > 0;
            if (obj->type == OBJ_LIST)
                return ((ObjList*)obj)->count > 0;
            return true;
        }
    }
    return true;
}

bool nova_equal(NovaValue a, NovaValue b) {
    if (a.type != b.type) {
        /* Allow int/float comparison */
        if (IS_NUMBER(a) && IS_NUMBER(b)) {
            return AS_NUMBER(a) == AS_NUMBER(b);
        }
        return false;
    }
    switch (a.type) {
        case VAL_NONE:  return true;
        case VAL_BOOL:  return AS_BOOL(a) == AS_BOOL(b);
        case VAL_INT:   return AS_INT(a) == AS_INT(b);
        case VAL_FLOAT: return AS_FLOAT(a) == AS_FLOAT(b);
        case VAL_OBJ:   return AS_OBJ(a) == AS_OBJ(b); /* interned strings: ptr cmp */
    }
    return false;
}

void nova_print_value(NovaValue value) {
    char *s = nova_repr(value);
    printf("%s", s);
    free(s);
}
