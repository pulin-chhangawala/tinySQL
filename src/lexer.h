#ifndef LEXER_H
#define LEXER_H

/*
 * SQL tokenizer. Turns a raw query string into a stream of tokens.
 *
 * Supported token types roughly map to SQL keywords + operators.
 * Nothing fancy, just enough for basic queries.
 */

typedef enum {
    /* keywords */
    TOK_SELECT, TOK_FROM, TOK_WHERE, TOK_AND, TOK_OR, TOK_NOT,
    TOK_ORDER, TOK_BY, TOK_ASC, TOK_DESC,
    TOK_JOIN, TOK_ON, TOK_INNER, TOK_LEFT,
    TOK_INSERT, TOK_INTO, TOK_VALUES,
    TOK_COUNT, TOK_SUM, TOK_AVG, TOK_MIN, TOK_MAX,
    TOK_AS, TOK_LIMIT, TOK_GROUP, TOK_HAVING,
    TOK_LIKE, TOK_IN, TOK_IS, TOK_NULL, TOK_DISTINCT,

    /* literals and identifiers */
    TOK_IDENT,      /* column/table name */
    TOK_STRING,     /* 'quoted string' */
    TOK_NUMBER,     /* integer or float */

    /* operators */
    TOK_STAR,       /* * */
    TOK_COMMA,      /* , */
    TOK_DOT,        /* . */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_EQ,         /* = */
    TOK_NEQ,        /* != or <> */
    TOK_LT,         /* < */
    TOK_GT,         /* > */
    TOK_LTE,        /* <= */
    TOK_GTE,        /* >= */
    TOK_SEMICOLON,  /* ; */

    TOK_EOF,
    TOK_ERROR
} token_type_t;

typedef struct {
    token_type_t type;
    char value[256];    /* token text */
    double num_value;   /* parsed numeric value (for TOK_NUMBER) */
} token_t;

typedef struct {
    const char *input;
    int pos;
    int len;
    token_t current;
} lexer_t;

void lexer_init(lexer_t *lex, const char *input);
token_t lexer_next(lexer_t *lex);
token_t lexer_peek(lexer_t *lex);
const char *token_type_name(token_type_t type);

#endif /* LEXER_H */
