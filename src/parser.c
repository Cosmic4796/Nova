#include "nova.h"

/* ── Helpers ── */

static Token *peek(Parser *p) {
    return &p->tokens[p->pos];
}

static bool at(Parser *p, TokenType type) {
    return peek(p)->type == type;
}

static Token *advance_tok(Parser *p) {
    Token *t = &p->tokens[p->pos];
    if (p->pos < p->count - 1) p->pos++;
    return t;
}

static Token *expect(Parser *p, TokenType type, const char *msg) {
    Token *t = peek(p);
    if (t->type != type) {
        nova_syntax_error(t->line, "%s (got %s)", msg, token_type_name(t->type));
    }
    return advance_tok(p);
}

static void skip_newlines(Parser *p) {
    while (at(p, TOK_NEWLINE)) advance_tok(p);
}

/* Forward declarations */
static AstNode *parse_statement(Parser *p);
static AstNode *parse_expression(Parser *p);
static AstNode *parse_or(Parser *p);

/* ── Statements ── */

static void expect_end(Parser *p) {
    if (at(p, TOK_NEWLINE)) {
        advance_tok(p);
    } else if (at(p, TOK_EOF) || at(p, TOK_DONE)) {
        /* ok */
    } else {
        Token *t = peek(p);
        nova_syntax_error(t->line, "Expected end of statement, got %s",
                          token_type_name(t->type));
    }
}

static AstNode *parse_do_block(Parser *p) {
    skip_newlines(p);
    Token *do_tok = expect(p, TOK_DO, "Expected 'do'");
    skip_newlines(p);

    /* Collect statements */
    int cap = 8, count = 0;
    AstNode **stmts = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);

    while (!at(p, TOK_DONE) && !at(p, TOK_EOF) && !at(p, TOK_ELSE) && !at(p, TOK_CATCH)) {
        if (count >= cap) {
            int old = cap;
            cap *= 2;
            AstNode **new_stmts = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
            memcpy(new_stmts, stmts, sizeof(AstNode*) * old);
            stmts = new_stmts;
        }
        stmts[count++] = parse_statement(p);
        skip_newlines(p);
    }

    AstNode *block = nova_ast_new(&p->arena, AST_BLOCK, do_tok->line);
    block->as.block.stmts = stmts;
    block->as.block.count = count;
    return block;
}

static char **parse_param_list(Parser *p, int *count) {
    int cap = 4;
    *count = 0;
    char **params = nova_arena_alloc(&p->arena, sizeof(char*) * cap);

    if (!at(p, TOK_RPAREN)) {
        Token *name = expect(p, TOK_IDENTIFIER, "Expected parameter name");
        params[(*count)++] = nova_arena_strdup(&p->arena, name->value.string);

        while (at(p, TOK_COMMA)) {
            advance_tok(p);
            if (*count >= cap) {
                int old = cap; cap *= 2;
                char **new_p = nova_arena_alloc(&p->arena, sizeof(char*) * cap);
                memcpy(new_p, params, sizeof(char*) * old);
                params = new_p;
            }
            name = expect(p, TOK_IDENTIFIER, "Expected parameter name");
            params[(*count)++] = nova_arena_strdup(&p->arena, name->value.string);
        }
    }
    return params;
}

static AstNode *parse_let(Parser *p) {
    Token *tok = advance_tok(p); /* consume 'let' */
    Token *name = expect(p, TOK_IDENTIFIER, "Expected variable name after 'let'");
    expect(p, TOK_ASSIGN, "Expected '=' after variable name");
    AstNode *value = parse_expression(p);
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_LET_DECLARATION, tok->line);
    node->as.let.name = nova_arena_strdup(&p->arena, name->value.string);
    node->as.let.value = value;
    return node;
}

