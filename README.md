<p align="center">
  <img src="docs/banner.png" alt="tinySQL banner" width="800"/>
</p>

<h1 align="center">tinySQL</h1>

<p align="center">
  <strong>A SQL query engine built from scratch in C</strong><br/>
  <em>Hand-written lexer, recursive descent parser, hash joins, cost-based optimizer</em>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C11-blue?style=flat-square" alt="C11"/>
  <img src="https://img.shields.io/badge/lines_of_code-2%2C800+-brightgreen?style=flat-square" alt="LOC"/>
  <img src="https://img.shields.io/badge/tests-10%2F10_passing-success?style=flat-square" alt="Tests"/>
  <img src="https://img.shields.io/badge/parser-hand--written-orange?style=flat-square" alt="Parser"/>
</p>

---

## What is this?

A complete SQL engine that parses and executes queries directly against CSV files with no database server, no dependencies, just `make` and go. Every component is hand-written: the lexer, the recursive descent parser, the executor, the hash join, and the cost-based query optimizer.

Started this after realizing I'd been writing SQL for years but had no idea what actually happens between `SELECT` and the result set showing up. Turns out building even a toy query engine teaches you more about databases than any textbook chapter.

## Quick Start

```bash
make
./tinysql data/      # interactive REPL
```

```sql
tinysql> SELECT name, gpa FROM students WHERE gpa > 3.7 ORDER BY gpa DESC
| name   | gpa  |
|--------|------|
| Ivy    | 3.95 |
| Alice  | 3.9  |
| Leo    | 3.85 |
| Diana  | 3.8  |
| Olivia | 3.75 |
(5 rows)
```

```bash
# pipe mode
echo "SELECT COUNT(*) FROM students WHERE major = 'Computer Science'" | ./tinysql data/

# optimizer
tinysql> .optimize SELECT * FROM students JOIN courses ON id = student_id WHERE gpa > 3.5
```

---

## Features

### SQL Support

| Feature | Syntax | Implementation |
|---------|--------|----------------|
| Select columns | `SELECT name, gpa FROM students` | Projection via column index mapping |
| Where clause | `WHERE gpa > 3.5 AND major = 'CS'` | Expression tree evaluation |
| Ordering | `ORDER BY gpa DESC` | Stdlib qsort with AST-driven comparator |
| Limit | `LIMIT 10` | Early termination |
| Inner join | `JOIN courses ON students.id = courses.student_id` | **Hash join** (O(n+m)) or nested-loop |
| Left join | `LEFT JOIN courses ON ...` | Null-padded hash join |
| Aggregations | `COUNT(*)`, `SUM()`, `AVG()`, `MIN()`, `MAX()` | Single-pass accumulator |
| Pattern match | `WHERE name LIKE 'A%'` | Prefix/suffix/contains matching |
| Distinct | `SELECT DISTINCT major FROM students` | Hash set deduplication |
| Aliases | `SELECT name AS student_name` | Column rename in projection |
| Insert | `INSERT INTO students VALUES (...)` | CSV append with persistence |
| Create table | `CREATE TABLE name (col1, col2, ...)` | CSV file creation |
| Delete | `DELETE FROM students WHERE gpa < 2.0` | In-place row filtering |

### Query Optimizer

The `.optimize` command shows what the optimizer would do:

```
tinysql> .optimize SELECT * FROM students JOIN courses ON id = student_id WHERE gpa > 3.5

Query Plan:
  1. Scan: students (estimated: 20 rows)
  2. Filter pushdown: gpa > 3.5 (selectivity: ~30%, est: 6 rows)
  3. Hash Join: students.id = courses.student_id
     Build side: students (smaller after filter)
     Probe side: courses
  4. Estimated cost: 26 row-comparisons
     vs. naive nested-loop: 400 row-comparisons
```

**Optimizations applied**:
- **Predicate pushdown**: Filters are pushed below joins to reduce intermediate result sizes
- **Join ordering**: Smaller table is always the build side of the hash join
- **Cost estimation**: Row count estimates based on selectivity heuristics

### Hash Join

The hash join uses an O(n+m) algorithm: build a hash table on the smaller relation, then probe with the larger one:

```c
// Build phase: hash the join key from the smaller table
for each row in build_table:
    bucket = hash(row[join_col]) % num_buckets
    insert row into bucket

// Probe phase: scan the larger table
for each row in probe_table:
    bucket = hash(row[join_col]) % num_buckets
    for each match in bucket:
        emit joined row
```

This replaces the naive O(n×m) nested-loop join. On a 1000×1000 row join: **1M comparisons → ~2K hash lookups**.

---

## Architecture

```
┌─────────────────────────────────────────────┐
│                   REPL                       │
│  .help  .tables  .schema  .optimize          │
└───────────────────┬─────────────────────────┘
                    │ SQL string
┌───────────────────▼─────────────────────────┐
│              Lexer (lexer.c)                 │
│  "SELECT name FROM students WHERE gpa > 3"  │
│  → [SELECT][IDENT:name][FROM][IDENT:stu...] │
└───────────────────┬─────────────────────────┘
                    │ token stream
┌───────────────────▼─────────────────────────┐
│         Parser (parser.c)                    │
│  Recursive descent, no yacc/bison           │
│  → AST: {select: [name], from: students,    │
│          where: {op: >, left: gpa, right: 3}}│
└───────────────────┬─────────────────────────┘
                    │ AST
┌───────────────────▼─────────────────────────┐
│       Optimizer (optimizer.c)                │
│  Predicate pushdown, join reordering,        │
│  cost estimation                             │
└───────────────────┬─────────────────────────┘
                    │ optimized plan
┌───────────────────▼─────────────────────────┐
│       Executor (executor.c)                  │
│  Hash join, nested-loop, aggregation,        │
│  sorting, projection                         │
└───────────────────┬─────────────────────────┘
                    │ result rows
┌───────────────────▼─────────────────────────┐
│        Table Display (table.c)               │
│  Formatted ASCII table output                │
└─────────────────────────────────────────────┘
```

### Source Files

| File | Lines | Purpose |
|------|-------|---------|
| `lexer.c/.h` | ~300 | Tokenizer: SQL string → token stream |
| `parser.c/.h` | ~500 | Recursive descent parser: tokens → AST |
| `executor.c/.h` | ~400 | Query execution: AST → result table |
| `table.c/.h` | ~250 | CSV loading, row storage, formatted output |
| `hashjoin.c/.h` | ~200 | O(n+m) hash join implementation |
| `optimizer.c/.h` | ~250 | Cost-based optimizer with predicate pushdown |
| `mutate.c/.h` | ~200 | INSERT/CREATE TABLE/DELETE with CSV persistence |
| `main.c` | ~100 | REPL loop, meta-commands, flag parsing |

---

## Data Format

Drop any CSV into the `data/` directory. First row is headers. The table name is the filename without `.csv`:

```bash
$ cat data/students.csv
id,name,gpa,major
1,Alice,3.9,Computer Science
2,Bob,3.2,Math
3,Carol,3.7,Computer Science
```

```sql
tinysql> SELECT * FROM students WHERE major = 'Computer Science'
```

---

## Testing

```bash
make test      # 10 integration tests
```

Tests pipe queries through the CLI and verify output patterns, testing the full stack from lexer through formatted output.

---

<p align="center">
  <sub>No dependencies. No ORM. No frameworks. Just C and SQL.</sub>
</p>
