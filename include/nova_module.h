#ifndef NOVA_MODULE_H
#define NOVA_MODULE_H

#include "nova_common.h"
#include "nova_value.h"
#include "nova_env.h"

/* Loaded module cache entry */
typedef struct ModuleEntry {
    char                *name;
    NovaTable            exports;
    struct ModuleEntry  *next;
} ModuleEntry;

struct ModuleLoader {
    char        *base_dir;
    char        *exe_dir;        /* directory of nova executable (for stdlib) */
    char        *project_root;   /* root directory for package resolution */
    ModuleEntry *loaded;         /* linked list of cached modules */
    char       **loading;        /* currently-loading module names (circular detect) */
    int          loading_count;
    int          loading_capacity;
};

void     nova_module_loader_init(ModuleLoader *loader, const char *base_dir,
                                 const char *exe_dir);
void     nova_module_loader_free(ModuleLoader *loader);
void     nova_module_load(ModuleLoader *loader, Interpreter *interp,
                          const char *module_name, int line);

/* C module interface: every shared lib must export this */
typedef void (*NovaModuleInit)(Interpreter *interp, Environment *env);

#endif
