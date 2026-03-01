#include "nova.h"

/* ── Interpreter init/free ── */

void nova_interpreter_init(Interpreter *interp, ModuleLoader *loader) {
    interp->globals = nova_env_new(NULL);
    interp->env_stack = NULL;
    interp->env_stack_count = 0;
    interp->env_stack_capacity = 0;
    interp->module_loader = loader;
    nova_register_builtins(interp);
}

void nova_interpreter_free(Interpreter *interp) {
    if (interp->env_stack) free(interp->env_stack);
}

Environment *nova_current_env(Interpreter *interp) {
    if (interp->env_stack_count == 0) return interp->globals;
    return interp->env_stack[interp->env_stack_count - 1];
}

void nova_push_env(Interpreter *interp, Environment *env) {
    if (interp->env_stack_count >= interp->env_stack_capacity) {
        int old = interp->env_stack_capacity;
        interp->env_stack_capacity = old < 8 ? 8 : old * 2;
        interp->env_stack = realloc(interp->env_stack,
                                    sizeof(Environment*) * interp->env_stack_capacity);
    }
    interp->env_stack[interp->env_stack_count++] = env;
}

void nova_pop_env(Interpreter *interp) {
    if (interp->env_stack_count > 0) interp->env_stack_count--;
}

static NovaValue run_in_env(Interpreter *interp, AstNode *block, Environment *env) {
    nova_push_env(interp, env);
    NovaValue result = NOVA_NONE();
    for (int i = 0; i < block->as.block.count; i++) {
        result = nova_interpret(interp, block->as.block.stmts[i]);
    }
    nova_pop_env(interp);
    return result;
}

/* ── Numeric operations ── */

static NovaValue numeric_op(NovaValue left, NovaValue right, OpType op, int line) {
    if (!IS_NUMBER(left) || !IS_NUMBER(right)) {
        const char *opname = "+";
        switch (op) {
            case OP_ADD: opname = "+"; break;
            case OP_SUB: opname = "-"; break;
            case OP_MUL: opname = "*"; break;
            case OP_DIV: opname = "/"; break;
            case OP_MOD: opname = "%"; break;
            default: break;
        }
        nova_type_error(line, "Unsupported operand types for '%s'", opname);
    }

    if (IS_INT(left) && IS_INT(right)) {
        int64_t l = AS_INT(left), r = AS_INT(right);
        switch (op) {
            case OP_ADD: return NOVA_INT(l + r);
            case OP_SUB: return NOVA_INT(l - r);
            case OP_MUL: return NOVA_INT(l * r);
            case OP_DIV: {
                if (r == 0) nova_runtime_error(line, "Division by zero");
                double result = (double)l / (double)r;
                if (result == (double)(int64_t)result)
                    return NOVA_INT((int64_t)result);
                return NOVA_FLOAT(result);
            }
            case OP_MOD:
                if (r == 0) nova_runtime_error(line, "Modulo by zero");
                return NOVA_INT(l % r);
            default: break;
        }
    }

    double l = AS_NUMBER(left), r = AS_NUMBER(right);
    switch (op) {
        case OP_ADD: return NOVA_FLOAT(l + r);
        case OP_SUB: return NOVA_FLOAT(l - r);
        case OP_MUL: return NOVA_FLOAT(l * r);
        case OP_DIV:
            if (r == 0) nova_runtime_error(line, "Division by zero");
            return NOVA_FLOAT(l / r);
        case OP_MOD:
            if (r == 0) nova_runtime_error(line, "Modulo by zero");
            return NOVA_FLOAT(fmod(l, r));
        default: break;
    }
    return NOVA_NONE();
}

/* ── Call a function ── */

