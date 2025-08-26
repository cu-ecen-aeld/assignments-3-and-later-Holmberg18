# Makefile for the writer app w/ cross-compilation support

# Toolchain config - Buildroot will override CC and CFLAGS
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Wextra -g

# Targets
TARGET := writer
SRC := writer.c
OBJ := $(SRC:.c=.o)

# Default target
all: $(TARGET)

# Link object file to create executable
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $^ -o $@
	@echo "DEBUG: Successfully built $@"

# Compile source to object file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean artifacts
clean:
	rm -f $(TARGET) $(OBJ)

.PHONY: all clean