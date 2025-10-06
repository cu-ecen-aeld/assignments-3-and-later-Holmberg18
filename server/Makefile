CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt 
SRC = aesdsocket.c
OBJ = $(SRC:.c=.o)

# Char device flag - set 1 to enable, 0 to disable
USE_CHAR_DEVICE ?= 1

ifeq ($(USE_CHAR_DEVICE),1)
CFLAGS += -DUSE_AESD_CHAR_DEVICE=1
endif;

all: $(TARGET)

$(TARGET): $(OBJ)
		$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c 
		$(CC) $(CFLAGS) -c $< -o $@

clean:
		rm -f $(TARGET) $(OBJ)

install:
		@echo "No install target defined"

.PHONY: all clean install