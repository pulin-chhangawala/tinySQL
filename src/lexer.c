#include "lexer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

/* keyword lookup table */
typedef struct {
    const char *word;
    token_type_t type;
} keyword_entry_t;

static keyword_entry_t keywords[] = {
    {"SELECT",   TOK_SELECT},
    {"FROM",     TOK_FROM},
    {"WHERE",    TOK_WHERE},
    {"AND",      TOK_AND},
    {"OR",       TOK_OR},
    {"NOT",      TOK_NOT},
    {"ORDER",    TOK_ORDER},
    {"BY",       TOK_BY},
    {"ASC",      TOK_ASC},
    {"DESC",     TOK_DESC},
    {"JOIN",     TOK_JOIN},
    {"ON",       TOK_ON},
    {"INNER",    TOK_INNER},
    {"LEFT",     TOK_LEFT},
    {"INSERT",   TOK_INSERT},
    {"INTO",     TOK_INTO},
    {"VALUES",   TOK_VALUES},
    {"COUNT",    TOK_COUNT},
    {"SUM",      TOK_SUM},
    {"AVG",      TOK_AVG},
    {"MIN",      TOK_MIN},
    {"MAX",      TOK_MAX},
    {"AS",       TOK_AS},
    {"LIMIT",    TOK_LIMIT},
    {"GROUP",    TOK_GROUP},
    {"HAVING",   TOK_HAVING},
    {"LIKE",     TOK_LIKE},
    {"IN",       TOK_IN},
    {"IS",       TOK_IS},
    {"NULL",     TOK_NULL},
    {"DISTINCT", TOK_DISTINCT},
    {NULL, TOK_ERROR}
};

void lexer_init(lexer_t *lex, const char *input) {
    lex->input = input;
    lex->pos = 0;
    lex->len = strlen(input);
    lex->current.type = TOK_EOF;
}

static void skip_whitespace(lexer_t *lex) {
    while (lex->pos < lex->len && isspace(lex->input[lex->pos]))
        lex->pos++;
}

static token_type_t lookup_keyword(const char *word) {
    /* case-insensitive keyword match */
    char upper[256];
    int i;
    for (i = 0; word[i] && i < 255; i++)
        upper[i] = toupper(word[i]);
    upper[i] = '\0';

    for (keyword_entry_t *kw = keywords; kw->word; kw++) {
        if (strcmp(upper, kw->word) == 0)
            return kw->type;
    }
    return TOK_IDENT;
}