static NovaValue call_function(Interpreter *interp, ObjFunction *func,
                               int argc, NovaValue *argv, int line,
                               ObjInstance *instance) {
    if (argc != func->arity) {
        nova_runtime_error(line, "%s() takes %d argument(s), got %d",
                           func->name->chars, func->arity, argc);
    }

    Environment *env = nova_env_new(func->closure);
    if (instance) nova_env_define(env, "self", NOVA_OBJ(instance));
    for (int i = 0; i < argc; i++)
        nova_env_define(env, func->params[i], argv[i]);

    if (func->is_lambda) {
        nova_push_env(interp, env);
        NovaValue result = nova_interpret(interp, func->body);
        nova_pop_env(interp);
        return result;
    }

    SignalContext sig;
    sig.prev = nova_signal_ctx;
    sig.signal_type = SIGNAL_RETURN;
    sig.return_value = NOVA_NONE();
    SignalContext *old_signal = nova_signal_ctx;
    int saved_env_count = interp->env_stack_count;

    int jmp = setjmp(sig.jump);
    if (jmp == SIGNAL_RETURN) {
        interp->env_stack_count = saved_env_count;
        nova_signal_ctx = old_signal;
        return sig.return_value;
    } else if (jmp == SIGNAL_BREAK || jmp == SIGNAL_CONTINUE) {
        interp->env_stack_count = saved_env_count;
        nova_signal_ctx = old_signal;
        if (nova_signal_ctx) {
            nova_signal_ctx->signal_type = (SignalType)jmp;
            longjmp(nova_signal_ctx->jump, jmp);
        }
        nova_runtime_error(line, jmp == SIGNAL_BREAK ? "break outside loop" : "continue outside loop");
    } else if (jmp == SIGNAL_TRY_ERROR) {
        interp->env_stack_count = saved_env_count;
        nova_signal_ctx = old_signal;
        if (nova_signal_ctx) {
            memcpy(nova_signal_ctx->error_msg, sig.error_msg, sizeof(sig.error_msg));
            nova_signal_ctx->signal_type = SIGNAL_TRY_ERROR;
            longjmp(nova_signal_ctx->jump, SIGNAL_TRY_ERROR);
        }
        if (nova_error_ctx) {
            strncpy(nova_error_ctx->message, sig.error_msg, sizeof(nova_error_ctx->message));
            longjmp(nova_error_ctx->jump, 1);
        }
        fprintf(stderr, "%s\n", sig.error_msg);
        exit(1);
    }

    nova_signal_ctx = &sig;
    nova_push_env(interp, env);
    for (int i = 0; i < func->body->as.block.count; i++)
        nova_interpret(interp, func->body->as.block.stmts[i]);
    nova_pop_env(interp);
    nova_signal_ctx = old_signal;
    return NOVA_NONE();
}

NovaValue nova_call_value(Interpreter *interp, NovaValue callee, int argc,
                          NovaValue *argv, int line) {
    if (IS_BUILTIN(callee)) {
        ObjBuiltin *b = AS_BUILTIN(callee);
        if (b->arity >= 0 && argc != b->arity)
            nova_runtime_error(line, "%s() takes %d argument(s), got %d",
                               b->name, b->arity, argc);
        return b->fn(interp, argc, argv, line);
    }
    if (IS_CLASS(callee)) {
        ObjClass *klass = AS_CLASS(callee);
        ObjInstance *inst = nova_instance_new(klass);
        ObjString *init_name = nova_string_copy("init", 4);
        ObjFunction *init = nova_class_find_method(klass, init_name);
        if (init)
            call_function(interp, init, argc, argv, line, inst);
        else if (argc > 0)
            nova_runtime_error(line, "%s() takes 0 arguments (no init method), got %d",
                               klass->name->chars, argc);
        return NOVA_OBJ(inst);
    }
    if (IS_BOUND_METHOD(callee)) {
        ObjBoundMethod *bm = AS_BOUND_METHOD(callee);
        return call_function(interp, bm->method, argc, argv, line, bm->instance);
    }
    if (IS_FUNCTION(callee))
        return call_function(interp, AS_FUNCTION(callee), argc, argv, line, NULL);

    char *repr = nova_repr(callee);
    nova_type_error(line, "'%s' is not callable", repr);
    free(repr);
    return NOVA_NONE();
}

/* ── Handle method calls on list/string/dict ── */

