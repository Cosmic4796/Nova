#ifndef NOVA_VALUE_H
#define NOVA_VALUE_H

#include "nova_common.h"

typedef enum {
    VAL_NONE,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ,
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        int64_t integer;
        double floating;
        Obj *obj;
    } as;
} NovaValue;

/* Constructors */
#define NOVA_NONE()        ((NovaValue){VAL_NONE,  {.integer = 0}})
#define NOVA_BOOL(v)       ((NovaValue){VAL_BOOL,  {.boolean = (v)}})
#define NOVA_INT(v)        ((NovaValue){VAL_INT,   {.integer = (v)}})
#define NOVA_FLOAT(v)      ((NovaValue){VAL_FLOAT, {.floating = (v)}})
#define NOVA_OBJ(o)        ((NovaValue){VAL_OBJ,   {.obj = (Obj*)(o)}})

/* Type checks */
#define IS_NONE(v)         ((v).type == VAL_NONE)
#define IS_BOOL(v)         ((v).type == VAL_BOOL)
#define IS_INT(v)          ((v).type == VAL_INT)
#define IS_FLOAT(v)        ((v).type == VAL_FLOAT)
#define IS_OBJ(v)          ((v).type == VAL_OBJ)
#define IS_NUMBER(v)       ((v).type == VAL_INT || (v).type == VAL_FLOAT)

/* Accessors */
#define AS_BOOL(v)         ((v).as.boolean)
#define AS_INT(v)          ((v).as.integer)
#define AS_FLOAT(v)        ((v).as.floating)
#define AS_OBJ(v)          ((v).as.obj)

/* Object type checks (defined after nova_object.h) */
#define IS_STRING(v)       (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STRING)
#define IS_LIST(v)         (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_LIST)
#define IS_DICT(v)         (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_DICT)
#define IS_FUNCTION(v)     (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_FUNCTION)
#define IS_CLASS(v)        (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_CLASS)
#define IS_INSTANCE(v)     (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_INSTANCE)
#define IS_BOUND_METHOD(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_BOUND_METHOD)
#define IS_BUILTIN(v)      (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_BUILTIN)

/* Object casts */
#define AS_STRING(v)       ((ObjString*)AS_OBJ(v))
#define AS_CSTRING(v)      (((ObjString*)AS_OBJ(v))->chars)
#define AS_LIST(v)         ((ObjList*)AS_OBJ(v))
#define AS_DICT(v)         ((ObjDict*)AS_OBJ(v))
#define AS_FUNCTION(v)     ((ObjFunction*)AS_OBJ(v))
#define AS_CLASS(v)        ((ObjClass*)AS_OBJ(v))
#define AS_INSTANCE(v)     ((ObjInstance*)AS_OBJ(v))
#define AS_BOUND_METHOD(v) ((ObjBoundMethod*)AS_OBJ(v))
#define AS_BUILTIN(v)      ((ObjBuiltin*)AS_OBJ(v))

/* Get a numeric value as double regardless of int/float */
static inline double AS_NUMBER(NovaValue v) {
    return v.type == VAL_INT ? (double)v.as.integer : v.as.floating;
}

/* Value operations */
char *nova_repr(NovaValue value);       /* Returns allocated string */
char *nova_display(NovaValue value);    /* Like repr but quotes strings */
bool  nova_truthy(NovaValue value);
bool  nova_equal(NovaValue a, NovaValue b);
void  nova_print_value(NovaValue value);

#endif
