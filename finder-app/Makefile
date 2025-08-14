# Makefile for the writer app w/ cross-compilation support

# Toolchain config
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Wextra -g

# Targets
# Create a an executable program called writer
TARGET := writer
# List source code files
SRC := writer.c
# Automaticallly takes .c file and converts it to .o (object file)
OBJ := $(SRC:.c=.o)

# Default target
all: $(TARGET)

#Link object file to create exe
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