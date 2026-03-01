#ifndef NOVA_OBJECT_H
#define NOVA_OBJECT_H

#include "nova_common.h"
#include "nova_value.h"
#include "nova_table.h"

typedef enum {
    OBJ_STRING,
    OBJ_LIST,
    OBJ_DICT,
    OBJ_FUNCTION,
    OBJ_CLASS,
    OBJ_INSTANCE,
    OBJ_BOUND_METHOD,
    OBJ_BUILTIN,
} ObjType;

/* Common object header */
struct Obj {
    ObjType type;
    bool marked;
    Obj *next;      /* GC linked list */
};

/* String: interned, flexible array member */
struct ObjString {
    Obj obj;
    int length;
    uint32_t hash;
    char chars[];   /* flexible array */
};

/* List: dynamic array of NovaValues */
struct ObjList {
    Obj obj;
    int count;
    int capacity;
    NovaValue *items;
};

/* Dict: hash table of NovaValue -> NovaValue */
struct ObjDict {
    Obj obj;
    NovaTable table;
};

/* Function: user-defined */
struct ObjFunction {
    Obj obj;
    ObjString *name;
    int arity;
    char **params;      /* parameter names */
    AstNode *body;      /* AST block */
    Environment *closure;
    bool is_lambda;
};

/* Class */
struct ObjClass {
    Obj obj;
    ObjString *name;
    ObjClass *parent;
    NovaTable methods;  /* name -> ObjFunction */
};

/* Instance */
struct ObjInstance {
    Obj obj;
    ObjClass *klass;
    NovaTable fields;
};

/* Bound method */
struct ObjBoundMethod {
    Obj obj;
    ObjInstance *instance;
    ObjFunction *method;
};

/* Builtin C function */
typedef NovaValue (*BuiltinFn)(Interpreter *interp, int argc, NovaValue *argv, int line);

struct ObjBuiltin {
    Obj obj;
    const char *name;
    BuiltinFn fn;
    int arity;      /* -1 = variadic */
};

/* Object allocation */
ObjString *nova_string_copy(const char *chars, int length);
ObjString *nova_string_take(char *chars, int length);
ObjString *nova_string_concat(ObjString *a, ObjString *b);
ObjString *nova_string_repeat(ObjString *s, int64_t n);
ObjString *nova_string_format(const char *fmt, ...);

ObjList *nova_list_new(void);
void     nova_list_push(ObjList *list, NovaValue value);
NovaValue nova_list_pop(ObjList *list);

ObjDict *nova_dict_new(void);

ObjFunction *nova_function_new(ObjString *name, int arity, char **params,
                               AstNode *body, Environment *closure);
ObjClass *nova_class_new(ObjString *name, ObjClass *parent);
ObjInstance *nova_instance_new(ObjClass *klass);
ObjBoundMethod *nova_bound_method_new(ObjInstance *instance, ObjFunction *method);
ObjBuiltin *nova_builtin_new(const char *name, BuiltinFn fn, int arity);

/* Method lookup (walks inheritance chain) */
ObjFunction *nova_class_find_method(ObjClass *klass, ObjString *name);

/* Instance field/method access */
NovaValue nova_instance_get(ObjInstance *inst, const char *name, int line);
void nova_instance_set(ObjInstance *inst, const char *name, NovaValue value);

#endif
