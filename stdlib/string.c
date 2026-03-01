#include "nova.h"
#include <ctype.h>

static NovaValue str_replace(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]) || !IS_STRING(argv[2]))
        nova_type_error(line, "replace() expects three strings");
    const char *src = AS_STRING(argv[0])->chars;
    const char *old = AS_STRING(argv[1])->chars;
    const char *new = AS_STRING(argv[2])->chars;
    int old_len = AS_STRING(argv[1])->length;
    int new_len = AS_STRING(argv[2])->length;

    if (old_len == 0) return argv[0];

    /* Count occurrences */
    int count = 0;
    const char *p = src;
    while ((p = strstr(p, old)) != NULL) { count++; p += old_len; }

    int src_len = AS_STRING(argv[0])->length;
    int result_len = src_len + count * (new_len - old_len);
    char *result = malloc(result_len + 1);
    char *dst = result;
    p = src;
    const char *found;
    while ((found = strstr(p, old)) != NULL) {
        int chunk = found - p;
        memcpy(dst, p, chunk);
        dst += chunk;
        memcpy(dst, new, new_len);
        dst += new_len;
        p = found + old_len;
    }
    strcpy(dst, p);
    return NOVA_OBJ(nova_string_take(result, result_len));
}

static NovaValue str_starts_with(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "starts_with() expects two strings");
    ObjString *s = AS_STRING(argv[0]);
    ObjString *prefix = AS_STRING(argv[1]);
    if (prefix->length > s->length) return NOVA_BOOL(false);
    return NOVA_BOOL(memcmp(s->chars, prefix->chars, prefix->length) == 0);
}

static NovaValue str_ends_with(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "ends_with() expects two strings");
    ObjString *s = AS_STRING(argv[0]);
    ObjString *suffix = AS_STRING(argv[1]);
    if (suffix->length > s->length) return NOVA_BOOL(false);
    return NOVA_BOOL(memcmp(s->chars + s->length - suffix->length,
                            suffix->chars, suffix->length) == 0);
}

static NovaValue str_trim(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "trim() expects a string");
    const char *s = AS_STRING(argv[0])->chars;
    int len = AS_STRING(argv[0])->length;
    int start = 0, end = len - 1;
    while (start < len && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end])) end--;
    int new_len = end - start + 1;
    if (new_len <= 0) return NOVA_OBJ(nova_string_copy("", 0));
    return NOVA_OBJ(nova_string_copy(s + start, new_len));
}

static NovaValue str_join(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_LIST(argv[1]))
        nova_type_error(line, "join() expects a separator string and a list");
    ObjString *sep = AS_STRING(argv[0]);
    ObjList *list = AS_LIST(argv[1]);

    if (list->count == 0) return NOVA_OBJ(nova_string_copy("", 0));

    /* Calculate total length */
    int total = 0;
    for (int i = 0; i < list->count; i++) {
        if (!IS_STRING(list->items[i]))
            nova_type_error(line, "join() list must contain only strings");
        total += AS_STRING(list->items[i])->length;
    }
    total += sep->length * (list->count - 1);

    char *result = malloc(total + 1);
    char *dst = result;
    for (int i = 0; i < list->count; i++) {
        if (i > 0) { memcpy(dst, sep->chars, sep->length); dst += sep->length; }
        ObjString *item = AS_STRING(list->items[i]);
        memcpy(dst, item->chars, item->length);
        dst += item->length;
    }
    *dst = '\0';
    return NOVA_OBJ(nova_string_take(result, total));
}

static NovaValue str_contains(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "contains() expects two strings");
    return NOVA_BOOL(strstr(AS_STRING(argv[0])->chars, AS_STRING(argv[1])->chars) != NULL);
}

static NovaValue str_char_at(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_INT(argv[1]))
        nova_type_error(line, "char_at() expects a string and an integer");
    ObjString *s = AS_STRING(argv[0]);
    int64_t idx = AS_INT(argv[1]);
    if (idx < 0 || idx >= s->length)
        nova_runtime_error(line, "char_at() index %lld out of range [0, %d)", idx, s->length);
    return NOVA_OBJ(nova_string_copy(s->chars + idx, 1));
}

static NovaValue str_to_upper(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "to_upper() expects a string");
    ObjString *s = AS_STRING(argv[0]);
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++)
        result[i] = toupper((unsigned char)s->chars[i]);
    result[s->length] = '\0';
    return NOVA_OBJ(nova_string_take(result, s->length));
}

static NovaValue str_to_lower(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "to_lower() expects a string");
    ObjString *s = AS_STRING(argv[0]);
    char *result = malloc(s->length + 1);
    for (int i = 0; i < s->length; i++)
        result[i] = tolower((unsigned char)s->chars[i]);
    result[s->length] = '\0';
    return NOVA_OBJ(nova_string_take(result, s->length));
}

static NovaValue str_substr(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_INT(argv[1]) || !IS_INT(argv[2]))
        nova_type_error(line, "substr() expects a string, start index, and length");
    ObjString *s = AS_STRING(argv[0]);
    int64_t start = AS_INT(argv[1]);
    int64_t len = AS_INT(argv[2]);
    if (start < 0) start = 0;
    if (start >= s->length) return NOVA_OBJ(nova_string_copy("", 0));
    if (start + len > s->length) len = s->length - start;
    return NOVA_OBJ(nova_string_copy(s->chars + start, (int)len));
}

static NovaValue str_split(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "split_str() expects two strings");
    const char *src = AS_STRING(argv[0])->chars;
    const char *sep = AS_STRING(argv[1])->chars;
    int sep_len = AS_STRING(argv[1])->length;

    ObjList *list = nova_list_new();
    if (sep_len == 0) {
        /* Split into individual characters */
        ObjString *s = AS_STRING(argv[0]);
        for (int i = 0; i < s->length; i++)
            nova_list_push(list, NOVA_OBJ(nova_string_copy(s->chars + i, 1)));
        return NOVA_OBJ(list);
    }

    const char *p = src;
    const char *found;
    while ((found = strstr(p, sep)) != NULL) {
        nova_list_push(list, NOVA_OBJ(nova_string_copy(p, (int)(found - p))));
        p = found + sep_len;
    }
    nova_list_push(list, NOVA_OBJ(nova_string_copy(p, strlen(p))));
    return NOVA_OBJ(list);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "replace",     NOVA_OBJ(nova_builtin_new("replace",     str_replace,     3)));
    nova_env_define(env, "starts_with", NOVA_OBJ(nova_builtin_new("starts_with", str_starts_with, 2)));
    nova_env_define(env, "ends_with",   NOVA_OBJ(nova_builtin_new("ends_with",   str_ends_with,   2)));
    nova_env_define(env, "trim",        NOVA_OBJ(nova_builtin_new("trim",        str_trim,        1)));
    nova_env_define(env, "join",        NOVA_OBJ(nova_builtin_new("join",        str_join,        2)));
    nova_env_define(env, "contains",    NOVA_OBJ(nova_builtin_new("contains",    str_contains,    2)));
    nova_env_define(env, "char_at",     NOVA_OBJ(nova_builtin_new("char_at",     str_char_at,     2)));
    nova_env_define(env, "to_upper",    NOVA_OBJ(nova_builtin_new("to_upper",    str_to_upper,    1)));
    nova_env_define(env, "to_lower",    NOVA_OBJ(nova_builtin_new("to_lower",    str_to_lower,    1)));
    nova_env_define(env, "substr",      NOVA_OBJ(nova_builtin_new("substr",      str_substr,      3)));
    nova_env_define(env, "split_str",   NOVA_OBJ(nova_builtin_new("split_str",   str_split,       2)));
}
