#ifndef NOVA_ENV_H
#define NOVA_ENV_H

#include "nova_common.h"
#include "nova_value.h"
#include "nova_table.h"

struct Environment {
    NovaTable values;
    Environment *parent;
    bool marked;    /* for GC */
};

Environment *nova_env_new(Environment *parent);
void         nova_env_free(Environment *env);
void         nova_env_define(Environment *env, const char *name, NovaValue value);
NovaValue    nova_env_get(Environment *env, const char *name, int line);
void         nova_env_set(Environment *env, const char *name, NovaValue value, int line);
bool         nova_env_has(Environment *env, const char *name);

#endif
