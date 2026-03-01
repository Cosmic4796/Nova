#ifndef NOVA_INTERPRETER_H
#define NOVA_INTERPRETER_H

#include "nova_common.h"
#include "nova_value.h"
#include "nova_ast.h"
#include "nova_env.h"

/* Module loader forward decl */
typedef struct ModuleLoader ModuleLoader;

struct Interpreter {
    Environment  *globals;
    Environment **env_stack;
    int           env_stack_count;
    int           env_stack_capacity;
    ModuleLoader *module_loader;
};

void      nova_interpreter_init(Interpreter *interp, ModuleLoader *loader);
void      nova_interpreter_free(Interpreter *interp);
NovaValue nova_interpret(Interpreter *interp, AstNode *node);
NovaValue nova_interpret_program(Interpreter *interp, AstNode *program);

/* Internal: get current environment */
Environment *nova_current_env(Interpreter *interp);
void         nova_push_env(Interpreter *interp, Environment *env);
void         nova_pop_env(Interpreter *interp);

/* Call a callable value */
NovaValue nova_call_value(Interpreter *interp, NovaValue callee, int argc,
                          NovaValue *argv, int line);

#endif