static NovaValue dispatch_method_call(Interpreter *interp, AstNode *node,
                                      NovaValue obj, const char *method) {
    int argc = node->as.call.arg_count;

    /* List methods */
    if (IS_LIST(obj)) {
        ObjList *list = AS_LIST(obj);
        if (strcmp(method, "push") == 0) {
            if (argc != 1) nova_runtime_error(node->line, "push() takes 1 argument");
            nova_list_push(list, nova_interpret(interp, node->as.call.args[0]));
            return NOVA_NONE();
        }
        if (strcmp(method, "pop") == 0) {
            if (argc != 0) nova_runtime_error(node->line, "pop() takes 0 arguments");
            if (list->count == 0) nova_runtime_error(node->line, "Cannot pop from empty list");
            return nova_list_pop(list);
        }
        nova_runtime_error(node->line, "List has no method '%s'", method);
    }

    /* String methods */
    if (IS_STRING(obj)) {
        ObjString *str = AS_STRING(obj);
        if (strcmp(method, "upper") == 0) {
            if (argc != 0) nova_runtime_error(node->line, "upper() takes 0 arguments");
            char *buf = malloc(str->length + 1);
            for (int i = 0; i < str->length; i++)
                buf[i] = toupper((unsigned char)str->chars[i]);
            buf[str->length] = '\0';
            return NOVA_OBJ(nova_string_take(buf, str->length));
        }
        if (strcmp(method, "lower") == 0) {
            if (argc != 0) nova_runtime_error(node->line, "lower() takes 0 arguments");
            char *buf = malloc(str->length + 1);
            for (int i = 0; i < str->length; i++)
                buf[i] = tolower((unsigned char)str->chars[i]);
            buf[str->length] = '\0';
            return NOVA_OBJ(nova_string_take(buf, str->length));
        }
        if (strcmp(method, "split") == 0) {
            ObjList *result = nova_list_new();
            if (argc == 0) {
                const char *s = str->chars;
                int len = str->length, i = 0;
                while (i < len) {
                    while (i < len && isspace((unsigned char)s[i])) i++;
                    if (i >= len) break;
                    int start = i;
                    while (i < len && !isspace((unsigned char)s[i])) i++;
                    nova_list_push(result, NOVA_OBJ(nova_string_copy(s + start, i - start)));
                }
            } else if (argc == 1) {
                NovaValue sep_val = nova_interpret(interp, node->as.call.args[0]);
                if (!IS_STRING(sep_val))
                    nova_type_error(node->line, "split() separator must be a string");
                ObjString *sep = AS_STRING(sep_val);
                const char *s = str->chars;
                int len = str->length, sep_len = sep->length, start = 0;
                for (int i = 0; i <= len - sep_len; i++) {
                    if (memcmp(s + i, sep->chars, sep_len) == 0) {
                        nova_list_push(result, NOVA_OBJ(nova_string_copy(s + start, i - start)));
                        i += sep_len - 1;
                        start = i + 1;
                    }
                }
                nova_list_push(result, NOVA_OBJ(nova_string_copy(s + start, len - start)));
            } else {
                nova_runtime_error(node->line, "split() takes 0-1 arguments");
            }
            return NOVA_OBJ(result);
        }
        nova_runtime_error(node->line, "String has no method '%s'", method);
    }

    /* Dict methods + namespace callable lookup */
    if (IS_DICT(obj)) {
        ObjDict *dict = AS_DICT(obj);
        if (strcmp(method, "keys") == 0) {
            if (argc != 0) nova_runtime_error(node->line, "keys() takes 0 arguments");
            ObjList *result = nova_list_new();
            TableIter it; ObjString *key; NovaValue val;
            nova_table_iter_init(&it, &dict->table);
            while (nova_table_iter_next(&it, &key, &val))
                nova_list_push(result, NOVA_OBJ(key));
            return NOVA_OBJ(result);
        }
        if (strcmp(method, "values") == 0) {
            if (argc != 0) nova_runtime_error(node->line, "values() takes 0 arguments");
            ObjList *result = nova_list_new();
            TableIter it; ObjString *key; NovaValue val;
            nova_table_iter_init(&it, &dict->table);
            while (nova_table_iter_next(&it, &key, &val))
                nova_list_push(result, val);
            return NOVA_OBJ(result);
        }
        if (strcmp(method, "items") == 0) {
            if (argc != 0) nova_runtime_error(node->line, "items() takes 0 arguments");
            ObjList *result = nova_list_new();
            TableIter it; ObjString *key; NovaValue val;
            nova_table_iter_init(&it, &dict->table);
            while (nova_table_iter_next(&it, &key, &val)) {
                ObjList *pair = nova_list_new();
                nova_list_push(pair, NOVA_OBJ(key));
                nova_list_push(pair, val);
                nova_list_push(result, NOVA_OBJ(pair));
            }
            return NOVA_OBJ(result);
        }
        /* Look up callable value in dict (module namespace support) */
        ObjString *mkey = nova_string_copy(method, strlen(method));
        NovaValue mval;
        if (nova_table_get(&dict->table, mkey, &mval)) {
            NovaValue *argv = NULL;
            if (argc > 0) {
                argv = malloc(sizeof(NovaValue) * argc);
                for (int i = 0; i < argc; i++)
                    argv[i] = nova_interpret(interp, node->as.call.args[i]);
            }
            NovaValue result = nova_call_value(interp, mval, argc, argv, node->line);
            free(argv);
            return result;
        }
        nova_runtime_error(node->line, "Dict has no method '%s'", method);
    }

    /* Instance: get method, then call normally */
    if (IS_INSTANCE(obj)) {
        NovaValue callee_val = nova_instance_get(AS_INSTANCE(obj), method, node->line);
        NovaValue *argv = NULL;
        if (argc > 0) {
            argv = malloc(sizeof(NovaValue) * argc);
            for (int i = 0; i < argc; i++)
                argv[i] = nova_interpret(interp, node->as.call.args[i]);
        }
        NovaValue result = nova_call_value(interp, callee_val, argc, argv, node->line);
        free(argv);
        return result;
    }

    nova_type_error(node->line, "Cannot call method '%s' on this type", method);
    return NOVA_NONE();
}

/* ── Main interpreter dispatch ── */

