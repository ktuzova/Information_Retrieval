CC = gcc
CFLAGS = -Wall -O2 -I./include
LDFLAGS = 
LIBS = 

INCLUDE_DIR = include
SRC_DIR = src
LIB_DIR = $(SRC_DIR)/lib
BIN_DIR = bin

PROGRAMS = indexer

LIB_SOURCES = $(LIB_DIR)/normalizer_lib.c \
              $(LIB_DIR)/stemmer_lib.c

MAIN_SOURCES = $(SRC_DIR)/indexer.c

all: $(addprefix $(BIN_DIR)/, $(PROGRAMS))

$(BIN_DIR)/indexer: $(SRC_DIR)/indexer.c $(SRC_DIR)/common.c $(LIB_SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(BIN_DIR)/*

.PHONY: all clean