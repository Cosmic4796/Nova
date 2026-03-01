#include "nova.h"

static NovaValue builtin_print(Interpreter *interp, int argc, NovaValue *argv, int line) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) printf(" ");
        char *s = nova_repr(argv[i]);
        printf("%s", s);
        free(s);
    }
    printf("\n");
    return NOVA_NONE();
}

static NovaValue builtin_input(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (argc > 0) {
        char *prompt = nova_repr(argv[0]);
        printf("%s", prompt);
        fflush(stdout);
        free(prompt);
    }
    char buf[4096];
    if (fgets(buf, sizeof(buf), stdin) == NULL) {
        return NOVA_OBJ(nova_string_copy("", 0));
    }
    /* Strip trailing newline */
    int len = strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = '\0';
    return NOVA_OBJ(nova_string_copy(buf, len));
}

static NovaValue builtin_len(Interpreter *interp, int argc, NovaValue *argv, int line) {
    NovaValue v = argv[0];
    if (IS_LIST(v)) return NOVA_INT(AS_LIST(v)->count);
    if (IS_STRING(v)) return NOVA_INT(AS_STRING(v)->length);
    if (IS_DICT(v)) return NOVA_INT(AS_DICT(v)->table.count);
    nova_type_error(line, "len() expects a list, string, or dict");
    return NOVA_NONE();
}

static NovaValue builtin_str(Interpreter *interp, int argc, NovaValue *argv, int line) {
    char *s = nova_repr(argv[0]);
    ObjString *result = nova_string_copy(s, strlen(s));
    free(s);
    return NOVA_OBJ(result);
}

static NovaValue builtin_int_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    NovaValue v = argv[0];
    if (IS_INT(v)) return v;
    if (IS_FLOAT(v)) return NOVA_INT((int64_t)AS_FLOAT(v));
    if (IS_BOOL(v)) return NOVA_INT(AS_BOOL(v) ? 1 : 0);
    if (IS_STRING(v)) {
        char *endptr;
        const char *s = AS_CSTRING(v);
        long long val = strtoll(s, &endptr, 10);
        if (*endptr == '\0' && endptr != s) return NOVA_INT(val);
        nova_type_error(line, "Cannot convert '%s' to int", s);
    }
    nova_type_error(line, "Cannot convert to int");
    return NOVA_NONE();
}

static NovaValue builtin_float_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    NovaValue v = argv[0];
    if (IS_FLOAT(v)) return v;
    if (IS_INT(v)) return NOVA_FLOAT((double)AS_INT(v));
    if (IS_BOOL(v)) return NOVA_FLOAT(AS_BOOL(v) ? 1.0 : 0.0);
    if (IS_STRING(v)) {
        char *endptr;
        const char *s = AS_CSTRING(v);
        double val = strtod(s, &endptr);
        if (*endptr == '\0' && endptr != s) return NOVA_FLOAT(val);
        nova_type_error(line, "Cannot convert '%s' to float", s);
    }
    nova_type_error(line, "Cannot convert to float");
    return NOVA_NONE();
}

static NovaValue builtin_type(Interpreter *interp, int argc, NovaValue *argv, int line) {
    NovaValue v = argv[0];
    const char *t;
    switch (v.type) {
        case VAL_NONE:  t = "none"; break;
        case VAL_BOOL:  t = "bool"; break;
        case VAL_INT:   t = "int"; break;
        case VAL_FLOAT: t = "float"; break;
        case VAL_OBJ: {
            switch (AS_OBJ(v)->type) {
                case OBJ_STRING:       t = "string"; break;
                case OBJ_LIST:         t = "list"; break;
                case OBJ_DICT:         t = "dict"; break;
                case OBJ_FUNCTION:     t = "function"; break;
                case OBJ_CLASS:        t = "class"; break;
                case OBJ_INSTANCE:     t = AS_INSTANCE(v)->klass->name->chars; break;
                case OBJ_BOUND_METHOD: t = "method"; break;
                case OBJ_BUILTIN:      t = "builtin"; break;
                default: t = "unknown"; break;
            }
            break;
        }
        default: t = "unknown"; break;
    }
    return NOVA_OBJ(nova_string_copy(t, strlen(t)));
}

