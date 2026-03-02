#!/bin/bash
# Integration tests for tinySQL
# Runs queries via pipe and checks for expected output patterns

set -euo pipefail

TINYSQL="./tinysql"
DATA_DIR="data"
PASS=0
FAIL=0

run_query() {
    local desc="$1"
    local query="$2"
    local pattern="$3"
    
    output=$(echo "$query" | $TINYSQL "$DATA_DIR" 2>&1)
    
    if echo "$output" | grep -qi "$pattern"; then
        echo "  ✓ $desc"
        PASS=$((PASS + 1))
    else
        echo "  ✗ $desc"
        echo "    Query:    $query"
        echo "    Expected: $pattern"
        echo "    Got:      $output"
        FAIL=$((FAIL + 1))
    fi
}

echo "=== tinySQL Integration Tests ==="
echo ""

# basic SELECT *
run_query "SELECT * FROM students" \
    "SELECT * FROM students LIMIT 3" \
    "Alice"

# SELECT specific columns
run_query "SELECT name, gpa" \
    "SELECT name, gpa FROM students LIMIT 5" \
    "name"

# WHERE clause
run_query "WHERE with comparison" \
    "SELECT name, gpa FROM students WHERE gpa > 3.7" \
    "Alice"

# WHERE with string
run_query "WHERE with string equality" \
    "SELECT name FROM students WHERE major = 'Computer Science'" \
    "Charlie"

# ORDER BY
run_query "ORDER BY DESC" \
    "SELECT name, gpa FROM students ORDER BY gpa DESC LIMIT 3" \
    "Ivy"

# COUNT
run_query "COUNT(*)" \
    "SELECT COUNT(*) FROM students" \
    "15"

# AVG
run_query "AVG aggregation" \
    "SELECT AVG(gpa) FROM students" \
    "3."

# WHERE + ORDER BY
run_query "WHERE + ORDER BY combo" \
    "SELECT name, gpa FROM students WHERE year = 2025 ORDER BY gpa DESC" \
    "Alice"

# JOIN
run_query "INNER JOIN" \
    "SELECT name, course_name FROM students JOIN courses ON students.id = courses.student_id LIMIT 5" \
    "Intro to CS"

# LIKE
run_query "LIKE pattern" \
    "SELECT name FROM students WHERE name LIKE 'A%'" \
    "Alice"

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

if [ $FAIL -gt 0 ]; then
    exit 1
fi
