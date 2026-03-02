#define _POSIX_C_SOURCE 200809L
/*
 * tinySQL - a minimal SQL query engine over CSV files
 *
 * Built from scratch as a learning exercise. Supports:
 *   SELECT, INSERT, CREATE TABLE, DELETE,
 *   WHERE, ORDER BY, JOIN (nested loop + hash join),
 *   GROUP BY aggregations, DISTINCT, LIMIT, LIKE.
 *   Includes a query optimizer and EXPLAIN command.
 *
 * Usage:
 *   ./tinysql [data_dir]      # start REPL, look for *.csv in data_dir
 *   echo "SELECT ..." | ./tinysql data/
 *
 * author: pulin chhangawala
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <strings.h>
#include "parser.h"
#include "table.h"
#include "executor.h"
#include "planner.h"
#include "mutate.h"
#include "optimizer.h"

#define MAX_QUERY_LEN 4096

static volatile int running = 1;

static void sigint_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nBye.\n");
}

static void print_banner(void) {
    printf("\n");
    printf("  tinySQL v0.2: a SQL engine that fits in your pocket\n");
    printf("  Type SQL queries, or .help for commands. Ctrl-C to quit.\n");
    printf("\n");
}

static void print_help(void) {
    printf("\n");
    printf("  Commands:\n");
    printf("    .help               Show this message\n");
    printf("    .tables             List loaded tables\n");
    printf("    .schema <t>         Show columns of table <t>\n");
    printf("    .explain <SQL>      Show query execution plan\n");
    printf("    .optimize <SQL>     Show optimizer analysis\n");
    printf("    .quit               Exit\n");
    printf("\n");
    printf("  Queries:\n");
    printf("    SELECT [DISTINCT] <cols|*> FROM <table>\n");
    printf("    [JOIN <table> ON <col> = <col>]\n");
    printf("    [WHERE <col> <op> <val> [AND|OR ...]]\n");
    printf("    [GROUP BY <col>]\n");
    printf("    [ORDER BY <col> [ASC|DESC]]\n");
    printf("    [LIMIT <n>]\n");
    printf("\n");
    printf("  Mutations:\n");
    printf("    INSERT INTO <table> VALUES (v1, v2, ...)\n");
    printf("    INSERT INTO <table> (c1, c2) VALUES (v1, v2)\n");
    printf("    CREATE TABLE <name> (col1, col2, ...)\n");
    printf("    DELETE FROM <table> WHERE <col> = <val>\n");
    printf("\n");
    printf("  Aggregations: COUNT, SUM, AVG, MIN, MAX\n");
    printf("  Operators: =, !=, <, >, <=, >=, LIKE\n");
    printf("\n");
}

static void handle_meta_command(exec_context_t *ctx, const char *cmd) {
    if (strcmp(cmd, ".help") == 0) {
        print_help();
    } else if (strcmp(cmd, ".quit") == 0 || strcmp(cmd, ".exit") == 0) {
        running = 0;
    } else if (strcmp(cmd, ".tables") == 0) {
        if (ctx->num_tables == 0) {
            printf("  (no tables loaded yet; run a query to auto-load)\n");
        } else {
            for (int i = 0; i < ctx->num_tables; i++) {
                printf("  %s (%d rows, %d cols)\n",
                       ctx->tables[i]->name,
                       ctx->tables[i]->num_rows,
                       ctx->tables[i]->num_columns);
            }
        }
    } else if (strncmp(cmd, ".schema ", 8) == 0) {
        const char *name = cmd + 8;
        while (*name == ' ') name++;
        table_t *t = exec_find_table(ctx, name);
        if (!t) t = exec_load_table(ctx, name);
        if (!t) {
            printf("  table '%s' not found\n", name);
        } else {
            printf("  Table: %s (%d rows)\n", t->name, t->num_rows);
            for (int i = 0; i < t->num_columns; i++)
                printf("    %d: %s\n", i, t->columns[i].name);
        }
    } else if (strncmp(cmd, ".explain ", 8) == 0) {
        const char *sql = cmd + 8;
        while (*sql == ' ') sql++;
        query_t q;
        char err[512] = {0};
        if (parse_query(sql, &q, err, sizeof(err)) < 0) {
            printf("  Parse error: %s\n", err);
        } else {
            plan_node_t *plan = plan_create(&q);
            printf("\n  Query Plan:\n");
            plan_print(plan, 1);
            printf("\n");
            plan_destroy(plan);
        }
    } else if (strncmp(cmd, ".optimize ", 10) == 0) {
        const char *sql = cmd + 10;
        while (*sql == ' ') sql++;
        query_t q;
        char err[512] = {0};
        if (parse_query(sql, &q, err, sizeof(err)) < 0) {
            printf("  Parse error: %s\n", err);
        } else {
            optimizer_result_t opt = optimize_query(ctx, &q);
            optimizer_explain(&opt);
        }
    } else {
        printf("  Unknown command: %s (try .help)\n", cmd);
    }
}

int main(int argc, char **argv) {
    const char *data_dir = argc > 1 ? argv[1] : "data";

    signal(SIGINT, sigint_handler);

    exec_context_t ctx;
    exec_init(&ctx, data_dir);

    int interactive = isatty(fileno(stdin));

    if (interactive) {
        print_banner();
        printf("  Data directory: %s/\n\n", data_dir);
    }

    char query_buf[MAX_QUERY_LEN];

    while (running) {
        if (interactive)
            printf("tinysql> ");

        if (!fgets(query_buf, sizeof(query_buf), stdin)) break;

        /* strip trailing newline */
        int len = strlen(query_buf);
        while (len > 0 && (query_buf[len-1] == '\n' || query_buf[len-1] == '\r'))
            query_buf[--len] = '\0';

        /* skip empty lines */
        if (len == 0) continue;

        /* meta commands */
        if (query_buf[0] == '.') {
            handle_meta_command(&ctx, query_buf);
            continue;
        }

        /* strip trailing semicolon */
        if (query_buf[len-1] == ';')
            query_buf[--len] = '\0';

        /* detect mutation statements */
        if (strncasecmp(query_buf, "INSERT", 6) == 0) {
            char err[512] = {0};
            int n = exec_insert(&ctx, query_buf, err, sizeof(err));
            if (n < 0) {
                printf("  Error: %s\n", err);
            } else {
                printf("  %d row(s) inserted.\n", n);
            }
            continue;
        }

        if (strncasecmp(query_buf, "CREATE", 6) == 0) {
            char err[512] = {0};
            int n = exec_create_table(&ctx, query_buf, err, sizeof(err));
            if (n < 0) {
                printf("  Error: %s\n", err);
            } else {
                printf("  Table created.\n");
            }
            continue;
        }

        if (strncasecmp(query_buf, "DELETE", 6) == 0) {
            char err[512] = {0};
            int n = exec_delete(&ctx, query_buf, err, sizeof(err));
            if (n < 0) {
                printf("  Error: %s\n", err);
            } else {
                printf("  %d row(s) deleted.\n", n);
            }
            continue;
        }

        /* parse SELECT */
        query_t q;
        char parse_err[512] = {0};
        if (parse_query(query_buf, &q, parse_err, sizeof(parse_err)) < 0) {
            printf("  Error: %s\n", parse_err);
            continue;
        }

        /* execute */
        char exec_err[512] = {0};
        table_t *result = exec_query(&ctx, &q, exec_err, sizeof(exec_err));

        if (!result) {
            printf("  Error: %s\n", exec_err);
            continue;
        }

        table_print(result, q.limit);
        table_destroy(result);
    }

    exec_destroy(&ctx);
    return 0;
}
