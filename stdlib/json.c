#include "nova.h"
#include <ctype.h>

/* ── Minimal JSON parser ─────────────────────────────────────────── */

typedef struct {
    const char *src;
    int pos;
    int line;       /* Nova source line for errors */
} JsonParser;

static void skip_ws(JsonParser *p) {
    while (p->src[p->pos] && isspace((unsigned char)p->src[p->pos])) p->pos++;
}

static NovaValue json_parse_value(JsonParser *p);

static ObjString *json_parse_string(JsonParser *p) {
    if (p->src[p->pos] != '"')
        nova_runtime_error(p->line, "JSON: expected '\"'");
    p->pos++; /* skip opening " */

    char buf[4096];
    int len = 0;
    while (p->src[p->pos] && p->src[p->pos] != '"') {
        if (p->src[p->pos] == '\\') {
            p->pos++;
            switch (p->src[p->pos]) {
                case '"':  buf[len++] = '"';  break;
                case '\\': buf[len++] = '\\'; break;
                case '/':  buf[len++] = '/';  break;
                case 'n':  buf[len++] = '\n'; break;
                case 't':  buf[len++] = '\t'; break;
                case 'r':  buf[len++] = '\r'; break;
                case 'b':  buf[len++] = '\b'; break;
                case 'f':  buf[len++] = '\f'; break;
                default:   buf[len++] = p->src[p->pos]; break;
            }
        } else {
            buf[len++] = p->src[p->pos];
        }
        p->pos++;
        if (len >= (int)sizeof(buf) - 1) break;
    }
    if (p->src[p->pos] == '"') p->pos++; /* skip closing " */
    buf[len] = '\0';
    return nova_string_copy(buf, len);
}

static NovaValue json_parse_number(JsonParser *p) {
    const char *start = p->src + p->pos;
    bool is_float = false;

    if (p->src[p->pos] == '-') p->pos++;
    while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    if (p->src[p->pos] == '.') {
        is_float = true;
        p->pos++;
        while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->src[p->pos] == 'e' || p->src[p->pos] == 'E') {
        is_float = true;
        p->pos++;
        if (p->src[p->pos] == '+' || p->src[p->pos] == '-') p->pos++;
        while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }

    if (is_float) return NOVA_FLOAT(strtod(start, NULL));
    return NOVA_INT(strtoll(start, NULL, 10));
}

static NovaValue json_parse_array(JsonParser *p) {
    p->pos++; /* skip [ */
    ObjList *list = nova_list_new();
    skip_ws(p);
    if (p->src[p->pos] == ']') { p->pos++; return NOVA_OBJ(list); }

    while (1) {
        skip_ws(p);
        nova_list_push(list, json_parse_value(p));
        skip_ws(p);
        if (p->src[p->pos] == ',') { p->pos++; continue; }
        if (p->src[p->pos] == ']') { p->pos++; break; }
        nova_runtime_error(p->line, "JSON: expected ',' or ']' in array");
    }
    return NOVA_OBJ(list);
}

static NovaValue json_parse_object(JsonParser *p) {
    p->pos++; /* skip { */
    ObjDict *dict = nova_dict_new();
    skip_ws(p);
    if (p->src[p->pos] == '}') { p->pos++; return NOVA_OBJ(dict); }

    while (1) {
        skip_ws(p);
        ObjString *key = json_parse_string(p);
        skip_ws(p);
        if (p->src[p->pos] != ':')
            nova_runtime_error(p->line, "JSON: expected ':' after key");
        p->pos++;
        skip_ws(p);
        NovaValue val = json_parse_value(p);
        nova_table_set(&dict->table, key, val);
        skip_ws(p);
        if (p->src[p->pos] == ',') { p->pos++; continue; }
        if (p->src[p->pos] == '}') { p->pos++; break; }
        nova_runtime_error(p->line, "JSON: expected ',' or '}' in object");
    }
    return NOVA_OBJ(dict);
}

static NovaValue json_parse_value(JsonParser *p) {
    skip_ws(p);
    char c = p->src[p->pos];

    if (c == '"') return NOVA_OBJ(json_parse_string(p));
    if (c == '[') return json_parse_array(p);
    if (c == '{') return json_parse_object(p);
    if (c == '-' || isdigit((unsigned char)c)) return json_parse_number(p);
    if (strncmp(p->src + p->pos, "true", 4) == 0)  { p->pos += 4; return NOVA_BOOL(true); }
    if (strncmp(p->src + p->pos, "false", 5) == 0) { p->pos += 5; return NOVA_BOOL(false); }
    if (strncmp(p->src + p->pos, "null", 4) == 0)  { p->pos += 4; return NOVA_NONE(); }

    nova_runtime_error(p->line, "JSON: unexpected character '%c'", c);
    return NOVA_NONE();
}

