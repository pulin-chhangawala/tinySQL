CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lm

SRC_DIR = src
BUILD_DIR = build

SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/lexer.c $(SRC_DIR)/parser.c \
       $(SRC_DIR)/table.c $(SRC_DIR)/executor.c $(SRC_DIR)/planner.c \
       $(SRC_DIR)/index.c $(SRC_DIR)/hashjoin.c $(SRC_DIR)/mutate.c \
       $(SRC_DIR)/optimizer.c
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))
TARGET = tinysql

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

test: $(TARGET)
	@echo "=== Running integration tests ==="
	bash tests/test_queries.sh