token_t lexer_next(lexer_t *lex) {
    token_t tok;
    memset(&tok, 0, sizeof(tok));

    skip_whitespace(lex);

    if (lex->pos >= lex->len) {
        tok.type = TOK_EOF;
        strcpy(tok.value, "EOF");
        lex->current = tok;
        return tok;
    }

    char c = lex->input[lex->pos];

    /* single-char tokens */
    switch (c) {
        case '*': tok.type = TOK_STAR;     tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        case ',': tok.type = TOK_COMMA;    tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        case '.': tok.type = TOK_DOT;      tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        case '(': tok.type = TOK_LPAREN;   tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        case ')': tok.type = TOK_RPAREN;   tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        case '=': tok.type = TOK_EQ;       tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        case ';': tok.type = TOK_SEMICOLON; tok.value[0] = c; lex->pos++; lex->current = tok; return tok;
        default: break;
    }

    /* two-char operators */
    if (c == '!' && lex->pos + 1 < lex->len && lex->input[lex->pos+1] == '=') {
        tok.type = TOK_NEQ;
        strcpy(tok.value, "!=");
        lex->pos += 2;
        lex->current = tok;
        return tok;
    }
    if (c == '<' && lex->pos + 1 < lex->len && lex->input[lex->pos+1] == '>') {
        tok.type = TOK_NEQ;
        strcpy(tok.value, "<>");
        lex->pos += 2;
        lex->current = tok;
        return tok;
    }
    if (c == '<' && lex->pos + 1 < lex->len && lex->input[lex->pos+1] == '=') {
        tok.type = TOK_LTE;
        strcpy(tok.value, "<=");
        lex->pos += 2;
        lex->current = tok;
        return tok;
    }
    if (c == '>' && lex->pos + 1 < lex->len && lex->input[lex->pos+1] == '=') {
        tok.type = TOK_GTE;
        strcpy(tok.value, ">=");
        lex->pos += 2;
        lex->current = tok;
        return tok;
    }
    if (c == '<') {
        tok.type = TOK_LT;
        tok.value[0] = c;
        lex->pos++;
        lex->current = tok;
        return tok;
    }
    if (c == '>') {
        tok.type = TOK_GT;
        tok.value[0] = c;
        lex->pos++;
        lex->current = tok;
        return tok;
    }

    /* string literal */
    if (c == '\'') {
        lex->pos++;  /* skip opening quote */
        int start = lex->pos;
        while (lex->pos < lex->len && lex->input[lex->pos] != '\'') {
            /* handle escaped quotes */
            if (lex->input[lex->pos] == '\\' && lex->pos + 1 < lex->len)
                lex->pos++;
            lex->pos++;
        }
        int len = lex->pos - start;
        if (len > 255) len = 255;
        strncpy(tok.value, &lex->input[start], len);
        tok.value[len] = '\0';
        tok.type = TOK_STRING;
        if (lex->pos < lex->len) lex->pos++;  /* skip closing quote */
        lex->current = tok;
        return tok;
    }

    /* number */
    if (isdigit(c) || (c == '-' && lex->pos + 1 < lex->len && isdigit(lex->input[lex->pos+1]))) {
        int start = lex->pos;
        if (c == '-') lex->pos++;
        while (lex->pos < lex->len && isdigit(lex->input[lex->pos]))
            lex->pos++;
        if (lex->pos < lex->len && lex->input[lex->pos] == '.') {
            lex->pos++;
            while (lex->pos < lex->len && isdigit(lex->input[lex->pos]))
                lex->pos++;
        }
        int len = lex->pos - start;
        if (len > 255) len = 255;
        strncpy(tok.value, &lex->input[start], len);
        tok.value[len] = '\0';
        tok.num_value = atof(tok.value);
        tok.type = TOK_NUMBER;
        lex->current = tok;
        return tok;
    }

    /* identifier or keyword */
    if (isalpha(c) || c == '_') {
        int start = lex->pos;
        while (lex->pos < lex->len &&
               (isalnum(lex->input[lex->pos]) || lex->input[lex->pos] == '_'))
            lex->pos++;
        int len = lex->pos - start;
        if (len > 255) len = 255;
        strncpy(tok.value, &lex->input[start], len);
        tok.value[len] = '\0';
        tok.type = lookup_keyword(tok.value);
        lex->current = tok;
        return tok;
    }

    /* unknown character */
    tok.type = TOK_ERROR;
    snprintf(tok.value, sizeof(tok.value), "unexpected char: '%c'", c);
    lex->pos++;
    lex->current = tok;
    return tok;
}

token_t lexer_peek(lexer_t *lex) {
    /* save state, get next, restore */
    int saved_pos = lex->pos;
    token_t saved_current = lex->current;
    token_t next = lexer_next(lex);
    lex->pos = saved_pos;
    lex->current = saved_current;
    return next;
}

const char *token_type_name(token_type_t type) {
    switch (type) {
        case TOK_SELECT:    return "SELECT";
        case TOK_FROM:      return "FROM";
        case TOK_WHERE:     return "WHERE";
        case TOK_AND:       return "AND";
        case TOK_OR:        return "OR";
        case TOK_JOIN:      return "JOIN";
        case TOK_ON:        return "ON";
        case TOK_ORDER:     return "ORDER";
        case TOK_BY:        return "BY";
        case TOK_IDENT:     return "IDENT";
        case TOK_STRING:    return "STRING";
        case TOK_NUMBER:    return "NUMBER";
        case TOK_STAR:      return "STAR";
        case TOK_COMMA:     return "COMMA";
        case TOK_EQ:        return "EQ";
        case TOK_NEQ:       return "NEQ";
        case TOK_LT:        return "LT";
        case TOK_GT:        return "GT";
        case TOK_LTE:       return "LTE";
        case TOK_GTE:       return "GTE";
        case TOK_EOF:       return "EOF";
        case TOK_ERROR:     return "ERROR";
        default:            return "???";
    }
}
