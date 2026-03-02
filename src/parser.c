#include "parser.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * Recursive descent parser for a subset of SQL.
 *
 * Grammar (simplified):
 *   query     := SELECT [DISTINCT] columns FROM table [join] [where]
 *                [group_by] [order_by] [limit]
 *   columns   := column_ref (',' column_ref)* | '*'
 *   column_ref:= [agg_func '('] ident ['.' ident] [')'] [AS ident]
 *   join      := [LEFT|INNER] JOIN table ON column_ref = column_ref
 *   where     := WHERE condition (AND|OR condition)*
 *   condition := column_ref op value
 *   group_by  := GROUP BY ident (',' ident)*
 *   order_by  := ORDER BY ident [ASC|DESC] (',' ident [ASC|DESC])*
 *   limit     := LIMIT number
 */

static lexer_t lex;
static char *err_buf;
static int err_size;

static void parse_error(const char *msg) {
    snprintf(err_buf, err_size, "Parse error near '%s': %s",
             lex.current.value, msg);
}

static token_t expect(token_type_t type) {
    token_t tok = lexer_next(&lex);
    if (tok.type != type) {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "expected %s, got %s",
                 token_type_name(type), token_type_name(tok.type));
        parse_error(tmp);
    }
    return tok;
}

static bool match(token_type_t type) {
    token_t peek = lexer_peek(&lex);
    if (peek.type == type) {
        lexer_next(&lex);
        return true;
    }
    return false;
}

static agg_func_t parse_agg_func(token_type_t type) {
    switch (type) {
        case TOK_COUNT: return AGG_COUNT;
        case TOK_SUM:   return AGG_SUM;
        case TOK_AVG:   return AGG_AVG;
        case TOK_MIN:   return AGG_MIN;
        case TOK_MAX:   return AGG_MAX;
        default:        return AGG_NONE;
    }
}

static bool is_agg_token(token_type_t type) {
    return type == TOK_COUNT || type == TOK_SUM || type == TOK_AVG ||
           type == TOK_MIN || type == TOK_MAX;
}

static int parse_column_ref(column_ref_t *col) {
    memset(col, 0, sizeof(*col));

    token_t peek = lexer_peek(&lex);

    /* check for aggregate function */
    if (is_agg_token(peek.type)) {
        token_t agg_tok = lexer_next(&lex);
        col->agg = parse_agg_func(agg_tok.type);
        expect(TOK_LPAREN);

        token_t inner = lexer_next(&lex);
        if (inner.type == TOK_STAR) {
            col->is_star = true;
            strcpy(col->name, "*");
        } else if (inner.type == TOK_IDENT) {
            strcpy(col->name, inner.value);
            /* check for table.column */
            if (lexer_peek(&lex).type == TOK_DOT) {
                lexer_next(&lex);
                strcpy(col->table, col->name);
                token_t col_tok = expect(TOK_IDENT);
                strcpy(col->name, col_tok.value);
            }
        } else {
            parse_error("expected column name or * in aggregate");
            return -1;
        }

        expect(TOK_RPAREN);
    }
    /* check for star (SELECT *) */
    else if (peek.type == TOK_STAR) {
        lexer_next(&lex);
        col->is_star = true;
        strcpy(col->name, "*");
    }
    /* regular column reference */
    else {
        token_t name_tok = expect(TOK_IDENT);
        strcpy(col->name, name_tok.value);

        /* table.column? */
        if (lexer_peek(&lex).type == TOK_DOT) {
            lexer_next(&lex);
            strcpy(col->table, col->name);
            token_t col_tok = expect(TOK_IDENT);
            strcpy(col->name, col_tok.value);
        }
    }

    /* AS alias? */
    if (match(TOK_AS)) {
        token_t alias_tok = expect(TOK_IDENT);
        strcpy(col->alias, alias_tok.value);
    }

    return 0;
}

