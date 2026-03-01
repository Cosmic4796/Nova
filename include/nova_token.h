#ifndef NOVA_TOKEN_H
#define NOVA_TOKEN_H

#include "nova_common.h"

typedef enum {
    /* Literals */
    TOK_NUMBER,
    TOK_STRING,
    TOK_IDENTIFIER,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NONE,

    /* Operators */
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,

    /* Comparison */
    TOK_EQ,         /* == */
    TOK_NEQ,        /* != */
    TOK_LT,         /* <  */
    TOK_GT,         /* >  */
    TOK_LTE,        /* <= */
    TOK_GTE,        /* >= */

    /* Assignment */
    TOK_ASSIGN,     /* = */

    /* Logical */
    TOK_AND,
    TOK_OR,
    TOK_NOT,

    /* Delimiters */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LBRACE,     /* { */
    TOK_RBRACE,     /* } */
    TOK_COMMA,
    TOK_DOT,
    TOK_COLON,
    TOK_ARROW,      /* => */
    TOK_PIPE,       /* |> */

    /* Keywords */
    TOK_LET,
    TOK_FUNC,
    TOK_RETURN,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_IN,
    TOK_DO,
    TOK_DONE,
    TOK_CLASS,
    TOK_IMPORT,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_TRY,
    TOK_CATCH,

    /* Special */
    TOK_NEWLINE,
    TOK_EOF,

    /* String interpolation */
    TOK_INTERP_START,   /* start of interpolated string segment */
    TOK_INTERP_MID,     /* middle segment between expressions */
    TOK_INTERP_END,     /* end of interpolated string */

    TOK_COUNT
} TokenType;

struct Token {
    TokenType type;
    int line;
    /* Token value - depends on type */
    union {
        double   number;    /* TOK_NUMBER (int or float stored as double) */
        char    *string;    /* TOK_STRING, TOK_IDENTIFIER, interp segments */
    } value;
    bool is_int;            /* for TOK_NUMBER: was it an integer? */
};

const char *token_type_name(TokenType type);
TokenType token_lookup_keyword(const char *name, int length);

#endif