/* ── JSON stringify ──────────────────────────────────────────────── */

typedef struct {
    char *buf;
    int len;
    int cap;
} JsonBuf;

static void jbuf_init(JsonBuf *b) { b->cap = 256; b->len = 0; b->buf = malloc(b->cap); }
static void jbuf_grow(JsonBuf *b, int need) {
    while (b->len + need >= b->cap) { b->cap *= 2; b->buf = realloc(b->buf, b->cap); }
}
static void jbuf_add(JsonBuf *b, const char *s, int n) {
    jbuf_grow(b, n); memcpy(b->buf + b->len, s, n); b->len += n;
}
static void jbuf_str(JsonBuf *b, const char *s) { jbuf_add(b, s, strlen(s)); }
static void jbuf_char(JsonBuf *b, char c) { jbuf_grow(b, 1); b->buf[b->len++] = c; }

static void json_stringify(JsonBuf *b, NovaValue val) {
    if (IS_NONE(val)) { jbuf_str(b, "null"); return; }
    if (IS_BOOL(val)) { jbuf_str(b, AS_BOOL(val) ? "true" : "false"); return; }
    if (IS_INT(val)) {
        char tmp[32]; snprintf(tmp, sizeof(tmp), "%lld", (long long)AS_INT(val));
        jbuf_str(b, tmp); return;
    }
    if (IS_FLOAT(val)) {
        char tmp[64]; snprintf(tmp, sizeof(tmp), "%g", AS_FLOAT(val));
        jbuf_str(b, tmp); return;
    }
    if (IS_STRING(val)) {
        ObjString *s = AS_STRING(val);
        jbuf_char(b, '"');
        for (int i = 0; i < s->length; i++) {
            char c = s->chars[i];
            switch (c) {
                case '"':  jbuf_str(b, "\\\""); break;
                case '\\': jbuf_str(b, "\\\\"); break;
                case '\n': jbuf_str(b, "\\n");  break;
                case '\t': jbuf_str(b, "\\t");  break;
                case '\r': jbuf_str(b, "\\r");  break;
                default:   jbuf_char(b, c);     break;
            }
        }
        jbuf_char(b, '"');
        return;
    }
    if (IS_LIST(val)) {
        ObjList *list = AS_LIST(val);
        jbuf_char(b, '[');
        for (int i = 0; i < list->count; i++) {
            if (i > 0) jbuf_char(b, ',');
            json_stringify(b, list->items[i]);
        }
        jbuf_char(b, ']');
        return;
    }
    if (IS_DICT(val)) {
        ObjDict *dict = AS_DICT(val);
        jbuf_char(b, '{');
        TableIter iter;
        ObjString *key;
        NovaValue v;
        nova_table_iter_init(&iter, &dict->table);
        bool first = true;
        while (nova_table_iter_next(&iter, &key, &v)) {
            if (!first) jbuf_char(b, ',');
            first = false;
            jbuf_char(b, '"');
            jbuf_add(b, key->chars, key->length);
            jbuf_char(b, '"');
            jbuf_char(b, ':');
            json_stringify(b, v);
        }
        jbuf_char(b, '}');
        return;
    }
    jbuf_str(b, "null"); /* fallback for unsupported types */
}

/* ── Nova interface ──────────────────────────────────────────────── */

static NovaValue json_parse_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "parse() expects a JSON string");
    JsonParser p = { .src = AS_STRING(argv[0])->chars, .pos = 0, .line = line };
    return json_parse_value(&p);
}

static NovaValue json_dump_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    JsonBuf b;
    jbuf_init(&b);
    json_stringify(&b, argv[0]);
    jbuf_char(&b, '\0');
    return NOVA_OBJ(nova_string_take(b.buf, b.len - 1));
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "parse", NOVA_OBJ(nova_builtin_new("parse", json_parse_fn, 1)));
    nova_env_define(env, "dump",  NOVA_OBJ(nova_builtin_new("dump",  json_dump_fn,  1)));
}
