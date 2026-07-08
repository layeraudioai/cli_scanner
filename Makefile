# Makefile for C# Project Intelligence CLI Scanner (C Edition)

CC = g++
CFLAGS = -Wall -O2 -Werror -std=c++11
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

$(BIN): cli_scanner.cpp cli_scanner.hpp webgl_bridge.c renderer.cpp
	$(CC) $(CFLAGS) cli_scanner.cpp webgl_bridge.c renderer.cpp -o $(BIN)

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