static NovaValue builtin_range(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int64_t start, end, step;
    if (argc == 1) {
        if (!IS_INT(argv[0]) && !IS_FLOAT(argv[0]))
            nova_type_error(line, "range() argument must be a number");
        start = 0;
        end = IS_INT(argv[0]) ? AS_INT(argv[0]) : (int64_t)AS_FLOAT(argv[0]);
        step = 1;
    } else if (argc == 2) {
        start = IS_INT(argv[0]) ? AS_INT(argv[0]) : (int64_t)AS_FLOAT(argv[0]);
        end = IS_INT(argv[1]) ? AS_INT(argv[1]) : (int64_t)AS_FLOAT(argv[1]);
        step = 1;
    } else if (argc == 3) {
        start = IS_INT(argv[0]) ? AS_INT(argv[0]) : (int64_t)AS_FLOAT(argv[0]);
        end = IS_INT(argv[1]) ? AS_INT(argv[1]) : (int64_t)AS_FLOAT(argv[1]);
        step = IS_INT(argv[2]) ? AS_INT(argv[2]) : (int64_t)AS_FLOAT(argv[2]);
        if (step == 0) nova_runtime_error(line, "range() step cannot be zero");
    } else {
        nova_runtime_error(line, "range() takes 1-3 arguments");
        return NOVA_NONE();
    }

    ObjList *list = nova_list_new();
    if (step > 0) {
        for (int64_t i = start; i < end; i += step)
            nova_list_push(list, NOVA_INT(i));
    } else {
        for (int64_t i = start; i > end; i += step)
            nova_list_push(list, NOVA_INT(i));
    }
    return NOVA_OBJ(list);
}

static NovaValue builtin_append(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "append() expects a list as first argument");
    nova_list_push(AS_LIST(argv[0]), argv[1]);
    return NOVA_NONE();
}

static NovaValue builtin_assert(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!nova_truthy(argv[0])) {
        if (argc > 1 && IS_STRING(argv[1]))
            nova_runtime_error(line, "Assertion failed: %s", AS_CSTRING(argv[1]));
        else
            nova_runtime_error(line, "Assertion failed");
    }
    return NOVA_NONE();
}

static NovaValue builtin_slice(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "slice() expects a list as first argument");
    ObjList *src = AS_LIST(argv[0]);
    if (!IS_INT(argv[1]) || !IS_INT(argv[2]))
        nova_type_error(line, "slice() start and end must be integers");
    int64_t start = AS_INT(argv[1]);
    int64_t end = AS_INT(argv[2]);
    if (start < 0) start = 0;
    if (end > src->count) end = src->count;
    ObjList *result = nova_list_new();
    for (int64_t i = start; i < end; i++)
        nova_list_push(result, src->items[i]);
    return NOVA_OBJ(result);
}

static NovaValue builtin_remove_at(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "remove_at() expects a list as first argument");
    if (!IS_INT(argv[1]))
        nova_type_error(line, "remove_at() index must be an integer");
    ObjList *list = AS_LIST(argv[0]);
    int64_t idx = AS_INT(argv[1]);
    if (idx < 0 || idx >= list->count)
        nova_runtime_error(line, "remove_at() index %lld out of range (list length %d)", idx, list->count);
    NovaValue removed = list->items[idx];
    for (int i = (int)idx; i < list->count - 1; i++)
        list->items[i] = list->items[i + 1];
    list->count--;
    return removed;
}

static NovaValue builtin_insert(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "insert() expects a list as first argument");
    if (!IS_INT(argv[1]))
        nova_type_error(line, "insert() index must be an integer");
    ObjList *list = AS_LIST(argv[0]);
    int64_t idx = AS_INT(argv[1]);
    if (idx < 0 || idx > list->count)
        nova_runtime_error(line, "insert() index %lld out of range", idx);
    /* Grow if needed */
    nova_list_push(list, NOVA_NONE()); /* adds one slot */
    /* Shift elements right */
    for (int i = list->count - 1; i > (int)idx; i--)
        list->items[i] = list->items[i - 1];
    list->items[(int)idx] = argv[2];
    return NOVA_NONE();
}

void nova_register_builtins(Interpreter *interp) {
    Environment *env = interp->globals;
    nova_env_define(env, "print", NOVA_OBJ(nova_builtin_new("print", builtin_print, -1)));
    nova_env_define(env, "input", NOVA_OBJ(nova_builtin_new("input", builtin_input, -1)));
    nova_env_define(env, "len", NOVA_OBJ(nova_builtin_new("len", builtin_len, 1)));
    nova_env_define(env, "str", NOVA_OBJ(nova_builtin_new("str", builtin_str, 1)));
    nova_env_define(env, "int", NOVA_OBJ(nova_builtin_new("int", builtin_int_fn, 1)));
    nova_env_define(env, "float", NOVA_OBJ(nova_builtin_new("float", builtin_float_fn, 1)));
    nova_env_define(env, "type", NOVA_OBJ(nova_builtin_new("type", builtin_type, 1)));
    nova_env_define(env, "range", NOVA_OBJ(nova_builtin_new("range", builtin_range, -1)));
    nova_env_define(env, "append", NOVA_OBJ(nova_builtin_new("append", builtin_append, 2)));
    nova_env_define(env, "assert", NOVA_OBJ(nova_builtin_new("assert", builtin_assert, -1)));
    nova_env_define(env, "slice", NOVA_OBJ(nova_builtin_new("slice", builtin_slice, 3)));
    nova_env_define(env, "remove_at", NOVA_OBJ(nova_builtin_new("remove_at", builtin_remove_at, 2)));
    nova_env_define(env, "insert", NOVA_OBJ(nova_builtin_new("insert", builtin_insert, 3)));
}