NovaValue nova_interpret(Interpreter *interp, AstNode *node) {
    if (!node) return NOVA_NONE();

    switch (node->type) {

    case AST_NUMBER_LITERAL:
        if (node->as.number.is_int) return NOVA_INT((int64_t)node->as.number.value);
        return NOVA_FLOAT(node->as.number.value);

    case AST_STRING_LITERAL:
        return NOVA_OBJ(nova_string_copy(node->as.string.value, strlen(node->as.string.value)));

    case AST_BOOL_LITERAL:
        return NOVA_BOOL(node->as.boolean.value);

    case AST_NONE_LITERAL:
        return NOVA_NONE();

    case AST_LIST_LITERAL: {
        ObjList *list = nova_list_new();
        for (int i = 0; i < node->as.list.count; i++)
            nova_list_push(list, nova_interpret(interp, node->as.list.elements[i]));
        return NOVA_OBJ(list);
    }

    case AST_DICT_LITERAL: {
        ObjDict *dict = nova_dict_new();
        for (int i = 0; i < node->as.dict.count; i++) {
            NovaValue key = nova_interpret(interp, node->as.dict.entries[i].key);
            NovaValue val = nova_interpret(interp, node->as.dict.entries[i].value);
            if (!IS_STRING(key)) nova_type_error(node->line, "Dict keys must be strings");
            nova_table_set(&dict->table, AS_STRING(key), val);
        }
        return NOVA_OBJ(dict);
    }

    case AST_IDENTIFIER:
        return nova_env_get(nova_current_env(interp), node->as.identifier.name, node->line);

    case AST_LET_DECLARATION: {
        NovaValue value = nova_interpret(interp, node->as.let.value);
        nova_env_define(nova_current_env(interp), node->as.let.name, value);
        return value;
    }

    case AST_ASSIGNMENT: {
        NovaValue value = nova_interpret(interp, node->as.assignment.value);
        nova_env_set(nova_current_env(interp), node->as.assignment.name, value, node->line);
        return value;
    }

    case AST_BINARY_OP: {
        if (node->as.binary.op == OP_AND) {
            NovaValue left = nova_interpret(interp, node->as.binary.left);
            if (!nova_truthy(left)) return left;
            return nova_interpret(interp, node->as.binary.right);
        }
        if (node->as.binary.op == OP_OR) {
            NovaValue left = nova_interpret(interp, node->as.binary.left);
            if (nova_truthy(left)) return left;
            return nova_interpret(interp, node->as.binary.right);
        }

        NovaValue left = nova_interpret(interp, node->as.binary.left);
        NovaValue right = nova_interpret(interp, node->as.binary.right);

        switch (node->as.binary.op) {
        case OP_ADD:
            if (IS_STRING(left) || IS_STRING(right)) {
                char *lr = nova_repr(left), *rr = nova_repr(right);
                int len = strlen(lr) + strlen(rr);
                char *buf = malloc(len + 1);
                memcpy(buf, lr, strlen(lr));
                memcpy(buf + strlen(lr), rr, strlen(rr) + 1);
                free(lr); free(rr);
                return NOVA_OBJ(nova_string_take(buf, len));
            }
            return numeric_op(left, right, OP_ADD, node->line);
        case OP_SUB: return numeric_op(left, right, OP_SUB, node->line);
        case OP_MUL:
            if (IS_STRING(left) && IS_INT(right))
                return NOVA_OBJ(nova_string_repeat(AS_STRING(left), AS_INT(right)));
            if (IS_INT(left) && IS_STRING(right))
                return NOVA_OBJ(nova_string_repeat(AS_STRING(right), AS_INT(left)));
            return numeric_op(left, right, OP_MUL, node->line);
        case OP_DIV: return numeric_op(left, right, OP_DIV, node->line);
        case OP_MOD: return numeric_op(left, right, OP_MOD, node->line);
        case OP_EQ:  return NOVA_BOOL(nova_equal(left, right));
        case OP_NEQ: return NOVA_BOOL(!nova_equal(left, right));
        case OP_LT:
            if (IS_NUMBER(left) && IS_NUMBER(right))
                return NOVA_BOOL(AS_NUMBER(left) < AS_NUMBER(right));
            if (IS_STRING(left) && IS_STRING(right))
                return NOVA_BOOL(strcmp(AS_CSTRING(left), AS_CSTRING(right)) < 0);
            nova_type_error(node->line, "Cannot compare these types");
        case OP_GT:
            if (IS_NUMBER(left) && IS_NUMBER(right))
                return NOVA_BOOL(AS_NUMBER(left) > AS_NUMBER(right));
            if (IS_STRING(left) && IS_STRING(right))
                return NOVA_BOOL(strcmp(AS_CSTRING(left), AS_CSTRING(right)) > 0);
            nova_type_error(node->line, "Cannot compare these types");
        case OP_LTE:
            if (IS_NUMBER(left) && IS_NUMBER(right))
                return NOVA_BOOL(AS_NUMBER(left) <= AS_NUMBER(right));
            nova_type_error(node->line, "Cannot compare these types");
        case OP_GTE:
            if (IS_NUMBER(left) && IS_NUMBER(right))
                return NOVA_BOOL(AS_NUMBER(left) >= AS_NUMBER(right));
            nova_type_error(node->line, "Cannot compare these types");
        default: break;
        }
        return NOVA_NONE();
    }

    case AST_UNARY_OP: {
        NovaValue operand = nova_interpret(interp, node->as.unary.operand);
        if (node->as.unary.op == OP_NEG) {
            if (IS_INT(operand)) return NOVA_INT(-AS_INT(operand));
            if (IS_FLOAT(operand)) return NOVA_FLOAT(-AS_FLOAT(operand));
            nova_type_error(node->line, "Unary '-' requires a number");
        }
        if (node->as.unary.op == OP_NOT)
            return NOVA_BOOL(!nova_truthy(operand));
        return NOVA_NONE();
    }

    case AST_IF_STATEMENT: {
        NovaValue cond = nova_interpret(interp, node->as.if_stmt.condition);
        if (nova_truthy(cond)) {
            Environment *env = nova_env_new(nova_current_env(interp));
            return run_in_env(interp, node->as.if_stmt.body, env);
        }
        for (int i = 0; i < node->as.if_stmt.else_if_count; i++) {
            ElseIfClause *ei = &node->as.if_stmt.else_ifs[i];
            cond = nova_interpret(interp, ei->condition);
            if (nova_truthy(cond)) {
                Environment *env = nova_env_new(nova_current_env(interp));
                return run_in_env(interp, ei->body, env);
            }
        }
        if (node->as.if_stmt.else_body) {
            Environment *env = nova_env_new(nova_current_env(interp));
            return run_in_env(interp, node->as.if_stmt.else_body, env);
        }
        return NOVA_NONE();
    }

    case AST_WHILE_STATEMENT: {
        NovaValue result = NOVA_NONE();
        SignalContext sig;
        sig.prev = nova_signal_ctx;
        SignalContext *old_signal = nova_signal_ctx;
        nova_signal_ctx = &sig;

        while (true) {
            NovaValue cond = nova_interpret(interp, node->as.while_stmt.condition);
            if (!nova_truthy(cond)) break;

            int jmp = setjmp(sig.jump);
            if (jmp == SIGNAL_BREAK) break;
            else if (jmp == SIGNAL_CONTINUE) continue;
            else if (jmp == SIGNAL_RETURN) {
                nova_signal_ctx = old_signal;
                if (nova_signal_ctx) {
                    nova_signal_ctx->return_value = sig.return_value;
                    nova_signal_ctx->signal_type = SIGNAL_RETURN;
                    longjmp(nova_signal_ctx->jump, SIGNAL_RETURN);
                }
                return sig.return_value;
            } else if (jmp == SIGNAL_TRY_ERROR) {
                nova_signal_ctx = old_signal;
                if (nova_signal_ctx) {
                    memcpy(nova_signal_ctx->error_msg, sig.error_msg, sizeof(sig.error_msg));
                    nova_signal_ctx->signal_type = SIGNAL_TRY_ERROR;
                    longjmp(nova_signal_ctx->jump, SIGNAL_TRY_ERROR);
                }
                if (nova_error_ctx) {
                    strncpy(nova_error_ctx->message, sig.error_msg, sizeof(nova_error_ctx->message));
                    longjmp(nova_error_ctx->jump, 1);
                }
                fprintf(stderr, "%s\n", sig.error_msg);
                exit(1);
            }

            Environment *env = nova_env_new(nova_current_env(interp));
            result = run_in_env(interp, node->as.while_stmt.body, env);
        }
        nova_signal_ctx = old_signal;
        return result;
    }

    case AST_FOR_STATEMENT: {
        NovaValue iter_val = nova_interpret(interp, node->as.for_stmt.iterable);
        int item_count = 0;
        NovaValue *items = NULL;
        bool free_items = false;

        if (IS_LIST(iter_val)) {
            ObjList *list = AS_LIST(iter_val);
            item_count = list->count;
            items = list->items;
        } else if (IS_STRING(iter_val)) {
            ObjString *s = AS_STRING(iter_val);
            item_count = s->length;
            items = malloc(sizeof(NovaValue) * item_count);
            free_items = true;
            for (int i = 0; i < s->length; i++)
                items[i] = NOVA_OBJ(nova_string_copy(&s->chars[i], 1));
        } else if (IS_DICT(iter_val)) {
            ObjDict *dict = AS_DICT(iter_val);
            items = malloc(sizeof(NovaValue) * dict->table.count);
            free_items = true;
            TableIter it; ObjString *key; NovaValue val; int idx = 0;
            nova_table_iter_init(&it, &dict->table);
            while (nova_table_iter_next(&it, &key, &val))
                items[idx++] = NOVA_OBJ(key);
            item_count = idx;
        } else {
            nova_type_error(node->line, "for loop requires an iterable (list, string, or dict)");
        }

        NovaValue result = NOVA_NONE();
        SignalContext sig;
        sig.prev = nova_signal_ctx;
        SignalContext *old_signal = nova_signal_ctx;
        nova_signal_ctx = &sig;

        for (int i = 0; i < item_count; i++) {
            int jmp = setjmp(sig.jump);
            if (jmp == SIGNAL_BREAK) goto for_done;
            else if (jmp == SIGNAL_CONTINUE) continue;
            else if (jmp == SIGNAL_RETURN) {
                nova_signal_ctx = old_signal;
                if (free_items) free(items);
                if (nova_signal_ctx) {
                    nova_signal_ctx->return_value = sig.return_value;
                    nova_signal_ctx->signal_type = SIGNAL_RETURN;
                    longjmp(nova_signal_ctx->jump, SIGNAL_RETURN);
                }
                return sig.return_value;
            } else if (jmp == SIGNAL_TRY_ERROR) {
                nova_signal_ctx = old_signal;
                if (free_items) free(items);
                if (nova_signal_ctx) {
                    memcpy(nova_signal_ctx->error_msg, sig.error_msg, sizeof(sig.error_msg));
                    nova_signal_ctx->signal_type = SIGNAL_TRY_ERROR;
                    longjmp(nova_signal_ctx->jump, SIGNAL_TRY_ERROR);
                }
                if (nova_error_ctx) {
                    strncpy(nova_error_ctx->message, sig.error_msg, sizeof(nova_error_ctx->message));
                    longjmp(nova_error_ctx->jump, 1);
                }
                fprintf(stderr, "%s\n", sig.error_msg);
                exit(1);
            }

            Environment *env = nova_env_new(nova_current_env(interp));
            nova_env_define(env, node->as.for_stmt.var_name, items[i]);
            result = run_in_env(interp, node->as.for_stmt.body, env);
        }
    for_done:
        nova_signal_ctx = old_signal;
        if (free_items) free(items);
        return result;
    }

    case AST_BREAK_STATEMENT:
        if (nova_signal_ctx) {
            nova_signal_ctx->signal_type = SIGNAL_BREAK;
            longjmp(nova_signal_ctx->jump, SIGNAL_BREAK);
        }
        nova_runtime_error(node->line, "break outside loop");
        return NOVA_NONE();

    case AST_CONTINUE_STATEMENT:
        if (nova_signal_ctx) {
            nova_signal_ctx->signal_type = SIGNAL_CONTINUE;
            longjmp(nova_signal_ctx->jump, SIGNAL_CONTINUE);
        }
        nova_runtime_error(node->line, "continue outside loop");
        return NOVA_NONE();

    case AST_RETURN_STATEMENT: {
        NovaValue value = NOVA_NONE();
        if (node->as.return_stmt.value)
            value = nova_interpret(interp, node->as.return_stmt.value);
        if (nova_signal_ctx) {
            nova_signal_ctx->return_value = value;
            nova_signal_ctx->signal_type = SIGNAL_RETURN;
            longjmp(nova_signal_ctx->jump, SIGNAL_RETURN);
        }
        return value;
    }

    case AST_FUNCTION_DEF: {
        ObjString *name = nova_string_copy(node->as.func_def.name, strlen(node->as.func_def.name));
        int arity = node->as.func_def.param_count;
        char **params = NULL;
        if (arity > 0) {
            params = nova_alloc(sizeof(char*) * arity);
            for (int i = 0; i < arity; i++)
                params[i] = node->as.func_def.params[i];
        }
        ObjFunction *func = nova_function_new(name, arity, params,
                                              node->as.func_def.body,
                                              nova_current_env(interp));
        nova_env_define(nova_current_env(interp), node->as.func_def.name, NOVA_OBJ(func));
        return NOVA_OBJ(func);
    }

    case AST_FUNCTION_CALL: {
        AstNode *callee_node = node->as.call.callee;

        /* Method call on list/string/dict/instance */
        if (callee_node->type == AST_MEMBER_ACCESS) {
            NovaValue obj = nova_interpret(interp, callee_node->as.member_access.obj);
            const char *method = callee_node->as.member_access.member;
            return dispatch_method_call(interp, node, obj, method);
        }

        /* Normal function call */
        NovaValue callee = nova_interpret(interp, callee_node);
        int argc = node->as.call.arg_count;
        NovaValue *argv = NULL;
        if (argc > 0) {
            argv = malloc(sizeof(NovaValue) * argc);
            for (int i = 0; i < argc; i++)
                argv[i] = nova_interpret(interp, node->as.call.args[i]);
        }
        NovaValue result = nova_call_value(interp, callee, argc, argv, node->line);
        free(argv);
        return result;
    }

    case AST_CLASS_DEF: {
        ObjClass *parent = NULL;
        if (node->as.class_def.parent) {
            NovaValue pv = nova_env_get(nova_current_env(interp),
                                        node->as.class_def.parent, node->line);
            if (!IS_CLASS(pv))
                nova_type_error(node->line, "'%s' is not a class", node->as.class_def.parent);
            parent = AS_CLASS(pv);
        }
        ObjString *name = nova_string_copy(node->as.class_def.name, strlen(node->as.class_def.name));
        ObjClass *klass = nova_class_new(name, parent);

        for (int i = 0; i < node->as.class_def.method_count; i++) {
            AstNode *m = node->as.class_def.methods[i];
            ObjString *mname = nova_string_copy(m->as.func_def.name, strlen(m->as.func_def.name));
            int arity = m->as.func_def.param_count;
            char **params = NULL;
            if (arity > 0) {
                params = nova_alloc(sizeof(char*) * arity);
                for (int j = 0; j < arity; j++)
                    params[j] = m->as.func_def.params[j];
            }
            ObjFunction *func = nova_function_new(mname, arity, params,
                                                  m->as.func_def.body,
                                                  nova_current_env(interp));
            nova_table_set(&klass->methods, mname, NOVA_OBJ(func));
        }
        nova_env_define(nova_current_env(interp), node->as.class_def.name, NOVA_OBJ(klass));
        return NOVA_OBJ(klass);
    }

    case AST_MEMBER_ACCESS: {
        NovaValue obj = nova_interpret(interp, node->as.member_access.obj);
        const char *member = node->as.member_access.member;

        if (IS_LIST(obj)) {
            if (strcmp(member, "length") == 0) return NOVA_INT(AS_LIST(obj)->count);
            nova_runtime_error(node->line, "List has no attribute '%s'", member);
        }
        if (IS_STRING(obj)) {
            if (strcmp(member, "length") == 0) return NOVA_INT(AS_STRING(obj)->length);
            nova_runtime_error(node->line, "String has no attribute '%s'", member);
        }
        if (IS_DICT(obj)) {
            ObjDict *dict = AS_DICT(obj);
            /* Look up member as key in dict (enables module namespace access) */
            ObjString *mkey = nova_string_copy(member, strlen(member));
            NovaValue mval;
            if (nova_table_get(&dict->table, mkey, &mval)) return mval;
            if (strcmp(member, "length") == 0) return NOVA_INT(dict->table.count);
            nova_runtime_error(node->line, "Dict has no key '%s'", member);
        }
        if (IS_INSTANCE(obj))
            return nova_instance_get(AS_INSTANCE(obj), member, node->line);

        nova_type_error(node->line, "Cannot access member '%s' on this type", member);
        return NOVA_NONE();
    }

    case AST_MEMBER_ASSIGN: {
        NovaValue obj = nova_interpret(interp, node->as.member_assign.obj);
        if (!IS_INSTANCE(obj))
            nova_type_error(node->line, "Can only set attributes on class instances");
        NovaValue value = nova_interpret(interp, node->as.member_assign.value);
        nova_instance_set(AS_INSTANCE(obj), node->as.member_assign.member, value);
        return value;
    }

    case AST_INDEX_ACCESS: {
        NovaValue obj = nova_interpret(interp, node->as.index_access.obj);
        NovaValue index = nova_interpret(interp, node->as.index_access.index);

        if (IS_LIST(obj)) {
            ObjList *list = AS_LIST(obj);
            if (!IS_INT(index)) nova_type_error(node->line, "List index must be an integer");
            int64_t idx = AS_INT(index);
            if (idx < 0 || idx >= list->count)
                nova_runtime_error(node->line, "Index %lld out of range", (long long)idx);
            return list->items[idx];
        }
        if (IS_STRING(obj)) {
            ObjString *str = AS_STRING(obj);
            if (!IS_INT(index)) nova_type_error(node->line, "String index must be an integer");
            int64_t idx = AS_INT(index);
            if (idx < 0 || idx >= str->length)
                nova_runtime_error(node->line, "Index %lld out of range", (long long)idx);
            return NOVA_OBJ(nova_string_copy(&str->chars[idx], 1));
        }
        if (IS_DICT(obj)) {
            ObjDict *dict = AS_DICT(obj);
            if (!IS_STRING(index)) nova_type_error(node->line, "Dict key must be a string");
            NovaValue val;
            if (nova_table_get(&dict->table, AS_STRING(index), &val)) return val;
            nova_runtime_error(node->line, "Key '%s' not found in dict", AS_CSTRING(index));
        }
        nova_type_error(node->line, "Indexing requires a list, string, or dict");
        return NOVA_NONE();
    }

    case AST_INDEX_ASSIGN: {
        NovaValue obj = nova_interpret(interp, node->as.index_assign.obj);
        NovaValue index = nova_interpret(interp, node->as.index_assign.index);
        NovaValue value = nova_interpret(interp, node->as.index_assign.value);

        if (IS_LIST(obj)) {
            ObjList *list = AS_LIST(obj);
            if (!IS_INT(index)) nova_type_error(node->line, "List index must be an integer");
            int64_t idx = AS_INT(index);
            if (idx < 0 || idx >= list->count)
                nova_runtime_error(node->line, "Index %lld out of range", (long long)idx);
            list->items[idx] = value;
            return value;
        }
        if (IS_DICT(obj)) {
            ObjDict *dict = AS_DICT(obj);
            if (!IS_STRING(index)) nova_type_error(node->line, "Dict key must be a string");
            nova_table_set(&dict->table, AS_STRING(index), value);
            return value;
        }
        nova_type_error(node->line, "Index assignment requires a list or dict");
        return NOVA_NONE();
    }

    case AST_IMPORT_STATEMENT:
        if (!interp->module_loader)
            nova_runtime_error(node->line, "Import not available");
        nova_module_load(interp->module_loader, interp,
                         node->as.import_stmt.module_name, node->line);
        return NOVA_NONE();

    case AST_PROGRAM: {
        nova_push_env(interp, interp->globals);
        NovaValue result = NOVA_NONE();
        for (int i = 0; i < node->as.program.count; i++)
            result = nova_interpret(interp, node->as.program.stmts[i]);
        nova_pop_env(interp);
        return result;
    }

    case AST_BLOCK: {
        NovaValue result = NOVA_NONE();
        for (int i = 0; i < node->as.block.count; i++)
            result = nova_interpret(interp, node->as.block.stmts[i]);
        return result;
    }

    case AST_LAMBDA: {
        ObjString *name = nova_string_copy("<lambda>", 8);
        int arity = node->as.lambda.param_count;
        char **params = NULL;
        if (arity > 0) {
            params = nova_alloc(sizeof(char*) * arity);
            for (int i = 0; i < arity; i++)
                params[i] = node->as.lambda.params[i];
        }
        ObjFunction *func = nova_function_new(name, arity, params,
                                              node->as.lambda.body,
                                              nova_current_env(interp));
        func->is_lambda = true;
        return NOVA_OBJ(func);
    }

    case AST_TRY_CATCH: {
        SignalContext sig;
        sig.prev = nova_signal_ctx;
        SignalContext *old_signal = nova_signal_ctx;
        nova_signal_ctx = &sig;

        int jmp = setjmp(sig.jump);
        if (jmp == 0) {
            Environment *try_env = nova_env_new(nova_current_env(interp));
            run_in_env(interp, node->as.try_catch.try_body, try_env);
            nova_signal_ctx = old_signal;
        } else if (jmp == SIGNAL_TRY_ERROR) {
            nova_signal_ctx = old_signal;
            Environment *catch_env = nova_env_new(nova_current_env(interp));
            ObjString *err_str = nova_string_copy(sig.error_msg, strlen(sig.error_msg));
            nova_env_define(catch_env, node->as.try_catch.error_name, NOVA_OBJ(err_str));
            run_in_env(interp, node->as.try_catch.catch_body, catch_env);
        } else if (jmp == SIGNAL_RETURN) {
            nova_signal_ctx = old_signal;
            if (nova_signal_ctx) {
                nova_signal_ctx->return_value = sig.return_value;
                nova_signal_ctx->signal_type = SIGNAL_RETURN;
                longjmp(nova_signal_ctx->jump, SIGNAL_RETURN);
            }
        } else if (jmp == SIGNAL_BREAK || jmp == SIGNAL_CONTINUE) {
            nova_signal_ctx = old_signal;
            if (nova_signal_ctx) {
                nova_signal_ctx->signal_type = (SignalType)jmp;
                longjmp(nova_signal_ctx->jump, jmp);
            }
        }
        return NOVA_NONE();
    }

    case AST_STRING_INTERP: {
        int total_len = 0;
        int part_count = node->as.interp.count;
        char **parts = malloc(sizeof(char*) * part_count);

        for (int i = 0; i < part_count; i++) {
            NovaValue v = nova_interpret(interp, node->as.interp.parts[i]);
            parts[i] = nova_repr(v);
            total_len += strlen(parts[i]);
        }

        char *result = malloc(total_len + 1);
        char *p = result;
        for (int i = 0; i < part_count; i++) {
            int len = strlen(parts[i]);
            memcpy(p, parts[i], len);
            p += len;
            free(parts[i]);
        }
        *p = '\0';
        free(parts);
        return NOVA_OBJ(nova_string_take(result, total_len));
    }

    case AST_TERNARY: {
        NovaValue cond = nova_interpret(interp, node->as.ternary.condition);
        if (nova_truthy(cond))
            return nova_interpret(interp, node->as.ternary.true_expr);
        return nova_interpret(interp, node->as.ternary.false_expr);
    }

    default:
        nova_runtime_error(node->line, "Unknown AST node type: %d", node->type);
    }

    return NOVA_NONE();
}

NovaValue nova_interpret_program(Interpreter *interp, AstNode *program) {
    return nova_interpret(interp, program);
}
