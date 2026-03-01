#include "nova.h"

static void add_token(Lexer *lex, TokenType type, int line) {
    if (lex->token_count >= lex->token_capacity) {
        int old = lex->token_capacity;
        lex->token_capacity = old < 8 ? 8 : old * 2;
        lex->tokens = realloc(lex->tokens, sizeof(Token) * lex->token_capacity);
    }
    Token *t = &lex->tokens[lex->token_count++];
    t->type = type;
    t->line = line;
    t->value.string = NULL;
    t->is_int = false;
}

static void add_string_token(Lexer *lex, TokenType type, char *str, int line) {
    if (lex->token_count >= lex->token_capacity) {
        int old = lex->token_capacity;
        lex->token_capacity = old < 8 ? 8 : old * 2;
        lex->tokens = realloc(lex->tokens, sizeof(Token) * lex->token_capacity);
    }
    Token *t = &lex->tokens[lex->token_count++];
    t->type = type;
    t->line = line;
    t->value.string = str;
    t->is_int = false;
}

static void add_number_token(Lexer *lex, double num, bool is_int, int line) {
    if (lex->token_count >= lex->token_capacity) {
        int old = lex->token_capacity;
        lex->token_capacity = old < 8 ? 8 : old * 2;
        lex->tokens = realloc(lex->tokens, sizeof(Token) * lex->token_capacity);
    }
    Token *t = &lex->tokens[lex->token_count++];
    t->type = TOK_NUMBER;
    t->line = line;
    t->value.number = num;
    t->is_int = is_int;
}

static char peek(Lexer *lex) {
    if (lex->pos >= lex->length) return '\0';
    return lex->source[lex->pos];
}

static char peek_next(Lexer *lex) {
    if (lex->pos + 1 >= lex->length) return '\0';
    return lex->source[lex->pos + 1];
}

static char advance(Lexer *lex) {
    char ch = lex->source[lex->pos++];
    if (ch == '\n') lex->line++;
    return ch;
}

static void skip_whitespace(Lexer *lex) {
    while (lex->pos < lex->length) {
        char ch = lex->source[lex->pos];
        if (ch == ' ' || ch == '\t' || ch == '\r')
            lex->pos++;
        else
            break;
    }
}

static void skip_comment(Lexer *lex) {
    while (lex->pos < lex->length && lex->source[lex->pos] != '\n')
        lex->pos++;
}

static char *read_string(Lexer *lex, char quote, bool *has_interp) {
    int start_line = lex->line;
    *has_interp = false;

    /* Dynamic buffer for the result */
    int cap = 64;
    int len = 0;
    char *buf = malloc(cap);

    while (lex->pos < lex->length) {
        char ch = lex->source[lex->pos];

        if (ch == '\\') {
            lex->pos++;
            if (lex->pos >= lex->length)
                nova_syntax_error(start_line, "Unterminated string");
            char esc = lex->source[lex->pos++];
            char c;
            switch (esc) {
                case 'n':  c = '\n'; break;
                case 't':  c = '\t'; break;
                case 'r':  c = '\r'; break;
                case '\\': c = '\\'; break;
                case '\'': c = '\''; break;
                case '"':  c = '"';  break;
                default:
                    /* Unknown escape, keep backslash */
                    if (len + 2 >= cap) { cap *= 2; buf = realloc(buf, cap); }
                    buf[len++] = '\\';
                    buf[len++] = esc;
                    continue;
            }
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = c;
        } else if (ch == '{') {
            /* String interpolation marker found */
            *has_interp = true;
            /* Return what we have so far */
            buf[len] = '\0';
            return buf;
        } else if (ch == quote) {
            lex->pos++;
            buf[len] = '\0';
            return buf;
        } else {
            if (ch == '\n') lex->line++;
            if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            buf[len++] = ch;
            lex->pos++;
        }
    }
    free(buf);
    nova_syntax_error(start_line, "Unterminated string");
    return NULL; /* unreachable */
}


