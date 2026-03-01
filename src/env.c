#include "nova.h"

Environment *nova_env_new(Environment *parent) {
    Environment *env = nova_alloc(sizeof(Environment));
    nova_table_init(&env->values);
    env->parent = parent;
    env->marked = false;
    return env;
}

void nova_env_free(Environment *env) {
    nova_table_free(&env->values);
    nova_free(env, sizeof(Environment));
}

void nova_env_define(Environment *env, const char *name, NovaValue value) {
    ObjString *key = nova_string_copy(name, strlen(name));
    nova_table_set(&env->values, key, value);
}

NovaValue nova_env_get(Environment *env, const char *name, int line) {
    ObjString *key = nova_string_copy(name, strlen(name));
    NovaValue value;
    Environment *cur = env;
    while (cur != NULL) {
        if (nova_table_get(&cur->values, key, &value)) {
            return value;
        }
        cur = cur->parent;
    }
    nova_name_error(line, "Undefined variable '%s'", name);
    return NOVA_NONE(); /* unreachable */
}

void nova_env_set(Environment *env, const char *name, NovaValue value, int line) {
    ObjString *key = nova_string_copy(name, strlen(name));
    NovaValue dummy;
    Environment *cur = env;
    while (cur != NULL) {
        if (nova_table_get(&cur->values, key, &dummy)) {
            nova_table_set(&cur->values, key, value);
            return;
        }
        cur = cur->parent;
    }
    nova_name_error(line, "Undefined variable '%s'", name);
}

bool nova_env_has(Environment *env, const char *name) {
    ObjString *key = nova_string_copy(name, strlen(name));
    NovaValue dummy;
    Environment *cur = env;
    while (cur != NULL) {
        if (nova_table_get(&cur->values, key, &dummy)) {
            return true;
        }
        cur = cur->parent;
    }
    return false;
}
