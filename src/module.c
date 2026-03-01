#include "nova.h"
#include <dlfcn.h>
#include <libgen.h>
#include <sys/stat.h>

/* Read "main" field from a nova.json file. Returns malloc'd string or NULL. */
static char *read_pkg_main(const char *pkg_dir) {
    char json_path[1024];
    snprintf(json_path, sizeof(json_path), "%s/nova.json", pkg_dir);
    FILE *f = fopen(json_path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);

    char *p = strstr(buf, "\"main\"");
    if (!p) { free(buf); return NULL; }
    p = strchr(p + 6, '"');
    if (!p) { free(buf); return NULL; }
    p++; /* skip opening quote */
    char *end = strchr(p, '"');
    if (!end) { free(buf); return NULL; }
    size_t len = end - p;
    char *result = malloc(len + 1);
    memcpy(result, p, len);
    result[len] = '\0';
    free(buf);
    return result;
}

static bool dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void nova_module_loader_init(ModuleLoader *loader, const char *base_dir,
                             const char *exe_dir) {
    loader->base_dir = strdup(base_dir);
    loader->exe_dir = exe_dir ? strdup(exe_dir) : NULL;
    loader->project_root = NULL;
    loader->loaded = NULL;
    loader->loading = NULL;
    loader->loading_count = 0;
    loader->loading_capacity = 0;
}

void nova_module_loader_free(ModuleLoader *loader) {
    free(loader->base_dir);
    if (loader->exe_dir) free(loader->exe_dir);
    if (loader->project_root) free(loader->project_root);

    ModuleEntry *entry = loader->loaded;
    while (entry) {
        ModuleEntry *next = entry->next;
        free(entry->name);
        nova_table_free(&entry->exports);
        free(entry);
        entry = next;
    }

    if (loader->loading) {
        for (int i = 0; i < loader->loading_count; i++)
            free(loader->loading[i]);
        free(loader->loading);
    }
}

static bool is_loading(ModuleLoader *loader, const char *name) {
    for (int i = 0; i < loader->loading_count; i++) {
        if (strcmp(loader->loading[i], name) == 0) return true;
    }
    return false;
}

static void push_loading(ModuleLoader *loader, const char *name) {
    if (loader->loading_count >= loader->loading_capacity) {
        int old = loader->loading_capacity;
        loader->loading_capacity = old < 4 ? 4 : old * 2;
        loader->loading = realloc(loader->loading,
                                  sizeof(char*) * loader->loading_capacity);
    }
    loader->loading[loader->loading_count++] = strdup(name);
}

static void pop_loading(ModuleLoader *loader) {
    if (loader->loading_count > 0) {
        free(loader->loading[--loader->loading_count]);
    }
}

static ModuleEntry *find_loaded(ModuleLoader *loader, const char *name) {
    ModuleEntry *entry = loader->loaded;
    while (entry) {
        if (strcmp(entry->name, name) == 0) return entry;
        entry = entry->next;
    }
    return NULL;
}

static void import_as_namespace(const char *module_name, NovaTable *table,
                                Environment *env) {
    ObjDict *ns = nova_dict_new();
    TableIter iter;
    ObjString *key;
    NovaValue value;
    nova_table_iter_init(&iter, table);
    while (nova_table_iter_next(&iter, &key, &value)) {
        nova_table_set(&ns->table, key, value);
    }
    nova_env_define(env, module_name, NOVA_OBJ(ns));
}

/* Load a C shared library module */
static bool load_c_module(ModuleLoader *loader, Interpreter *interp,
                          const char *module_name, const char *path, int line) {
    void *handle = dlopen(path, RTLD_NOW);
    if (!handle) return false;

    NovaModuleInit init_fn = (NovaModuleInit)dlsym(handle, "nova_module_init");
    if (!init_fn) {
        dlclose(handle);
        return false;
    }

    /* Create a temporary environment to collect exports */
    Environment *mod_env = nova_env_new(NULL);
    init_fn(interp, mod_env);

    /* Cache and import */
    ModuleEntry *entry = malloc(sizeof(ModuleEntry));
    entry->name = strdup(module_name);
    nova_table_init(&entry->exports);
    nova_table_copy(&mod_env->values, &entry->exports);
    entry->next = loader->loaded;
    loader->loaded = entry;

    import_as_namespace(module_name, &entry->exports, nova_current_env(interp));
    return true;
}

