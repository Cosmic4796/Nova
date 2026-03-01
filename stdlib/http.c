#include "nova.h"
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

/* ── Response buffer ── */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} HttpBuf;

static void httpbuf_init(HttpBuf *b) {
    b->cap = 4096;
    b->len = 0;
    b->data = malloc(b->cap);
}

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuf *b = (HttpBuf *)userdata;
    size_t total = size * nmemb;
    while (b->len + total + 1 >= b->cap) { b->cap *= 2; b->data = realloc(b->data, b->cap); }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

/* ── Header buffer ── */
static size_t header_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    HttpBuf *b = (HttpBuf *)userdata;
    size_t total = size * nmemb;
    while (b->len + total + 1 >= b->cap) { b->cap *= 2; b->data = realloc(b->data, b->cap); }
    memcpy(b->data + b->len, ptr, total);
    b->len += total;
    b->data[b->len] = '\0';
    return total;
}

/* Build response dict from curl result */
static NovaValue make_response(CURL *curl, HttpBuf *body, HttpBuf *headers, int line) {
    long status;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    ObjDict *dict = nova_dict_new();
    nova_table_set(&dict->table, nova_string_copy("status", 6), NOVA_INT(status));
    nova_table_set(&dict->table, nova_string_copy("body", 4),
                   NOVA_OBJ(nova_string_copy(body->data, (int)body->len)));

    /* Parse headers into a dict */
    ObjDict *hdict = nova_dict_new();
    char *hdr = headers->data;
    while (hdr && *hdr) {
        char *nl = strstr(hdr, "\r\n");
        if (!nl) nl = hdr + strlen(hdr);
        char *colon = memchr(hdr, ':', nl - hdr);
        if (colon) {
            int klen = (int)(colon - hdr);
            char *vstart = colon + 1;
            while (vstart < nl && *vstart == ' ') vstart++;
            int vlen = (int)(nl - vstart);
            ObjString *key = nova_string_copy(hdr, klen);
            nova_table_set(&hdict->table, key, NOVA_OBJ(nova_string_copy(vstart, vlen)));
        }
        if (*nl == '\0') break;
        hdr = nl + 2;
    }
    nova_table_set(&dict->table, nova_string_copy("headers", 7), NOVA_OBJ(hdict));

    return NOVA_OBJ(dict);
}

/* ── get(url) ── */
static NovaValue http_get(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "get() expects a URL string");

    CURL *curl = curl_easy_init();
    if (!curl) nova_runtime_error(line, "get() failed to initialize HTTP client");

    HttpBuf body, headers;
    httpbuf_init(&body);
    httpbuf_init(&headers);

    curl_easy_setopt(curl, CURLOPT_URL, AS_CSTRING(argv[0]));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        const char *err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        free(body.data);
        free(headers.data);
        nova_runtime_error(line, "get() HTTP error: %s", err);
    }

    NovaValue result = make_response(curl, &body, &headers, line);
    curl_easy_cleanup(curl);
    free(body.data);
    free(headers.data);
    return result;
}

/* ── post(url, body) ── */
static NovaValue http_post(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "post() expects a URL string as first argument");
    if (!IS_STRING(argv[1]))
        nova_type_error(line, "post() expects a body string as second argument");

    CURL *curl = curl_easy_init();
    if (!curl) nova_runtime_error(line, "post() failed to initialize HTTP client");

    HttpBuf body, headers;
    httpbuf_init(&body);
    httpbuf_init(&headers);

    curl_easy_setopt(curl, CURLOPT_URL, AS_CSTRING(argv[0]));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, AS_CSTRING(argv[1]));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)AS_STRING(argv[1])->length);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        const char *err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        free(body.data);
        free(headers.data);
        nova_runtime_error(line, "post() HTTP error: %s", err);
    }

    NovaValue result = make_response(curl, &body, &headers, line);
    curl_easy_cleanup(curl);
    free(body.data);
    free(headers.data);
    return result;
}

/* ── download(url, path) ── */
static NovaValue http_download(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "download() expects a URL string as first argument");
    if (!IS_STRING(argv[1]))
        nova_type_error(line, "download() expects a file path string as second argument");

    const char *url = AS_CSTRING(argv[0]);
    const char *path = AS_CSTRING(argv[1]);

    FILE *f = fopen(path, "wb");
    if (!f) nova_runtime_error(line, "download() cannot open file '%s'", path);

    CURL *curl = curl_easy_init();
    if (!curl) { fclose(f); nova_runtime_error(line, "download() failed to initialize HTTP client"); }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

    CURLcode res = curl_easy_perform(curl);
    fclose(f);

    if (res != CURLE_OK) {
        const char *err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        nova_runtime_error(line, "download() HTTP error: %s", err);
    }

    curl_easy_cleanup(curl);
    return NOVA_BOOL(true);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "get",      NOVA_OBJ(nova_builtin_new("get",      http_get,      1)));
    nova_env_define(env, "post",     NOVA_OBJ(nova_builtin_new("post",     http_post,     2)));
    nova_env_define(env, "download", NOVA_OBJ(nova_builtin_new("download", http_download, 2)));
}
