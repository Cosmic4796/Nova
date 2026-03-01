#include "nova.h"
#include <libgen.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ── join(parts...) ── variadic */
static NovaValue path_join(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (argc == 1 && IS_LIST(argv[0])) {
        /* Accept a list of parts */
        ObjList *parts = AS_LIST(argv[0]);
        char buf[PATH_MAX] = "";
        for (int i = 0; i < parts->count; i++) {
            if (!IS_STRING(parts->items[i]))
                nova_type_error(line, "join() expects string path components");
            if (i > 0 && buf[strlen(buf) - 1] != '/') strcat(buf, "/");
            strncat(buf, AS_CSTRING(parts->items[i]), sizeof(buf) - strlen(buf) - 1);
        }
        return NOVA_OBJ(nova_string_copy(buf, strlen(buf)));
    }

    /* Variadic: join all string args */
    char buf[PATH_MAX] = "";
    for (int i = 0; i < argc; i++) {
        if (!IS_STRING(argv[i]))
            nova_type_error(line, "join() expects string arguments");
        if (i > 0 && strlen(buf) > 0 && buf[strlen(buf) - 1] != '/') strcat(buf, "/");
        strncat(buf, AS_CSTRING(argv[i]), sizeof(buf) - strlen(buf) - 1);
    }
    return NOVA_OBJ(nova_string_copy(buf, strlen(buf)));
}

/* ── basename(path) ── */
static NovaValue path_basename_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "basename() expects a string");
    char *tmp = strdup(AS_CSTRING(argv[0]));
    char *base = basename(tmp);
    NovaValue result = NOVA_OBJ(nova_string_copy(base, strlen(base)));
    free(tmp);
    return result;
}

/* ── dirname(path) ── */
static NovaValue path_dirname_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "dirname() expects a string");
    char *tmp = strdup(AS_CSTRING(argv[0]));
    char *dir = dirname(tmp);
    NovaValue result = NOVA_OBJ(nova_string_copy(dir, strlen(dir)));
    free(tmp);
    return result;
}

/* ── extension(path) ── */
static NovaValue path_extension(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "extension() expects a string");
    const char *p = AS_CSTRING(argv[0]);
    /* Find last component */
    const char *slash = strrchr(p, '/');
    const char *base = slash ? slash + 1 : p;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) return NOVA_OBJ(nova_string_copy("", 0));
    return NOVA_OBJ(nova_string_copy(dot, strlen(dot)));
}

/* ── exists(path) ── */
static NovaValue path_exists(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "exists() expects a string");
    struct stat st;
    return NOVA_BOOL(stat(AS_CSTRING(argv[0]), &st) == 0);
}

/* ── is_dir(path) ── */
static NovaValue path_is_dir(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "is_dir() expects a string");
    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0) return NOVA_BOOL(false);
    return NOVA_BOOL(S_ISDIR(st.st_mode));
}

/* ── is_file(path) ── */
static NovaValue path_is_file(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "is_file() expects a string");
    struct stat st;
    if (stat(AS_CSTRING(argv[0]), &st) != 0) return NOVA_BOOL(false);
    return NOVA_BOOL(S_ISREG(st.st_mode));
}

/* ── absolute(path) ── */
static NovaValue path_absolute(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "absolute() expects a string");
    char resolved[PATH_MAX];
    if (realpath(AS_CSTRING(argv[0]), resolved) == NULL)
        nova_runtime_error(line, "absolute() cannot resolve path '%s'", AS_CSTRING(argv[0]));
    return NOVA_OBJ(nova_string_copy(resolved, strlen(resolved)));
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "join",      NOVA_OBJ(nova_builtin_new("join",      path_join,         -1)));
    nova_env_define(env, "basename",  NOVA_OBJ(nova_builtin_new("basename",  path_basename_fn,   1)));
    nova_env_define(env, "dirname",   NOVA_OBJ(nova_builtin_new("dirname",   path_dirname_fn,    1)));
    nova_env_define(env, "extension", NOVA_OBJ(nova_builtin_new("extension", path_extension,     1)));
    nova_env_define(env, "exists",    NOVA_OBJ(nova_builtin_new("exists",    path_exists,        1)));
    nova_env_define(env, "is_dir",    NOVA_OBJ(nova_builtin_new("is_dir",    path_is_dir,        1)));
    nova_env_define(env, "is_file",   NOVA_OBJ(nova_builtin_new("is_file",   path_is_file,       1)));
    nova_env_define(env, "absolute",  NOVA_OBJ(nova_builtin_new("absolute",  path_absolute,      1)));
}