/* Load a .nova source module */
static bool load_nova_module(ModuleLoader *loader, Interpreter *interp,
                             const char *module_name, const char *path, int line) {
    FILE *f = fopen(path, "r");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = malloc(size + 1);
    size_t read = fread(source, 1, size, f);
    source[read] = '\0';
    fclose(f);

    /* Lex */
    Lexer lexer;
    nova_lexer_init(&lexer, source, path);
    int token_count;
    nova_lexer_tokenize(&lexer, &token_count);

    /* Parse */
    Parser parser;
    nova_parser_init(&parser, lexer.tokens, token_count);
    AstNode *program = nova_parse(&parser);

    /* Get the directory of this module for relative imports */
    char *path_copy = strdup(path);
    char *mod_dir = dirname(path_copy);

    /* Create a sub-interpreter to execute the module */
    ModuleLoader sub_loader;
    nova_module_loader_init(&sub_loader, mod_dir, loader->exe_dir);
    /* Share the project root for package resolution */
    if (loader->project_root)
        sub_loader.project_root = strdup(loader->project_root);
    /* Share the loading stack for circular detection */
    sub_loader.loading = loader->loading;
    sub_loader.loading_count = loader->loading_count;
    sub_loader.loading_capacity = loader->loading_capacity;
    /* Share loaded modules cache */
    sub_loader.loaded = loader->loaded;

    Interpreter mod_interp;
    nova_interpreter_init(&mod_interp, &sub_loader);
    nova_interpret_program(&mod_interp, program);

    /* Cache module exports (everything in globals) */
    ModuleEntry *entry = malloc(sizeof(ModuleEntry));
    entry->name = strdup(module_name);
    nova_table_init(&entry->exports);
    nova_table_copy(&mod_interp.globals->values, &entry->exports);
    entry->next = loader->loaded;
    loader->loaded = entry;

    /* Import into current interpreter */
    import_as_namespace(module_name, &entry->exports, nova_current_env(interp));

    /* Sync back shared state */
    loader->loading = sub_loader.loading;
    loader->loading_count = sub_loader.loading_count;
    loader->loading_capacity = sub_loader.loading_capacity;
    loader->loaded = sub_loader.loaded;

    /* Cleanup (don't free shared resources) */
    sub_loader.loading = NULL;
    sub_loader.loading_count = 0;
    sub_loader.loading_capacity = 0;
    sub_loader.loaded = NULL;
    nova_module_loader_free(&sub_loader);
    nova_interpreter_free(&mod_interp);

    /*
     * NOTE: We intentionally do NOT free the parser, lexer, or source here.
     * Exported ObjFunction objects contain pointers to AST nodes owned by
     * the parser's arena. Freeing the parser would create dangling pointers
     * and cause crashes when those functions are called. The memory lives
     * for the lifetime of the program (bounded: one allocation per module).
     */
    free(path_copy);
    return true;
}

void nova_module_load(ModuleLoader *loader, Interpreter *interp,
                      const char *module_name, int line) {
    /* Check if already loaded */
    ModuleEntry *cached = find_loaded(loader, module_name);
    if (cached) {
        import_as_namespace(module_name, &cached->exports, nova_current_env(interp));
        return;
    }

    /* Check for circular import */
    if (is_loading(loader, module_name)) {
        nova_runtime_error(line, "Circular import detected: '%s'", module_name);
    }

    push_loading(loader, module_name);

    /* Try stdlib C module first */
    if (loader->exe_dir) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/stdlib/math.%s",
                 loader->exe_dir,
#ifdef __APPLE__
                 "dylib"
#else
                 "so"
#endif
        );
        /* For now, just try math specifically */
        if (strcmp(module_name, "math") == 0) {
            if (load_c_module(loader, interp, module_name, path, line)) {
                pop_loading(loader);
                return;
            }
        }
    }

    /* Try stdlib in build directory (look relative to executable) */
    {
        char path[1024];
        /* Try relative to exe_dir */
        if (loader->exe_dir) {
            snprintf(path, sizeof(path), "%s/stdlib/%s.%s",
                     loader->exe_dir, module_name,
#ifdef __APPLE__
                     "dylib"
#else
                     "so"
#endif
            );
            if (load_c_module(loader, interp, module_name, path, line)) {
                pop_loading(loader);
                return;
            }
        }
    }

    /* Try .nova file relative to base_dir */
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s.nova", loader->base_dir, module_name);
        if (load_nova_module(loader, interp, module_name, path, line)) {
            pop_loading(loader);
            return;
        }
    }

    /* Try nova_packages/ — check base_dir first, then project_root */
    const char *roots[2];
    int root_count = 0;
    roots[root_count++] = loader->base_dir;
    if (loader->project_root && strcmp(loader->project_root, loader->base_dir) != 0)
        roots[root_count++] = loader->project_root;

    for (int r = 0; r < root_count; r++) {
        char pkg_dir[1024];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/nova_packages/%s", roots[r], module_name);
        if (!dir_exists(pkg_dir)) continue;

        /* Try native C module in package directory */
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s.%s", pkg_dir, module_name,
#ifdef __APPLE__
                     "dylib"
#else
                     "so"
#endif
            );
            if (load_c_module(loader, interp, module_name, path, line)) {
                pop_loading(loader);
                return;
            }
        }

        /* Try nova.json "main" entry point first */
        char *pkg_main = read_pkg_main(pkg_dir);
        if (pkg_main) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", pkg_dir, pkg_main);
            free(pkg_main);
            if (load_nova_module(loader, interp, module_name, path, line)) {
                pop_loading(loader);
                return;
            }
        }

        /* Try main.nova */
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/main.nova", pkg_dir);
            if (load_nova_module(loader, interp, module_name, path, line)) {
                pop_loading(loader);
                return;
            }
        }

        /* Try <name>.nova */
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s.nova", pkg_dir, module_name);
            if (load_nova_module(loader, interp, module_name, path, line)) {
                pop_loading(loader);
                return;
            }
        }
    }

    pop_loading(loader);
    nova_runtime_error(line, "Module '%s' not found", module_name);
}