static AstNode *parse_function_def(Parser *p) {
    Token *tok = advance_tok(p); /* consume 'func' */
    Token *name = expect(p, TOK_IDENTIFIER, "Expected function name");
    expect(p, TOK_LPAREN, "Expected '(' after function name");

    int param_count;
    char **params = parse_param_list(p, &param_count);

    expect(p, TOK_RPAREN, "Expected ')' after parameters");
    AstNode *body = parse_do_block(p);
    expect(p, TOK_DONE, "Expected 'done' to close function block");
    skip_newlines(p);

    AstNode *node = nova_ast_new(&p->arena, AST_FUNCTION_DEF, tok->line);
    node->as.func_def.name = nova_arena_strdup(&p->arena, name->value.string);
    node->as.func_def.params = params;
    node->as.func_def.param_count = param_count;
    node->as.func_def.body = body;
    return node;
}

static AstNode *parse_if(Parser *p) {
    Token *tok = advance_tok(p); /* consume 'if' */
    AstNode *condition = parse_expression(p);
    AstNode *body = parse_do_block(p);

    int ei_cap = 4, ei_count = 0;
    ElseIfClause *else_ifs = nova_arena_alloc(&p->arena, sizeof(ElseIfClause) * ei_cap);
    AstNode *else_body = NULL;

    while (at(p, TOK_ELSE)) {
        advance_tok(p); /* consume 'else' */
        if (at(p, TOK_IF)) {
            Token *ei_tok = advance_tok(p); /* consume 'if' */
            AstNode *ei_cond = parse_expression(p);
            AstNode *ei_body = parse_do_block(p);

            if (ei_count >= ei_cap) {
                int old = ei_cap; ei_cap *= 2;
                ElseIfClause *new_ei = nova_arena_alloc(&p->arena, sizeof(ElseIfClause) * ei_cap);
                memcpy(new_ei, else_ifs, sizeof(ElseIfClause) * old);
                else_ifs = new_ei;
            }
            else_ifs[ei_count].condition = ei_cond;
            else_ifs[ei_count].body = ei_body;
            else_ifs[ei_count].line = ei_tok->line;
            ei_count++;
        } else {
            else_body = parse_do_block(p);
            break;
        }
    }

    expect(p, TOK_DONE, "Expected 'done' to close if block");
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_IF_STATEMENT, tok->line);
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.body = body;
    node->as.if_stmt.else_ifs = else_ifs;
    node->as.if_stmt.else_if_count = ei_count;
    node->as.if_stmt.else_body = else_body;
    return node;
}

static AstNode *parse_while(Parser *p) {
    Token *tok = advance_tok(p);
    AstNode *condition = parse_expression(p);
    AstNode *body = parse_do_block(p);
    expect(p, TOK_DONE, "Expected 'done' to close while block");
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_WHILE_STATEMENT, tok->line);
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
    return node;
}

static AstNode *parse_for(Parser *p) {
    Token *tok = advance_tok(p);
    Token *var = expect(p, TOK_IDENTIFIER, "Expected variable name after 'for'");
    expect(p, TOK_IN, "Expected 'in' after for variable");
    AstNode *iterable = parse_expression(p);
    AstNode *body = parse_do_block(p);
    expect(p, TOK_DONE, "Expected 'done' to close for block");
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_FOR_STATEMENT, tok->line);
    node->as.for_stmt.var_name = nova_arena_strdup(&p->arena, var->value.string);
    node->as.for_stmt.iterable = iterable;
    node->as.for_stmt.body = body;
    return node;
}

static AstNode *parse_class(Parser *p) {
    Token *tok = advance_tok(p);
    Token *name = expect(p, TOK_IDENTIFIER, "Expected class name");

    char *parent = NULL;
    if (at(p, TOK_LPAREN)) {
        advance_tok(p);
        Token *parent_tok = expect(p, TOK_IDENTIFIER, "Expected parent class name");
        parent = nova_arena_strdup(&p->arena, parent_tok->value.string);
        expect(p, TOK_RPAREN, "Expected ')' after parent class name");
    }

    skip_newlines(p);
    expect(p, TOK_DO, "Expected 'do' after class declaration");
    skip_newlines(p);

    int cap = 8, count = 0;
    AstNode **methods = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);

    while (!at(p, TOK_DONE) && !at(p, TOK_EOF)) {
        skip_newlines(p);
        if (at(p, TOK_DONE)) break;

        if (!at(p, TOK_FUNC)) {
            nova_syntax_error(peek(p)->line, "Expected method definition inside class");
        }
        if (count >= cap) {
            int old = cap; cap *= 2;
            AstNode **new_m = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
            memcpy(new_m, methods, sizeof(AstNode*) * old);
            methods = new_m;
        }
        methods[count++] = parse_function_def(p);
        skip_newlines(p);
    }

    expect(p, TOK_DONE, "Expected 'done' to close class block");
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_CLASS_DEF, tok->line);
    node->as.class_def.name = nova_arena_strdup(&p->arena, name->value.string);
    node->as.class_def.parent = parent;
    node->as.class_def.methods = methods;
    node->as.class_def.method_count = count;
    return node;
}