static cmp_op_t parse_cmp_op(void) {
    token_t tok = lexer_next(&lex);
    switch (tok.type) {
        case TOK_EQ:   return CMP_EQ;
        case TOK_NEQ:  return CMP_NEQ;
        case TOK_LT:   return CMP_LT;
        case TOK_GT:   return CMP_GT;
        case TOK_LTE:  return CMP_LTE;
        case TOK_GTE:  return CMP_GTE;
        case TOK_LIKE: return CMP_LIKE;
        default:
            parse_error("expected comparison operator");
            return CMP_EQ;
    }
}

static int parse_condition(condition_t *cond) {
    memset(cond, 0, sizeof(*cond));

    if (parse_column_ref(&cond->left) < 0) return -1;
    cond->op = parse_cmp_op();

    token_t val = lexer_next(&lex);
    if (val.type == TOK_STRING) {
        strcpy(cond->str_value, val.value);
        cond->is_numeric = false;
    } else if (val.type == TOK_NUMBER) {
        cond->num_value = val.num_value;
        snprintf(cond->str_value, sizeof(cond->str_value), "%s", val.value);
        cond->is_numeric = true;
    } else if (val.type == TOK_NULL) {
        strcpy(cond->str_value, "");
        cond->is_numeric = false;
    } else {
        parse_error("expected value in condition");
        return -1;
    }

    /* check for AND / OR */
    token_t peek = lexer_peek(&lex);
    if (peek.type == TOK_AND || peek.type == TOK_OR) {
        cond->logical = peek.type;
    }

    return 0;
}

int parse_query(const char *sql, query_t *query, char *error_buf, int error_buf_size) {
    memset(query, 0, sizeof(*query));
    query->limit = -1;
    err_buf = error_buf;
    err_size = error_buf_size;
    error_buf[0] = '\0';

    lexer_init(&lex, sql);

    /* SELECT */
    expect(TOK_SELECT);
    if (error_buf[0]) return -1;

    /* DISTINCT? */
    if (match(TOK_DISTINCT)) {
        query->distinct = true;
    }

    /* columns */
    do {
        if (query->num_columns >= MAX_COLUMNS) {
            parse_error("too many columns");
            return -1;
        }
        if (parse_column_ref(&query->columns[query->num_columns]) < 0)
            return -1;
        query->num_columns++;
    } while (match(TOK_COMMA));

    /* FROM */
    expect(TOK_FROM);
    if (error_buf[0]) return -1;

    token_t table_tok = expect(TOK_IDENT);
    if (error_buf[0]) return -1;
    strcpy(query->from_table.name, table_tok.value);

    /* optional table alias */
    token_t peek = lexer_peek(&lex);
    if (peek.type == TOK_IDENT || peek.type == TOK_AS) {
        if (peek.type == TOK_AS) lexer_next(&lex);
        token_t alias = expect(TOK_IDENT);
        strcpy(query->from_table.alias, alias.value);
    }

    /* JOIN? */
    peek = lexer_peek(&lex);
    if (peek.type == TOK_JOIN || peek.type == TOK_INNER || peek.type == TOK_LEFT) {
        query->has_join = true;

        if (peek.type == TOK_LEFT) {
            lexer_next(&lex);
            query->join.is_left_join = true;
        } else if (peek.type == TOK_INNER) {
            lexer_next(&lex);
        }

        expect(TOK_JOIN);
        if (error_buf[0]) return -1;

        token_t join_table = expect(TOK_IDENT);
        strcpy(query->join.table.name, join_table.value);

        /* optional alias */
        peek = lexer_peek(&lex);
        if (peek.type == TOK_IDENT || peek.type == TOK_AS) {
            if (peek.type == TOK_AS) lexer_next(&lex);
            token_t alias = expect(TOK_IDENT);
            strcpy(query->join.table.alias, alias.value);
        }

        expect(TOK_ON);
        if (error_buf[0]) return -1;

        parse_column_ref(&query->join.left_col);
        expect(TOK_EQ);
        parse_column_ref(&query->join.right_col);
    }

    /* WHERE? */
    if (match(TOK_WHERE)) {
        do {
            if (query->num_conditions >= MAX_CONDITIONS) {
                parse_error("too many conditions");
                return -1;
            }
            if (parse_condition(&query->conditions[query->num_conditions]) < 0)
                return -1;
            query->num_conditions++;
        } while (match(TOK_AND) || match(TOK_OR));
    }

    /* GROUP BY? */
    peek = lexer_peek(&lex);
    if (peek.type == TOK_GROUP) {
        lexer_next(&lex);
        expect(TOK_BY);
        do {
            token_t g = expect(TOK_IDENT);
            strcpy(query->group_by[query->num_group_by], g.value);
            query->num_group_by++;
        } while (match(TOK_COMMA));
    }

    /* ORDER BY? */
    peek = lexer_peek(&lex);
    if (peek.type == TOK_ORDER) {
        lexer_next(&lex);
        expect(TOK_BY);
        do {
            token_t o = expect(TOK_IDENT);
            strcpy(query->order_keys[query->num_order_keys].column, o.value);
            query->order_keys[query->num_order_keys].descending = false;

            peek = lexer_peek(&lex);
            if (peek.type == TOK_DESC) {
                lexer_next(&lex);
                query->order_keys[query->num_order_keys].descending = true;
            } else if (peek.type == TOK_ASC) {
                lexer_next(&lex);
            }
            query->num_order_keys++;
        } while (match(TOK_COMMA));
    }

    /* LIMIT? */
    if (match(TOK_LIMIT)) {
        token_t num = expect(TOK_NUMBER);
        query->limit = (int)num.num_value;
    }

    return error_buf[0] ? -1 : 0;
}

