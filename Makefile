# Makefile for C# Project Intelligence CLI Scanner (C Edition)

CC = gcc
CFLAGS = -Wall -O2 -Werror
TARGET = cli_scanner

# Windows detection
ifdef OS
   RM = del /Q
   TARGET_EXT = .exe
else
   RM = rm -f
   TARGET_EXT =
endif

BIN = $(TARGET)$(TARGET_EXT)

all: $(BIN)

install: install.bat $(BIN)

$(BIN): cli_scanner.c cli_scanner.h
	$(CC) $(CFLAGS) cli_scanner.c -o $(BIN)

clean:
	$(RM) $(BIN)

# Run the compiled tool on current directory
run: all
	./$(BIN) .

# Run the compiled tool on current directory with auto-repair active
fix: all
	./$(BIN) . --fix

# Boot the standalone C terminal interface & interactive nano editor!
ui: all
	./$(BIN) . --ui