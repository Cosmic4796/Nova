#include "nova.h"
#include <string.h>
#include <stdlib.h>

/* ── RFC 4180 CSV parser ── */

static NovaValue csv_parse_string(const char *src, int line) {
    ObjList *rows = nova_list_new();
    ObjList *row = nova_list_new();
    char field[4096];
    int flen = 0;
    int i = 0;
    bool in_quotes = false;

    while (src[i]) {
        if (in_quotes) {
            if (src[i] == '"') {
                if (src[i + 1] == '"') {
                    /* Escaped quote */
                    field[flen++] = '"';
                    i += 2;
                } else {
                    /* End of quoted field */
                    in_quotes = false;
                    i++;
                }
            } else {
                field[flen++] = src[i++];
            }
        } else {
            if (src[i] == '"') {
                in_quotes = true;
                i++;
            } else if (src[i] == ',') {
                field[flen] = '\0';
                nova_list_push(row, NOVA_OBJ(nova_string_copy(field, flen)));
                flen = 0;
                i++;
            } else if (src[i] == '\n' || (src[i] == '\r' && src[i + 1] == '\n')) {
                field[flen] = '\0';
                nova_list_push(row, NOVA_OBJ(nova_string_copy(field, flen)));
                flen = 0;
                /* Skip empty trailing rows */
                if (row->count > 1 || (row->count == 1 && AS_STRING(row->items[0])->length > 0))
                    nova_list_push(rows, NOVA_OBJ(row));
                row = nova_list_new();
                if (src[i] == '\r') i++;
                i++;
            } else {
                field[flen++] = src[i++];
            }
        }
        if (flen >= (int)sizeof(field) - 1) flen = (int)sizeof(field) - 2;
    }

    /* Final field/row */
    if (flen > 0 || row->count > 0) {
        field[flen] = '\0';
        nova_list_push(row, NOVA_OBJ(nova_string_copy(field, flen)));
        if (row->count > 1 || (row->count == 1 && AS_STRING(row->items[0])->length > 0))
            nova_list_push(rows, NOVA_OBJ(row));
    }

    return NOVA_OBJ(rows);
}

/* ── parse(csv_string) ── */
static NovaValue csv_parse_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "parse() expects a CSV string");
    return csv_parse_string(AS_CSTRING(argv[0]), line);
}

/* ── dump(data) ── list of lists to CSV string */
static NovaValue csv_dump_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_LIST(argv[0]))
        nova_type_error(line, "dump() expects a list of lists");
    ObjList *rows = AS_LIST(argv[0]);

    /* Calculate rough size */
    int cap = 1024;
    char *buf = malloc(cap);
    int len = 0;

    for (int r = 0; r < rows->count; r++) {
        if (!IS_LIST(rows->items[r]))
            nova_type_error(line, "dump() expects each row to be a list");
        ObjList *row = AS_LIST(rows->items[r]);

        for (int c = 0; c < row->count; c++) {
            if (c > 0) { if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = ','; }

            const char *val = "";
            int vlen = 0;
            char numbuf[64];
            if (IS_STRING(row->items[c])) {
                val = AS_CSTRING(row->items[c]);
                vlen = AS_STRING(row->items[c])->length;
            } else if (IS_INT(row->items[c])) {
                vlen = snprintf(numbuf, sizeof(numbuf), "%lld", (long long)AS_INT(row->items[c]));
                val = numbuf;
            } else if (IS_FLOAT(row->items[c])) {
                vlen = snprintf(numbuf, sizeof(numbuf), "%g", AS_FLOAT(row->items[c]));
                val = numbuf;
            } else if (IS_BOOL(row->items[c])) {
                val = AS_BOOL(row->items[c]) ? "true" : "false";
                vlen = strlen(val);
            } else if (IS_NONE(row->items[c])) {
                val = "";
                vlen = 0;
            }

            /* Check if quoting needed */
            bool need_quotes = false;
            for (int k = 0; k < vlen; k++) {
                if (val[k] == ',' || val[k] == '"' || val[k] == '\n' || val[k] == '\r') {
                    need_quotes = true;
                    break;
                }
            }

            if (need_quotes) {
                /* Quoted field: double any quotes */
                while (len + vlen * 2 + 3 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                buf[len++] = '"';
                for (int k = 0; k < vlen; k++) {
                    if (val[k] == '"') buf[len++] = '"';
                    buf[len++] = val[k];
                }
                buf[len++] = '"';
            } else {
                while (len + vlen + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                memcpy(buf + len, val, vlen);
                len += vlen;
            }
        }

        /* Newline */
        while (len + 2 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = '\n';
    }

    buf[len] = '\0';
    return NOVA_OBJ(nova_string_take(buf, len));
}

/* ── parse_file(path) ── */
static NovaValue csv_parse_file(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "parse_file() expects a file path string");
    const char *path = AS_CSTRING(argv[0]);

    FILE *f = fopen(path, "r");
    if (!f) nova_runtime_error(line, "parse_file() cannot open '%s'", path);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    size_t rd = fread(buf, 1, size, f);
    buf[rd] = '\0';
    fclose(f);

    NovaValue result = csv_parse_string(buf, line);
    free(buf);
    return result;
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "parse",      NOVA_OBJ(nova_builtin_new("parse",      csv_parse_fn,   1)));
    nova_env_define(env, "dump",       NOVA_OBJ(nova_builtin_new("dump",       csv_dump_fn,    1)));
    nova_env_define(env, "parse_file", NOVA_OBJ(nova_builtin_new("parse_file", csv_parse_file, 1)));
}
