#include "nova.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

/* ── now() ── returns dict with year/month/day/hour/minute/second */
static NovaValue dt_now(Interpreter *interp, int argc, NovaValue *argv, int line) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    ObjDict *dict = nova_dict_new();
    nova_table_set(&dict->table, nova_string_copy("year",   4), NOVA_INT(tm->tm_year + 1900));
    nova_table_set(&dict->table, nova_string_copy("month",  5), NOVA_INT(tm->tm_mon + 1));
    nova_table_set(&dict->table, nova_string_copy("day",    3), NOVA_INT(tm->tm_mday));
    nova_table_set(&dict->table, nova_string_copy("hour",   4), NOVA_INT(tm->tm_hour));
    nova_table_set(&dict->table, nova_string_copy("minute", 6), NOVA_INT(tm->tm_min));
    nova_table_set(&dict->table, nova_string_copy("second", 6), NOVA_INT(tm->tm_sec));
    return NOVA_OBJ(dict);
}

/* ── timestamp() ── Unix timestamp as float */
static NovaValue dt_timestamp(Interpreter *interp, int argc, NovaValue *argv, int line) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return NOVA_FLOAT((double)tv.tv_sec + (double)tv.tv_usec / 1000000.0);
}

/* ── format(timestamp, fmt) ── strftime formatting */
static NovaValue dt_format(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_NUMBER(argv[0]))
        nova_type_error(line, "format() expects a number (timestamp) as first argument");
    if (!IS_STRING(argv[1]))
        nova_type_error(line, "format() expects a format string as second argument");

    time_t t = (time_t)AS_NUMBER(argv[0]);
    struct tm *tm = localtime(&t);
    const char *fmt = AS_CSTRING(argv[1]);

    char buf[256];
    size_t len = strftime(buf, sizeof(buf), fmt, tm);
    return NOVA_OBJ(nova_string_copy(buf, (int)len));
}

/* ── parse(date_str, fmt) ── strptime to timestamp */
static NovaValue dt_parse(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "parse() expects a date string as first argument");
    if (!IS_STRING(argv[1]))
        nova_type_error(line, "parse() expects a format string as second argument");

    const char *date_str = AS_CSTRING(argv[0]);
    const char *fmt = AS_CSTRING(argv[1]);

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    char *ret = strptime(date_str, fmt, &tm);
    if (!ret)
        nova_runtime_error(line, "parse() could not parse date '%s' with format '%s'", date_str, fmt);

    time_t t = mktime(&tm);
    return NOVA_FLOAT((double)t);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "now",       NOVA_OBJ(nova_builtin_new("now",       dt_now,       0)));
    nova_env_define(env, "timestamp", NOVA_OBJ(nova_builtin_new("timestamp", dt_timestamp, 0)));
    nova_env_define(env, "format",    NOVA_OBJ(nova_builtin_new("format",    dt_format,    2)));
    nova_env_define(env, "parse",     NOVA_OBJ(nova_builtin_new("parse",     dt_parse,     2)));
}