static AstNode *parse_return(Parser *p) {
    Token *tok = advance_tok(p);
    AstNode *value = NULL;
    if (!at(p, TOK_NEWLINE) && !at(p, TOK_EOF) && !at(p, TOK_DONE)) {
        value = parse_expression(p);
    }
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_RETURN_STATEMENT, tok->line);
    node->as.return_stmt.value = value;
    return node;
}

static AstNode *parse_import(Parser *p) {
    Token *tok = advance_tok(p);
    Token *name = expect(p, TOK_STRING, "Expected module name string after 'import'");
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_IMPORT_STATEMENT, tok->line);
    node->as.import_stmt.module_name = nova_arena_strdup(&p->arena, name->value.string);
    return node;
}

static AstNode *parse_try_catch(Parser *p) {
    Token *tok = advance_tok(p); /* consume 'try' */
    AstNode *try_body = parse_do_block(p);

    expect(p, TOK_CATCH, "Expected 'catch' after try block");
    Token *err_name = expect(p, TOK_IDENTIFIER, "Expected error variable name after 'catch'");
    AstNode *catch_body = parse_do_block(p);
    expect(p, TOK_DONE, "Expected 'done' to close try/catch block");
    expect_end(p);

    AstNode *node = nova_ast_new(&p->arena, AST_TRY_CATCH, tok->line);
    node->as.try_catch.try_body = try_body;
    node->as.try_catch.error_name = nova_arena_strdup(&p->arena, err_name->value.string);
    node->as.try_catch.catch_body = catch_body;
    return node;
}

static AstNode *parse_expression_statement(Parser *p) {
    AstNode *expr = parse_expression(p);

    /* Check for assignment: identifier = value */
    if (expr->type == AST_IDENTIFIER && at(p, TOK_ASSIGN)) {
        advance_tok(p);
        AstNode *value = parse_expression(p);
        expect_end(p);
        AstNode *node = nova_ast_new(&p->arena, AST_ASSIGNMENT, expr->line);
        node->as.assignment.name = expr->as.identifier.name;
        node->as.assignment.value = value;
        return node;
    }

    /* Member assignment: obj.member = value */
    if (expr->type == AST_MEMBER_ACCESS && at(p, TOK_ASSIGN)) {
        advance_tok(p);
        AstNode *value = parse_expression(p);
        expect_end(p);
        AstNode *node = nova_ast_new(&p->arena, AST_MEMBER_ASSIGN, expr->line);
        node->as.member_assign.obj = expr->as.member_access.obj;
        node->as.member_assign.member = expr->as.member_access.member;
        node->as.member_assign.value = value;
        return node;
    }

    /* Index assignment: obj[index] = value */
    if (expr->type == AST_INDEX_ACCESS && at(p, TOK_ASSIGN)) {
        advance_tok(p);
        AstNode *value = parse_expression(p);
        expect_end(p);
        AstNode *node = nova_ast_new(&p->arena, AST_INDEX_ASSIGN, expr->line);
        node->as.index_assign.obj = expr->as.index_access.obj;
        node->as.index_assign.index = expr->as.index_access.index;
        node->as.index_assign.value = value;
        return node;
    }

    expect_end(p);
    return expr;
}

