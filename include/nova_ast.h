#ifndef NOVA_AST_H
#define NOVA_AST_H

#include "nova_common.h"
#include "nova_value.h"

typedef enum {
    AST_NUMBER_LITERAL,
    AST_STRING_LITERAL,
    AST_BOOL_LITERAL,
    AST_NONE_LITERAL,
    AST_LIST_LITERAL,
    AST_DICT_LITERAL,       /* new: {key: val, ...} */
    AST_IDENTIFIER,
    AST_BINARY_OP,
    AST_UNARY_OP,
    AST_ASSIGNMENT,
    AST_LET_DECLARATION,
    AST_BLOCK,
    AST_IF_STATEMENT,
    AST_WHILE_STATEMENT,
    AST_FOR_STATEMENT,
    AST_FUNCTION_DEF,
    AST_RETURN_STATEMENT,
    AST_BREAK_STATEMENT,
    AST_CONTINUE_STATEMENT,
    AST_FUNCTION_CALL,
    AST_CLASS_DEF,
    AST_MEMBER_ACCESS,
    AST_MEMBER_ASSIGN,
    AST_INDEX_ACCESS,
    AST_INDEX_ASSIGN,
    AST_IMPORT_STATEMENT,
    AST_PROGRAM,
    AST_LAMBDA,             /* new: (params) => expr */
    AST_TRY_CATCH,          /* new: try/catch */
    AST_STRING_INTERP,      /* new: "hello {name}" */
    AST_TERNARY,            /* new: a if cond else b */
} AstNodeType;

typedef enum {
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LTE, OP_GTE,
    OP_AND, OP_OR,
    OP_NEG, OP_NOT,
    OP_PIPE,
} OpType;

/* Else-if clause (stored inline in if statements) */
typedef struct {
    AstNode *condition;
    AstNode *body;
    int line;
} ElseIfClause;

/* Dict entry (key-value pair in literal) */
typedef struct {
    AstNode *key;
    AstNode *value;
} DictEntry;

struct AstNode {
    AstNodeType type;
    int line;

    union {
        /* AST_NUMBER_LITERAL */
        struct { double value; bool is_int; } number;

        /* AST_STRING_LITERAL */
        struct { char *value; } string;

        /* AST_BOOL_LITERAL */
        struct { bool value; } boolean;

        /* AST_IDENTIFIER */
        struct { char *name; } identifier;

        /* AST_BINARY_OP */
        struct { AstNode *left; OpType op; AstNode *right; } binary;

        /* AST_UNARY_OP */
        struct { OpType op; AstNode *operand; } unary;

        /* AST_ASSIGNMENT */
        struct { char *name; AstNode *value; } assignment;

        /* AST_LET_DECLARATION */
        struct { char *name; AstNode *value; } let;

        /* AST_BLOCK */
        struct { AstNode **stmts; int count; } block;

        /* AST_IF_STATEMENT */
        struct {
            AstNode *condition;
            AstNode *body;
            ElseIfClause *else_ifs;
            int else_if_count;
            AstNode *else_body;     /* NULL if no else */
        } if_stmt;

        /* AST_WHILE_STATEMENT */
        struct { AstNode *condition; AstNode *body; } while_stmt;

        /* AST_FOR_STATEMENT */
        struct { char *var_name; AstNode *iterable; AstNode *body; } for_stmt;

        /* AST_FUNCTION_DEF */
        struct {
            char *name;
            char **params;
            int param_count;
            AstNode *body;
        } func_def;

        /* AST_RETURN_STATEMENT */
        struct { AstNode *value; /* NULL if bare return */ } return_stmt;

        /* AST_FUNCTION_CALL */
        struct { AstNode *callee; AstNode **args; int arg_count; } call;

        /* AST_CLASS_DEF */
        struct {
            char *name;
            char *parent;       /* NULL if no parent */
            AstNode **methods;  /* array of AST_FUNCTION_DEF */
            int method_count;
        } class_def;

        /* AST_MEMBER_ACCESS */
        struct { AstNode *obj; char *member; } member_access;

        /* AST_MEMBER_ASSIGN */
        struct { AstNode *obj; char *member; AstNode *value; } member_assign;

        /* AST_INDEX_ACCESS */
        struct { AstNode *obj; AstNode *index; } index_access;

        /* AST_INDEX_ASSIGN */
        struct { AstNode *obj; AstNode *index; AstNode *value; } index_assign;

        /* AST_IMPORT_STATEMENT */
        struct { char *module_name; } import_stmt;

        /* AST_LIST_LITERAL */
        struct { AstNode **elements; int count; } list;

        /* AST_DICT_LITERAL */
        struct { DictEntry *entries; int count; } dict;

        /* AST_PROGRAM */
        struct { AstNode **stmts; int count; } program;

        /* AST_LAMBDA */
        struct {
            char **params;
            int param_count;
            AstNode *body;      /* single expression */
        } lambda;

        /* AST_TRY_CATCH */
        struct {
            AstNode *try_body;
            char *error_name;   /* variable name in catch */
            AstNode *catch_body;
        } try_catch;

        /* AST_STRING_INTERP */
        struct {
            AstNode **parts;    /* alternating: string, expr, string, expr, ... string */
            int count;
        } interp;

        /* AST_TERNARY */
        struct {
            AstNode *true_expr;
            AstNode *condition;
            AstNode *false_expr;
        } ternary;
    } as;
};

/* Arena allocator for AST nodes */
typedef struct AstArena {
    char *buffer;
    size_t used;
    size_t capacity;
    struct AstArena *next;
} AstArena;

void     nova_arena_init(AstArena *arena);
void     nova_arena_free(AstArena *arena);
void    *nova_arena_alloc(AstArena *arena, size_t size);
AstNode *nova_ast_new(AstArena *arena, AstNodeType type, int line);
char    *nova_arena_strdup(AstArena *arena, const char *s);
char    *nova_arena_strndup(AstArena *arena, const char *s, int len);

#endif
