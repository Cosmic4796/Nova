#ifndef NOVA_LEXER_H
#define NOVA_LEXER_H

#include "nova_common.h"
#include "nova_token.h"

struct Lexer {
    const char *source;
    int         length;
    int         pos;
    int         line;
    const char *filename;
    int         paren_depth;

    /* Token array */
    Token      *tokens;
    int         token_count;
    int         token_capacity;
};

void  nova_lexer_init(Lexer *lexer, const char *source, const char *filename);
void  nova_lexer_free(Lexer *lexer);
Token *nova_lexer_tokenize(Lexer *lexer, int *count);

#endif