static AstNode *parse_statement(Parser *p) {
    Token *tok = peek(p);

    switch (tok->type) {
        case TOK_LET:      return parse_let(p);
        case TOK_FUNC:     return parse_function_def(p);
        case TOK_IF:       return parse_if(p);
        case TOK_WHILE:    return parse_while(p);
        case TOK_FOR:      return parse_for(p);
        case TOK_CLASS:    return parse_class(p);
        case TOK_RETURN:   return parse_return(p);
        case TOK_IMPORT:   return parse_import(p);
        case TOK_TRY:      return parse_try_catch(p);
        case TOK_BREAK: {
            advance_tok(p);
            expect_end(p);
            return nova_ast_new(&p->arena, AST_BREAK_STATEMENT, tok->line);
        }
        case TOK_CONTINUE: {
            advance_tok(p);
            expect_end(p);
            return nova_ast_new(&p->arena, AST_CONTINUE_STATEMENT, tok->line);
        }
        default:
            return parse_expression_statement(p);
    }
}

/* ── Expressions ── */

static AstNode *parse_and(Parser *p);
static AstNode *parse_not(Parser *p);
static AstNode *parse_comparison(Parser *p);
static AstNode *parse_addition(Parser *p);
static AstNode *parse_multiplication(Parser *p);
static AstNode *parse_unary(Parser *p);
static AstNode *parse_postfix(Parser *p);
static AstNode *parse_primary(Parser *p);

static AstNode *parse_pipe(Parser *p);

static AstNode *parse_expression(Parser *p) {
    AstNode *expr = parse_pipe(p);

    /* Ternary: expr if condition else alt */
    if (at(p, TOK_IF)) {
        advance_tok(p); /* consume 'if' */
        AstNode *condition = parse_pipe(p);
        expect(p, TOK_ELSE, "Expected 'else' in ternary expression");
        AstNode *alt = parse_expression(p);

        AstNode *node = nova_ast_new(&p->arena, AST_TERNARY, expr->line);
        node->as.ternary.true_expr = expr;
        node->as.ternary.condition = condition;
        node->as.ternary.false_expr = alt;
        return node;
    }

    return expr;
}

/* Pipe operator: expr |> func  becomes  func(expr)
 *                expr |> func(a)  becomes  func(expr, a) */
static AstNode *parse_pipe(Parser *p) {
    AstNode *left = parse_or(p);

    while (at(p, TOK_PIPE)) {
        Token *pipe_tok = advance_tok(p);
        /* Parse the right side as a postfix expression (allows func calls) */
        AstNode *right = parse_postfix(p);

        if (right->type == AST_FUNCTION_CALL) {
            /* expr |> func(a, b)  =>  func(expr, a, b)
             * Prepend left as first argument */
            int old_count = right->as.call.arg_count;
            int new_count = old_count + 1;
            AstNode **new_args = nova_arena_alloc(&p->arena, sizeof(AstNode*) * new_count);
            new_args[0] = left;
            for (int i = 0; i < old_count; i++)
                new_args[i + 1] = right->as.call.args[i];
            right->as.call.args = new_args;
            right->as.call.arg_count = new_count;
            left = right;
        } else {
            /* expr |> func  =>  func(expr) */
            AstNode *call = nova_ast_new(&p->arena, AST_FUNCTION_CALL, pipe_tok->line);
            call->as.call.callee = right;
            AstNode **args = nova_arena_alloc(&p->arena, sizeof(AstNode*));
            args[0] = left;
            call->as.call.args = args;
            call->as.call.arg_count = 1;
            left = call;
        }
    }
    return left;
}

static AstNode *parse_or(Parser *p) {
    AstNode *left = parse_and(p);
    while (at(p, TOK_OR)) {
        Token *op = advance_tok(p);
        AstNode *right = parse_and(p);
        AstNode *node = nova_ast_new(&p->arena, AST_BINARY_OP, op->line);
        node->as.binary.left = left;
        node->as.binary.op = OP_OR;
        node->as.binary.right = right;
        left = node;
    }
    return left;
}