void nova_lexer_init(Lexer *lex, const char *source, const char *filename) {
    lex->source = source;
    lex->length = strlen(source);
    lex->pos = 0;
    lex->line = 1;
    lex->filename = filename;
    lex->paren_depth = 0;
    lex->tokens = NULL;
    lex->token_count = 0;
    lex->token_capacity = 0;
}

void nova_lexer_free(Lexer *lex) {
    if (lex->tokens) {
        /* Free string values */
        for (int i = 0; i < lex->token_count; i++) {
            Token *t = &lex->tokens[i];
            if ((t->type == TOK_STRING || t->type == TOK_IDENTIFIER ||
                 t->type == TOK_INTERP_START || t->type == TOK_INTERP_MID ||
                 t->type == TOK_INTERP_END) && t->value.string) {
                free(t->value.string);
            }
        }
        free(lex->tokens);
    }
}

Token *nova_lexer_tokenize(Lexer *lex, int *count) {
    while (lex->pos < lex->length) {
        skip_whitespace(lex);
        if (lex->pos >= lex->length) break;

        char ch = peek(lex);

        /* Newlines */
        if (ch == '\n') {
            int nl_line = lex->line;
            advance(lex);
            if (lex->paren_depth == 0 &&
                lex->token_count > 0 &&
                lex->tokens[lex->token_count - 1].type != TOK_NEWLINE) {
                add_token(lex, TOK_NEWLINE, nl_line);
            }
            continue;
        }

        /* Comments */
        if (ch == '#') {
            skip_comment(lex);
            continue;
        }

        /* Strings (with interpolation support) */
        if (ch == '"' || ch == '\'') {
            char quote = ch;
            int line = lex->line;
            lex->pos++; /* skip opening quote */
            bool has_interp = false;
            char *seg = read_string(lex, quote, &has_interp);
            if (!has_interp) {
                add_string_token(lex, TOK_STRING, seg, line);
            } else {
                /* String has interpolation: start the interp token sequence */
                add_string_token(lex, TOK_INTERP_START, seg, line);
                lex->pos++; /* skip '{' */

                /* Now we need to lex the interpolation expression(s) */
                /* Use a recursive-ish approach to handle nested {} */
                int brace_depth = 1;
                while (lex->pos < lex->length && brace_depth > 0) {
                    skip_whitespace(lex);
                    if (lex->pos >= lex->length) break;
                    char c2 = peek(lex);
                    if (c2 == '}') {
                        brace_depth--;
                        if (brace_depth == 0) { lex->pos++; break; }
                    }
                    if (c2 == '{') { brace_depth++; add_token(lex, TOK_LBRACE, lex->line); advance(lex); continue; }
                    if (c2 == '\n') { advance(lex); continue; }
                    if (c2 == '#') { skip_comment(lex); continue; }

                    /* Lex one token */
                    if (c2 == '"' || c2 == '\'') {
                        char q2 = c2;
                        advance(lex);
                        bool d;
                        char *s = read_string(lex, q2, &d);
                        add_string_token(lex, TOK_STRING, s, lex->line);
                        continue;
                    }
                    if (isdigit(c2)) {
                        int start = lex->pos;
                        bool has_dot = false;
                        while (lex->pos < lex->length) {
                            char c = lex->source[lex->pos];
                            if (c == '.' && !has_dot && isdigit(peek_next(lex))) {
                                has_dot = true; lex->pos++;
                            } else if (isdigit(c)) {
                                lex->pos++;
                            } else break;
                        }
                        int nlen = lex->pos - start;
                        char nbuf[64];
                        memcpy(nbuf, lex->source + start, nlen);
                        nbuf[nlen] = '\0';
                        add_number_token(lex, atof(nbuf), !has_dot, lex->line);
                        continue;
                    }
                    if (isalpha(c2) || c2 == '_') {
                        int start = lex->pos;
                        while (lex->pos < lex->length &&
                               (isalnum(lex->source[lex->pos]) || lex->source[lex->pos] == '_'))
                            lex->pos++;
                        int nlen = lex->pos - start;
                        TokenType kw = token_lookup_keyword(lex->source + start, nlen);
                        if (kw != TOK_IDENTIFIER) {
                            add_token(lex, kw, lex->line);
                        } else {
                            char *name = malloc(nlen + 1);
                            memcpy(name, lex->source + start, nlen);
                            name[nlen] = '\0';
                            add_string_token(lex, TOK_IDENTIFIER, name, lex->line);
                        }
                        continue;
                    }
                    /* Two-char operators */
                    if (lex->pos + 1 < lex->length) {
                        char t0 = lex->source[lex->pos], t1 = lex->source[lex->pos+1];
                        if (t0 == '=' && t1 == '=') { add_token(lex, TOK_EQ, lex->line); lex->pos += 2; continue; }
                        if (t0 == '!' && t1 == '=') { add_token(lex, TOK_NEQ, lex->line); lex->pos += 2; continue; }
                        if (t0 == '<' && t1 == '=') { add_token(lex, TOK_LTE, lex->line); lex->pos += 2; continue; }
                        if (t0 == '>' && t1 == '=') { add_token(lex, TOK_GTE, lex->line); lex->pos += 2; continue; }
                        if (t0 == '=' && t1 == '>') { add_token(lex, TOK_ARROW, lex->line); lex->pos += 2; continue; }
                        if (t0 == '|' && t1 == '>') { add_token(lex, TOK_PIPE, lex->line); lex->pos += 2; continue; }
                    }
                    int ln = lex->line;
                    lex->pos++;
                    switch (c2) {
                        case '+': add_token(lex, TOK_PLUS, ln); break;
                        case '-': add_token(lex, TOK_MINUS, ln); break;
                        case '*': add_token(lex, TOK_STAR, ln); break;
                        case '/': add_token(lex, TOK_SLASH, ln); break;
                        case '%': add_token(lex, TOK_PERCENT, ln); break;
                        case '<': add_token(lex, TOK_LT, ln); break;
                        case '>': add_token(lex, TOK_GT, ln); break;
                        case '=': add_token(lex, TOK_ASSIGN, ln); break;
                        case ',': add_token(lex, TOK_COMMA, ln); break;
                        case '.': add_token(lex, TOK_DOT, ln); break;
                        case ':': add_token(lex, TOK_COLON, ln); break;
                        case '(': add_token(lex, TOK_LPAREN, ln); break;
                        case ')': add_token(lex, TOK_RPAREN, ln); break;
                        case '[': add_token(lex, TOK_LBRACKET, ln); break;
                        case ']': add_token(lex, TOK_RBRACKET, ln); break;
                        default:
                            nova_syntax_error(ln, "Unexpected character: '%c'", c2);
                    }
                }

                /* Continue reading more segments */
                while (lex->pos < lex->length) {
                    has_interp = false;
                    seg = read_string(lex, quote, &has_interp);
                    if (!has_interp) {
                        add_string_token(lex, TOK_INTERP_END, seg, lex->line);
                        break;
                    }
                    add_string_token(lex, TOK_INTERP_MID, seg, lex->line);
                    lex->pos++; /* skip '{' */

                    brace_depth = 1;
                    while (lex->pos < lex->length && brace_depth > 0) {
                        skip_whitespace(lex);
                        if (lex->pos >= lex->length) break;
                        char c3 = peek(lex);
                        if (c3 == '}') { brace_depth--; if (brace_depth == 0) { lex->pos++; break; } }
                        if (c3 == '{') { brace_depth++; add_token(lex, TOK_LBRACE, lex->line); advance(lex); continue; }
                        if (c3 == '\n') { advance(lex); continue; }
                        if (c3 == '#') { skip_comment(lex); continue; }

                        if (c3 == '"' || c3 == '\'') {
                            char q3 = c3; advance(lex);
                            bool d; char *s = read_string(lex, q3, &d);
                            add_string_token(lex, TOK_STRING, s, lex->line);
                            continue;
                        }
                        if (isdigit(c3)) {
                            int st = lex->pos; bool hd = false;
                            while (lex->pos < lex->length) {
                                char c = lex->source[lex->pos];
                                if (c == '.' && !hd && isdigit(peek_next(lex))) { hd = true; lex->pos++; }
                                else if (isdigit(c)) lex->pos++;
                                else break;
                            }
                            int nl2 = lex->pos - st; char nb[64];
                            memcpy(nb, lex->source + st, nl2); nb[nl2] = '\0';
                            add_number_token(lex, atof(nb), !hd, lex->line);
                            continue;
                        }
                        if (isalpha(c3) || c3 == '_') {
                            int st = lex->pos;
                            while (lex->pos < lex->length && (isalnum(lex->source[lex->pos]) || lex->source[lex->pos] == '_'))
                                lex->pos++;
                            int nl2 = lex->pos - st;
                            TokenType kw = token_lookup_keyword(lex->source + st, nl2);
                            if (kw != TOK_IDENTIFIER) { add_token(lex, kw, lex->line); }
                            else {
                                char *nm = malloc(nl2+1); memcpy(nm, lex->source+st, nl2); nm[nl2]='\0';
                                add_string_token(lex, TOK_IDENTIFIER, nm, lex->line);
                            }
                            continue;
                        }
                        if (lex->pos+1 < lex->length) {
                            char t0=lex->source[lex->pos], t1=lex->source[lex->pos+1];
                            if (t0=='='&&t1=='='){add_token(lex,TOK_EQ,lex->line);lex->pos+=2;continue;}
                            if (t0=='!'&&t1=='='){add_token(lex,TOK_NEQ,lex->line);lex->pos+=2;continue;}
                            if (t0=='<'&&t1=='='){add_token(lex,TOK_LTE,lex->line);lex->pos+=2;continue;}
                            if (t0=='>'&&t1=='='){add_token(lex,TOK_GTE,lex->line);lex->pos+=2;continue;}
                            if (t0=='='&&t1=='>'){add_token(lex,TOK_ARROW,lex->line);lex->pos+=2;continue;}
                            if (t0=='|'&&t1=='>'){add_token(lex,TOK_PIPE,lex->line);lex->pos+=2;continue;}
                        }
                        int ln2 = lex->line; lex->pos++;
                        switch(c3){
                            case '+':add_token(lex,TOK_PLUS,ln2);break;
                            case '-':add_token(lex,TOK_MINUS,ln2);break;
                            case '*':add_token(lex,TOK_STAR,ln2);break;
                            case '/':add_token(lex,TOK_SLASH,ln2);break;
                            case '%':add_token(lex,TOK_PERCENT,ln2);break;
                            case '<':add_token(lex,TOK_LT,ln2);break;
                            case '>':add_token(lex,TOK_GT,ln2);break;
                            case '=':add_token(lex,TOK_ASSIGN,ln2);break;
                            case ',':add_token(lex,TOK_COMMA,ln2);break;
                            case '.':add_token(lex,TOK_DOT,ln2);break;
                            case ':':add_token(lex,TOK_COLON,ln2);break;
                            case '(':add_token(lex,TOK_LPAREN,ln2);break;
                            case ')':add_token(lex,TOK_RPAREN,ln2);break;
                            case '[':add_token(lex,TOK_LBRACKET,ln2);break;
                            case ']':add_token(lex,TOK_RBRACKET,ln2);break;
                            default:nova_syntax_error(ln2,"Unexpected character: '%c'",c3);
                        }
                    }
                }
            }
            continue;
        }

        /* Numbers */
        if (isdigit(ch)) {
            int line = lex->line;
            int start = lex->pos;
            bool has_dot = false;
            while (lex->pos < lex->length) {
                char c = lex->source[lex->pos];
                if (c == '.' && !has_dot && isdigit(peek_next(lex))) {
                    has_dot = true;
                    lex->pos++;
                } else if (isdigit(c)) {
                    lex->pos++;
                } else {
                    break;
                }
            }
            int len = lex->pos - start;
            char nbuf[64];
            memcpy(nbuf, lex->source + start, len);
            nbuf[len] = '\0';
            double num = atof(nbuf);
            add_number_token(lex, num, !has_dot, line);
            continue;
        }

        /* Identifiers and keywords */
        if (isalpha(ch) || ch == '_') {
            int line = lex->line;
            int start = lex->pos;
            while (lex->pos < lex->length &&
                   (isalnum(lex->source[lex->pos]) || lex->source[lex->pos] == '_'))
                lex->pos++;
            int len = lex->pos - start;
            TokenType kw = token_lookup_keyword(lex->source + start, len);
            if (kw != TOK_IDENTIFIER) {
                add_token(lex, kw, line);
            } else {
                char *name = malloc(len + 1);
                memcpy(name, lex->source + start, len);
                name[len] = '\0';
                add_string_token(lex, TOK_IDENTIFIER, name, line);
            }
            continue;
        }

        /* Two-character operators */
        if (lex->pos + 1 < lex->length) {
            char c0 = lex->source[lex->pos], c1 = lex->source[lex->pos + 1];
            if (c0 == '=' && c1 == '=') {
                add_token(lex, TOK_EQ, lex->line); lex->pos += 2; continue;
            }
            if (c0 == '!' && c1 == '=') {
                add_token(lex, TOK_NEQ, lex->line); lex->pos += 2; continue;
            }
            if (c0 == '<' && c1 == '=') {
                add_token(lex, TOK_LTE, lex->line); lex->pos += 2; continue;
            }
            if (c0 == '>' && c1 == '=') {
                add_token(lex, TOK_GTE, lex->line); lex->pos += 2; continue;
            }
            if (c0 == '=' && c1 == '>') {
                add_token(lex, TOK_ARROW, lex->line); lex->pos += 2; continue;
            }
            if (c0 == '|' && c1 == '>') {
                add_token(lex, TOK_PIPE, lex->line); lex->pos += 2; continue;
            }
        }

        /* Single-character tokens */
        int line = lex->line;
        lex->pos++;
        switch (ch) {
            case '+': add_token(lex, TOK_PLUS, line); break;
            case '-': add_token(lex, TOK_MINUS, line); break;
            case '*': add_token(lex, TOK_STAR, line); break;
            case '/': add_token(lex, TOK_SLASH, line); break;
            case '%': add_token(lex, TOK_PERCENT, line); break;
            case '<': add_token(lex, TOK_LT, line); break;
            case '>': add_token(lex, TOK_GT, line); break;
            case '=': add_token(lex, TOK_ASSIGN, line); break;
            case ',': add_token(lex, TOK_COMMA, line); break;
            case '.': add_token(lex, TOK_DOT, line); break;
            case ':': add_token(lex, TOK_COLON, line); break;
            case '(': lex->paren_depth++; add_token(lex, TOK_LPAREN, line); break;
            case ')': lex->paren_depth--; add_token(lex, TOK_RPAREN, line); break;
            case '[': lex->paren_depth++; add_token(lex, TOK_LBRACKET, line); break;
            case ']': lex->paren_depth--; add_token(lex, TOK_RBRACKET, line); break;
            case '{': add_token(lex, TOK_LBRACE, line); break;
            case '}': add_token(lex, TOK_RBRACE, line); break;
            default:
                nova_syntax_error(line, "Unexpected character: '%c'", ch);
        }
    }

    /* Ensure trailing newline */
    if (lex->token_count > 0 && lex->tokens[lex->token_count - 1].type != TOK_NEWLINE) {
        add_token(lex, TOK_NEWLINE, lex->line);
    }
    add_token(lex, TOK_EOF, lex->line);

    *count = lex->token_count;
    return lex->tokens;
}