void query_print(const query_t *q) {
    printf("Query AST:\n");
    printf("  SELECT%s", q->distinct ? " DISTINCT" : "");
    for (int i = 0; i < q->num_columns; i++) {
        const column_ref_t *c = &q->columns[i];
        if (c->agg != AGG_NONE) {
            const char *agg_names[] = {"", "COUNT", "SUM", "AVG", "MIN", "MAX"};
            printf(" %s(%s)", agg_names[c->agg], c->name);
        } else {
            printf(" %s%s%s",
                   c->table[0] ? c->table : "",
                   c->table[0] ? "." : "",
                   c->name);
        }
        if (c->alias[0]) printf(" AS %s", c->alias);
        if (i < q->num_columns - 1) printf(",");
    }
    printf("\n  FROM %s", q->from_table.name);
    if (q->from_table.alias[0]) printf(" %s", q->from_table.alias);
    printf("\n");
    if (q->has_join) {
        printf("  %sJOIN %s ON %s.%s = %s.%s\n",
               q->join.is_left_join ? "LEFT " : "",
               q->join.table.name,
               q->join.left_col.table, q->join.left_col.name,
               q->join.right_col.table, q->join.right_col.name);
    }
    if (q->num_conditions > 0) {
        printf("  WHERE ");
        for (int i = 0; i < q->num_conditions; i++) {
            const condition_t *c = &q->conditions[i];
            printf("%s %s %s",
                   c->left.name,
                   c->op == CMP_EQ ? "=" : c->op == CMP_LT ? "<" : "?",
                   c->str_value);
            if (i < q->num_conditions - 1)
                printf(" %s ", c->logical == TOK_AND ? "AND" : "OR");
        }
        printf("\n");
    }
    if (q->num_order_keys > 0) {
        printf("  ORDER BY");
        for (int i = 0; i < q->num_order_keys; i++)
            printf(" %s %s", q->order_keys[i].column,
                   q->order_keys[i].descending ? "DESC" : "ASC");
        printf("\n");
    }
    if (q->limit >= 0) printf("  LIMIT %d\n", q->limit);
}