static AstNode *parse_and(Parser *p) {
    AstNode *left = parse_not(p);
    while (at(p, TOK_AND)) {
        Token *op = advance_tok(p);
        AstNode *right = parse_not(p);
        AstNode *node = nova_ast_new(&p->arena, AST_BINARY_OP, op->line);
        node->as.binary.left = left;
        node->as.binary.op = OP_AND;
        node->as.binary.right = right;
        left = node;
    }
    return left;
}

static AstNode *parse_not(Parser *p) {
    if (at(p, TOK_NOT)) {
        Token *op = advance_tok(p);
        AstNode *operand = parse_not(p);
        AstNode *node = nova_ast_new(&p->arena, AST_UNARY_OP, op->line);
        node->as.unary.op = OP_NOT;
        node->as.unary.operand = operand;
        return node;
    }
    return parse_comparison(p);
}

static AstNode *parse_comparison(Parser *p) {
    AstNode *left = parse_addition(p);
    while (at(p, TOK_EQ) || at(p, TOK_NEQ) || at(p, TOK_LT) ||
           at(p, TOK_GT) || at(p, TOK_LTE) || at(p, TOK_GTE)) {
        Token *op = advance_tok(p);
        OpType optype;
        switch (op->type) {
            case TOK_EQ:  optype = OP_EQ;  break;
            case TOK_NEQ: optype = OP_NEQ; break;
            case TOK_LT:  optype = OP_LT;  break;
            case TOK_GT:  optype = OP_GT;  break;
            case TOK_LTE: optype = OP_LTE; break;
            case TOK_GTE: optype = OP_GTE; break;
            default: optype = OP_EQ; break;
        }
        AstNode *right = parse_addition(p);
        AstNode *node = nova_ast_new(&p->arena, AST_BINARY_OP, op->line);
        node->as.binary.left = left;
        node->as.binary.op = optype;
        node->as.binary.right = right;
        left = node;
    }
    return left;
}

static AstNode *parse_addition(Parser *p) {
    AstNode *left = parse_multiplication(p);
    while (at(p, TOK_PLUS) || at(p, TOK_MINUS)) {
        Token *op = advance_tok(p);
        OpType optype = (op->type == TOK_PLUS) ? OP_ADD : OP_SUB;
        AstNode *right = parse_multiplication(p);
        AstNode *node = nova_ast_new(&p->arena, AST_BINARY_OP, op->line);
        node->as.binary.left = left;
        node->as.binary.op = optype;
        node->as.binary.right = right;
        left = node;
    }
    return left;
}

static AstNode *parse_multiplication(Parser *p) {
    AstNode *left = parse_unary(p);
    while (at(p, TOK_STAR) || at(p, TOK_SLASH) || at(p, TOK_PERCENT)) {
        Token *op = advance_tok(p);
        OpType optype;
        switch (op->type) {
            case TOK_STAR:    optype = OP_MUL; break;
            case TOK_SLASH:   optype = OP_DIV; break;
            case TOK_PERCENT: optype = OP_MOD; break;
            default: optype = OP_MUL; break;
        }
        AstNode *right = parse_unary(p);
        AstNode *node = nova_ast_new(&p->arena, AST_BINARY_OP, op->line);
        node->as.binary.left = left;
        node->as.binary.op = optype;
        node->as.binary.right = right;
        left = node;
    }
    return left;
}

static AstNode *parse_unary(Parser *p) {
    if (at(p, TOK_MINUS)) {
        Token *op = advance_tok(p);
        AstNode *operand = parse_unary(p);
        AstNode *node = nova_ast_new(&p->arena, AST_UNARY_OP, op->line);
        node->as.unary.op = OP_NEG;
        node->as.unary.operand = operand;
        return node;
    }
    return parse_postfix(p);
}

