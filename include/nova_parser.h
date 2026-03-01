#ifndef NOVA_PARSER_H
#define NOVA_PARSER_H

#include "nova_common.h"
#include "nova_token.h"
#include "nova_ast.h"

struct Parser {
    Token  *tokens;
    int     count;
    int     pos;
    AstArena arena;
};

void     nova_parser_init(Parser *parser, Token *tokens, int count);
void     nova_parser_free(Parser *parser);
AstNode *nova_parse(Parser *parser);

#endif
