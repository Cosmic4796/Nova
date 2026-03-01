#include "nova_token.h"
#include <string.h>

typedef struct {
    const char *name;
    TokenType   type;
} Keyword;

static const Keyword keywords[] = {
    {"let",      TOK_LET},
    {"func",     TOK_FUNC},
    {"return",   TOK_RETURN},
    {"if",       TOK_IF},
    {"else",     TOK_ELSE},
    {"while",    TOK_WHILE},
    {"for",      TOK_FOR},
    {"in",       TOK_IN},
    {"do",       TOK_DO},
    {"done",     TOK_DONE},
    {"class",    TOK_CLASS},
    {"import",   TOK_IMPORT},
    {"true",     TOK_TRUE},
    {"false",    TOK_FALSE},
    {"none",     TOK_NONE},
    {"and",      TOK_AND},
    {"or",       TOK_OR},
    {"not",      TOK_NOT},
    {"break",    TOK_BREAK},
    {"continue", TOK_CONTINUE},
    {"try",      TOK_TRY},
    {"catch",    TOK_CATCH},
    {NULL, 0}
};

TokenType token_lookup_keyword(const char *name, int length) {
    for (int i = 0; keywords[i].name != NULL; i++) {
        if ((int)strlen(keywords[i].name) == length &&
            memcmp(keywords[i].name, name, length) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENTIFIER;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOK_NUMBER:     return "NUMBER";
        case TOK_STRING:     return "STRING";
        case TOK_IDENTIFIER: return "IDENTIFIER";
        case TOK_TRUE:       return "TRUE";
        case TOK_FALSE:      return "FALSE";
        case TOK_NONE:       return "NONE";
        case TOK_PLUS:       return "PLUS";
        case TOK_MINUS:      return "MINUS";
        case TOK_STAR:       return "STAR";
        case TOK_SLASH:      return "SLASH";
        case TOK_PERCENT:    return "PERCENT";
        case TOK_EQ:         return "EQ";
        case TOK_NEQ:        return "NEQ";
        case TOK_LT:        return "LT";
        case TOK_GT:        return "GT";
        case TOK_LTE:       return "LTE";
        case TOK_GTE:       return "GTE";
        case TOK_ASSIGN:    return "ASSIGN";
        case TOK_AND:       return "AND";
        case TOK_OR:        return "OR";
        case TOK_NOT:       return "NOT";
        case TOK_LPAREN:    return "LPAREN";
        case TOK_RPAREN:    return "RPAREN";
        case TOK_LBRACKET:  return "LBRACKET";
        case TOK_RBRACKET:  return "RBRACKET";
        case TOK_LBRACE:    return "LBRACE";
        case TOK_RBRACE:    return "RBRACE";
        case TOK_COMMA:     return "COMMA";
        case TOK_DOT:       return "DOT";
        case TOK_COLON:     return "COLON";
        case TOK_ARROW:     return "ARROW";
        case TOK_PIPE:      return "PIPE";
        case TOK_LET:       return "LET";
        case TOK_FUNC:      return "FUNC";
        case TOK_RETURN:    return "RETURN";
        case TOK_IF:        return "IF";
        case TOK_ELSE:      return "ELSE";
        case TOK_WHILE:     return "WHILE";
        case TOK_FOR:       return "FOR";
        case TOK_IN:        return "IN";
        case TOK_DO:        return "DO";
        case TOK_DONE:      return "DONE";
        case TOK_CLASS:     return "CLASS";
        case TOK_IMPORT:    return "IMPORT";
        case TOK_BREAK:     return "BREAK";
        case TOK_CONTINUE:  return "CONTINUE";
        case TOK_TRY:       return "TRY";
        case TOK_CATCH:     return "CATCH";
        case TOK_NEWLINE:   return "NEWLINE";
        case TOK_EOF:       return "EOF";
        case TOK_INTERP_START: return "INTERP_START";
        case TOK_INTERP_MID:   return "INTERP_MID";
        case TOK_INTERP_END:   return "INTERP_END";
        case TOK_COUNT:     return "COUNT";
    }
    return "UNKNOWN";
}