static AstNode *parse_call_args(Parser *p, AstNode *callee) {
    advance_tok(p); /* consume '(' */

    int cap = 4, count = 0;
    AstNode **args = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);

    if (!at(p, TOK_RPAREN)) {
        if (count >= cap) {
            int old = cap; cap *= 2;
            AstNode **na = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
            memcpy(na, args, sizeof(AstNode*) * old);
            args = na;
        }
        args[count++] = parse_expression(p);

        while (at(p, TOK_COMMA)) {
            advance_tok(p);
            if (count >= cap) {
                int old = cap; cap *= 2;
                AstNode **na = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
                memcpy(na, args, sizeof(AstNode*) * old);
                args = na;
            }
            args[count++] = parse_expression(p);
        }
    }
    Token *rparen = expect(p, TOK_RPAREN, "Expected ')' after arguments");

    AstNode *node = nova_ast_new(&p->arena, AST_FUNCTION_CALL, rparen->line);
    node->as.call.callee = callee;
    node->as.call.args = args;
    node->as.call.arg_count = count;
    return node;
}

static AstNode *parse_postfix(Parser *p) {
    AstNode *expr = parse_primary(p);

    while (true) {
        if (at(p, TOK_LPAREN)) {
            expr = parse_call_args(p, expr);
        } else if (at(p, TOK_DOT)) {
            advance_tok(p);
            Token *member = expect(p, TOK_IDENTIFIER, "Expected member name after '.'");
            AstNode *node = nova_ast_new(&p->arena, AST_MEMBER_ACCESS, member->line);
            node->as.member_access.obj = expr;
            node->as.member_access.member = nova_arena_strdup(&p->arena, member->value.string);
            expr = node;
        } else if (at(p, TOK_LBRACKET)) {
            Token *bracket = advance_tok(p);
            AstNode *index = parse_expression(p);
            expect(p, TOK_RBRACKET, "Expected ']'");
            AstNode *node = nova_ast_new(&p->arena, AST_INDEX_ACCESS, bracket->line);
            node->as.index_access.obj = expr;
            node->as.index_access.index = index;
            expr = node;
        } else {
            break;
        }
    }
    return expr;
}

static AstNode *parse_list_literal(Parser *p) {
    Token *tok = advance_tok(p); /* consume '[' */
    int cap = 4, count = 0;
    AstNode **elems = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
    skip_newlines(p);

    if (!at(p, TOK_RBRACKET)) {
        if (count >= cap) {
            int old = cap; cap *= 2;
            AstNode **ne = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
            memcpy(ne, elems, sizeof(AstNode*) * old);
            elems = ne;
        }
        elems[count++] = parse_expression(p);
        while (at(p, TOK_COMMA)) {
            advance_tok(p);
            skip_newlines(p);
            if (at(p, TOK_RBRACKET)) break;
            if (count >= cap) {
                int old = cap; cap *= 2;
                AstNode **ne = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);
                memcpy(ne, elems, sizeof(AstNode*) * old);
                elems = ne;
            }
            elems[count++] = parse_expression(p);
        }
    }
    skip_newlines(p);
    expect(p, TOK_RBRACKET, "Expected ']'");

    AstNode *node = nova_ast_new(&p->arena, AST_LIST_LITERAL, tok->line);
    node->as.list.elements = elems;
    node->as.list.count = count;
    return node;
}

static AstNode *parse_dict_literal(Parser *p) {
    Token *tok = advance_tok(p); /* consume '{' */
    int cap = 4, count = 0;
    DictEntry *entries = nova_arena_alloc(&p->arena, sizeof(DictEntry) * cap);
    skip_newlines(p);

    if (!at(p, TOK_RBRACE)) {
        AstNode *key = parse_expression(p);
        expect(p, TOK_COLON, "Expected ':' after dict key");
        AstNode *value = parse_expression(p);
        entries[count].key = key;
        entries[count].value = value;
        count++;

        while (at(p, TOK_COMMA)) {
            advance_tok(p);
            skip_newlines(p);
            if (at(p, TOK_RBRACE)) break;
            if (count >= cap) {
                int old = cap; cap *= 2;
                DictEntry *ne = nova_arena_alloc(&p->arena, sizeof(DictEntry) * cap);
                memcpy(ne, entries, sizeof(DictEntry) * old);
                entries = ne;
            }
            key = parse_expression(p);
            expect(p, TOK_COLON, "Expected ':' after dict key");
            value = parse_expression(p);
            entries[count].key = key;
            entries[count].value = value;
            count++;
        }
    }
    skip_newlines(p);
    expect(p, TOK_RBRACE, "Expected '}'");

    AstNode *node = nova_ast_new(&p->arena, AST_DICT_LITERAL, tok->line);
    node->as.dict.entries = entries;
    node->as.dict.count = count;
    return node;
}

