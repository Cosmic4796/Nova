#include "nova.h"
#include <sys/stat.h>
#include <dirent.h>

static NovaValue io_read_file(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "read_file() expects a string path");
    const char *path = AS_STRING(argv[0])->chars;

    FILE *f = fopen(path, "r");
    if (!f) nova_runtime_error(line, "Cannot open file '%s'", path);

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc(size + 1);
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);

    ObjString *s = nova_string_take(buf, (int)read);
    return NOVA_OBJ(s);
}

static NovaValue io_write_file(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "write_file() expects a string path");
    if (!IS_STRING(argv[1]))
        nova_type_error(line, "write_file() expects string data");
    const char *path = AS_STRING(argv[0])->chars;
    ObjString *data = AS_STRING(argv[1]);

    FILE *f = fopen(path, "w");
    if (!f) nova_runtime_error(line, "Cannot write to file '%s'", path);

    fwrite(data->chars, 1, data->length, f);
    fclose(f);
    return NOVA_NONE();
}

static NovaValue io_file_exists(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "file_exists() expects a string path");
    struct stat st;
    return NOVA_BOOL(stat(AS_STRING(argv[0])->chars, &st) == 0);
}

static NovaValue io_remove_file(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "remove_file() expects a string path");
    const char *path = AS_STRING(argv[0])->chars;
    if (remove(path) != 0)
        nova_runtime_error(line, "Cannot remove file '%s'", path);
    return NOVA_NONE();
}

static NovaValue io_list_dir(Interpreter *interp, int argc, NovaValue *argv, int line) {
    if (!IS_STRING(argv[0]))
        nova_type_error(line, "list_dir() expects a string path");
    const char *path = AS_STRING(argv[0])->chars;

    DIR *d = opendir(path);
    if (!d) nova_runtime_error(line, "Cannot open directory '%s'", path);

    ObjList *list = nova_list_new();
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        ObjString *name = nova_string_copy(ent->d_name, strlen(ent->d_name));
        nova_list_push(list, NOVA_OBJ(name));
    }
    closedir(d);
    return NOVA_OBJ(list);
}

void nova_module_init(Interpreter *interp, Environment *env) {
    nova_env_define(env, "read_file",   NOVA_OBJ(nova_builtin_new("read_file",   io_read_file,   1)));
    nova_env_define(env, "write_file",  NOVA_OBJ(nova_builtin_new("write_file",  io_write_file,  2)));
    nova_env_define(env, "file_exists", NOVA_OBJ(nova_builtin_new("file_exists", io_file_exists, 1)));
    nova_env_define(env, "remove_file", NOVA_OBJ(nova_builtin_new("remove_file", io_remove_file, 1)));
    nova_env_define(env, "list_dir",    NOVA_OBJ(nova_builtin_new("list_dir",    io_list_dir,    1)));
}
