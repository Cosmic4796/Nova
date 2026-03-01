#include "nova.h"
#include <regex.h>
#include <string.h>
#include <stdlib.h>

/* ── match(pattern, string) ── first match as dict {match, start, end} or none */
static NovaValue regex_match_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "match() expects two strings (pattern, string)");

    const char *pattern = AS_CSTRING(argv[0]);
    const char *str = AS_CSTRING(argv[1]);

    regex_t reg;
    int ret = regcomp(&reg, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &reg, errbuf, sizeof(errbuf));
        nova_runtime_error(line, "match() invalid regex: %s", errbuf);
    }

    regmatch_t pmatch[1];
    ret = regexec(&reg, str, 1, pmatch, 0);
    regfree(&reg);

    if (ret == REG_NOMATCH) return NOVA_NONE();

    int start = pmatch[0].rm_so;
    int end = pmatch[0].rm_eo;
    ObjDict *dict = nova_dict_new();
    nova_table_set(&dict->table, nova_string_copy("match", 5),
                   NOVA_OBJ(nova_string_copy(str + start, end - start)));
    nova_table_set(&dict->table, nova_string_copy("start", 5), NOVA_INT(start));
    nova_table_set(&dict->table, nova_string_copy("end", 3), NOVA_INT(end));
    return NOVA_OBJ(dict);
}

/* ── find_all(pattern, string) ── list of all match strings */
static NovaValue regex_find_all(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "find_all() expects two strings (pattern, string)");

    const char *pattern = AS_CSTRING(argv[0]);
    const char *str = AS_CSTRING(argv[1]);

    regex_t reg;
    int ret = regcomp(&reg, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &reg, errbuf, sizeof(errbuf));
        nova_runtime_error(line, "find_all() invalid regex: %s", errbuf);
    }

    ObjList *results = nova_list_new();
    regmatch_t pmatch[1];
    const char *cursor = str;

    while (regexec(&reg, cursor, 1, pmatch, 0) == 0) {
        int start = pmatch[0].rm_so;
        int end = pmatch[0].rm_eo;
        if (start == end) { cursor++; continue; } /* Prevent infinite loop on zero-width match */
        nova_list_push(results, NOVA_OBJ(nova_string_copy(cursor + start, end - start)));
        cursor += end;
    }

    regfree(&reg);
    return NOVA_OBJ(results);
}

/* ── replace(pattern, string, replacement) ── regex replace */
static NovaValue regex_replace_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]) || !IS_STRING(argv[2]))
        nova_type_error(line, "replace() expects three strings (pattern, string, replacement)");

    const char *pattern = AS_CSTRING(argv[0]);
    const char *str = AS_CSTRING(argv[1]);
    const char *repl = AS_CSTRING(argv[2]);
    int repl_len = AS_STRING(argv[2])->length;

    regex_t reg;
    int ret = regcomp(&reg, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &reg, errbuf, sizeof(errbuf));
        nova_runtime_error(line, "replace() invalid regex: %s", errbuf);
    }

    /* Build result by replacing all matches */
    int cap = 1024;
    char *buf = malloc(cap);
    int len = 0;
    const char *cursor = str;
    regmatch_t pmatch[1];

    while (regexec(&reg, cursor, 1, pmatch, 0) == 0) {
        int start = pmatch[0].rm_so;
        int end = pmatch[0].rm_eo;

        /* Copy before match */
        while (len + start + repl_len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        memcpy(buf + len, cursor, start);
        len += start;

        /* Copy replacement */
        memcpy(buf + len, repl, repl_len);
        len += repl_len;

        cursor += end;
        if (start == end) {
            /* Zero-width match: copy one char and advance */
            if (*cursor) { buf[len++] = *cursor++; }
            else break;
        }
    }

    /* Copy remainder */
    int rem = strlen(cursor);
    while (len + rem + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
    memcpy(buf + len, cursor, rem);
    len += rem;
    buf[len] = '\0';

    regfree(&reg);
    return NOVA_OBJ(nova_string_take(buf, len));
}

/* ── test(pattern, string) ── bool */
static NovaValue regex_test_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]) || !IS_STRING(argv[1]))
        nova_type_error(line, "test() expects two strings (pattern, string)");

    const char *pattern = AS_CSTRING(argv[0]);
    const char *str = AS_CSTRING(argv[1]);

    regex_t reg;
    int ret = regcomp(&reg, pattern, REG_EXTENDED);
    if (ret != 0) {
        char errbuf[256];
        regerror(ret, &reg, errbuf, sizeof(errbuf));
        nova_runtime_error(line, "test() invalid regex: %s", errbuf);
    }

    ret = regexec(&reg, str, 0, NULL, 0);
    regfree(&reg);
    return NOVA_BOOL(ret == 0);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "match",    NOVA_OBJ(nova_builtin_new("match",    regex_match_fn,   2)));
    nova_env_define(env, "find_all", NOVA_OBJ(nova_builtin_new("find_all", regex_find_all,   2)));
    nova_env_define(env, "replace",  NOVA_OBJ(nova_builtin_new("replace",  regex_replace_fn, 3)));
    nova_env_define(env, "test",     NOVA_OBJ(nova_builtin_new("test",     regex_test_fn,    2)));
}