static AstNode *parse_string_interp(Parser *p) {
    /* INTERP_START text, then expression tokens, then INTERP_MID/INTERP_END, etc. */
    Token *start = advance_tok(p); /* consume INTERP_START */
    int line = start->line;

    int cap = 8, count = 0;
    AstNode **parts = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap);

    /* First string segment */
    AstNode *seg = nova_ast_new(&p->arena, AST_STRING_LITERAL, line);
    seg->as.string.value = nova_arena_strdup(&p->arena, start->value.string ? start->value.string : "");
    parts[count++] = seg;

    /* First expression (tokens between INTERP_START and INTERP_MID/INTERP_END) */
    AstNode *expr = parse_expression(p);
    if (count >= cap) { int old = cap; cap *= 2; AstNode **np = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap); memcpy(np, parts, sizeof(AstNode*) * old); parts = np; }
    parts[count++] = expr;

    /* Continue with mid/end segments */
    while (at(p, TOK_INTERP_MID)) {
        Token *mid = advance_tok(p);
        seg = nova_ast_new(&p->arena, AST_STRING_LITERAL, mid->line);
        seg->as.string.value = nova_arena_strdup(&p->arena, mid->value.string ? mid->value.string : "");
        if (count >= cap) { int old = cap; cap *= 2; AstNode **np = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap); memcpy(np, parts, sizeof(AstNode*) * old); parts = np; }
        parts[count++] = seg;

        expr = parse_expression(p);
        if (count >= cap) { int old = cap; cap *= 2; AstNode **np = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap); memcpy(np, parts, sizeof(AstNode*) * old); parts = np; }
        parts[count++] = expr;
    }

    /* Final end segment */
    Token *end = expect(p, TOK_INTERP_END, "Expected end of interpolated string");
    seg = nova_ast_new(&p->arena, AST_STRING_LITERAL, end->line);
    seg->as.string.value = nova_arena_strdup(&p->arena, end->value.string ? end->value.string : "");
    if (count >= cap) { int old = cap; cap *= 2; AstNode **np = nova_arena_alloc(&p->arena, sizeof(AstNode*) * cap); memcpy(np, parts, sizeof(AstNode*) * old); parts = np; }
    parts[count++] = seg;

    AstNode *node = nova_ast_new(&p->arena, AST_STRING_INTERP, line);
    node->as.interp.parts = parts;
    node->as.interp.count = count;
    return node;
}

/* Try to parse a lambda: (params) => expr
 * Uses backtracking: save position, try params + ) + =>, if fail restore. */
static AstNode *try_parse_lambda(Parser *p) {
    int saved_pos = p->pos;

    advance_tok(p); /* consume '(' */

    /* Try to parse parameter list (identifiers separated by commas) */
    int cap = 4, count = 0;
    char **params = nova_arena_alloc(&p->arena, sizeof(char*) * cap);

    if (!at(p, TOK_RPAREN)) {
        if (!at(p, TOK_IDENTIFIER)) { p->pos = saved_pos; return NULL; }
        Token *name = advance_tok(p);
        params[count++] = nova_arena_strdup(&p->arena, name->value.string);

        while (at(p, TOK_COMMA)) {
            advance_tok(p);
            if (!at(p, TOK_IDENTIFIER)) { p->pos = saved_pos; return NULL; }
            if (count >= cap) {
                int old = cap; cap *= 2;
                char **np = nova_arena_alloc(&p->arena, sizeof(char*) * cap);
                memcpy(np, params, sizeof(char*) * old);
                params = np;
            }
            name = advance_tok(p);
            params[count++] = nova_arena_strdup(&p->arena, name->value.string);
        }
    }

    if (!at(p, TOK_RPAREN)) { p->pos = saved_pos; return NULL; }
    advance_tok(p); /* consume ')' */

    if (!at(p, TOK_ARROW)) { p->pos = saved_pos; return NULL; }
    Token *arrow = advance_tok(p); /* consume '=>' */

    /* It's a lambda! Parse the body expression */
    AstNode *body = parse_expression(p);

    AstNode *node = nova_ast_new(&p->arena, AST_LAMBDA, arrow->line);
    node->as.lambda.params = params;
    node->as.lambda.param_count = count;
    node->as.lambda.body = body;
    return node;
}

