#include "nova.h"
#include <time.h>

/* Global argv storage set from main */
static int g_argc = 0;
static char **g_argv = NULL;

static NovaValue os_args(Interpreter *interp, int argc, NovaValue *argv, int line) {
    ObjList *list = nova_list_new();
    for (int i = 0; i < g_argc; i++) {
        ObjString *s = nova_string_copy(g_argv[i], strlen(g_argv[i]));
        nova_list_push(list, NOVA_OBJ(s));
    }
    return NOVA_OBJ(list);
}

static NovaValue os_env(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "env() expects a string");
    const char *val = getenv(AS_STRING(argv[0])->chars);
    if (!val) return NOVA_NONE();
    return NOVA_OBJ(nova_string_copy(val, strlen(val)));
}

static NovaValue os_clock_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double t = ts.tv_sec + ts.tv_nsec / 1e9;
    return NOVA_FLOAT(t);
}

static NovaValue os_exit_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    int code = 0;
    if (argc > 0 && IS_INT(argv[0])) code = (int)AS_INT(argv[0]);
    exit(code);
    return NOVA_NONE();
}

static NovaValue os_system_fn(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "system() expects a string command");
    int ret = system(AS_STRING(argv[0])->chars);
    return NOVA_INT(ret);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "args",    NOVA_OBJ(nova_builtin_new("args",   os_args,      0)));
    nova_env_define(env, "env",     NOVA_OBJ(nova_builtin_new("env",    os_env,       1)));
    nova_env_define(env, "clock",   NOVA_OBJ(nova_builtin_new("clock",  os_clock_fn,  0)));
    nova_env_define(env, "exit",    NOVA_OBJ(nova_builtin_new("exit",   os_exit_fn,  -1)));
    nova_env_define(env, "system",  NOVA_OBJ(nova_builtin_new("system", os_system_fn, 1)));
}