static AstNode *parse_primary(Parser *p) {
    Token *tok = peek(p);

    switch (tok->type) {
        case TOK_NUMBER: {
            advance_tok(p);
            AstNode *node = nova_ast_new(&p->arena, AST_NUMBER_LITERAL, tok->line);
            node->as.number.value = tok->value.number;
            node->as.number.is_int = tok->is_int;
            return node;
        }
        case TOK_STRING: {
            advance_tok(p);
            AstNode *node = nova_ast_new(&p->arena, AST_STRING_LITERAL, tok->line);
            node->as.string.value = nova_arena_strdup(&p->arena, tok->value.string);
            return node;
        }
        case TOK_INTERP_START:
            return parse_string_interp(p);

        case TOK_TRUE: {
            advance_tok(p);
            AstNode *node = nova_ast_new(&p->arena, AST_BOOL_LITERAL, tok->line);
            node->as.boolean.value = true;
            return node;
        }
        case TOK_FALSE: {
            advance_tok(p);
            AstNode *node = nova_ast_new(&p->arena, AST_BOOL_LITERAL, tok->line);
            node->as.boolean.value = false;
            return node;
        }
        case TOK_NONE: {
            advance_tok(p);
            return nova_ast_new(&p->arena, AST_NONE_LITERAL, tok->line);
        }
        case TOK_IDENTIFIER: {
            advance_tok(p);
            AstNode *node = nova_ast_new(&p->arena, AST_IDENTIFIER, tok->line);
            node->as.identifier.name = nova_arena_strdup(&p->arena, tok->value.string);
            return node;
        }
        case TOK_LPAREN: {
            /* Try lambda first: (params) => expr */
            AstNode *lambda = try_parse_lambda(p);
            if (lambda) return lambda;

            /* Grouped expression */
            advance_tok(p);
            AstNode *expr = parse_expression(p);
            expect(p, TOK_RPAREN, "Expected ')'");
            return expr;
        }
        case TOK_LBRACKET:
            return parse_list_literal(p);

        case TOK_LBRACE:
            return parse_dict_literal(p);

        default:
            nova_syntax_error(tok->line, "Unexpected token: %s",
                              token_type_name(tok->type));
            return NULL; /* unreachable */
    }
}

/* ── Public API ── */

void nova_parser_init(Parser *parser, Token *tokens, int count) {
    parser->tokens = tokens;
    parser->count = count;
    parser->pos = 0;
    nova_arena_init(&parser->arena);
}

void nova_parser_free(Parser *parser) {
    nova_arena_free(&parser->arena);
}

AstNode *nova_parse(Parser *parser) {
    skip_newlines(parser);

    int cap = 16, count = 0;
    AstNode **stmts = nova_arena_alloc(&parser->arena, sizeof(AstNode*) * cap);

    while (!at(parser, TOK_EOF)) {
        if (count >= cap) {
            int old = cap; cap *= 2;
            AstNode **ns = nova_arena_alloc(&parser->arena, sizeof(AstNode*) * cap);
            memcpy(ns, stmts, sizeof(AstNode*) * old);
            stmts = ns;
        }
        stmts[count++] = parse_statement(parser);
        skip_newlines(parser);
    }

    AstNode *prog = nova_ast_new(&parser->arena, AST_PROGRAM, 1);
    prog->as.program.stmts = stmts;
    prog->as.program.count = count;
    return prog;
}
