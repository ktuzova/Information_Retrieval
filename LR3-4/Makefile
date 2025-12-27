CC = gcc
CFLAGS = -Wall -O2 -I./include
LDFLAGS = 
LIBS = 

INCLUDE_DIR = include
SRC_DIR = src
LIB_DIR = $(SRC_DIR)/lib
BIN_DIR = bin

PROGRAMS = tokenizer normalizer stopwords stemmer finalizer pipeline

LIB_SOURCES = $(LIB_DIR)/tokenizer_lib.c \
              $(LIB_DIR)/normalizer_lib.c \
              $(LIB_DIR)/stopwords_lib.c \
              $(LIB_DIR)/stemmer_lib.c \
              $(LIB_DIR)/finalizer_lib.c \
              $(LIB_DIR)/statistics_lib.c

MAIN_SOURCES = $(SRC_DIR)/tokenizer.c \
               $(SRC_DIR)/normalizer.c \
               $(SRC_DIR)/stopwords.c \
               $(SRC_DIR)/stemmer.c \
               $(SRC_DIR)/finalizer.c \
               $(SRC_DIR)/pipeline.c

all: $(addprefix $(BIN_DIR)/, $(PROGRAMS))

$(BIN_DIR)/tokenizer: $(SRC_DIR)/tokenizer.c $(SRC_DIR)/common.c $(LIB_DIR)/tokenizer_lib.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/normalizer: $(SRC_DIR)/normalizer.c $(SRC_DIR)/common.c $(LIB_DIR)/normalizer_lib.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/stopwords: $(SRC_DIR)/stopwords.c $(SRC_DIR)/common.c $(LIB_DIR)/stopwords_lib.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/stemmer: $(SRC_DIR)/stemmer.c $(SRC_DIR)/common.c $(LIB_DIR)/stemmer_lib.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR)/finalizer: $(SRC_DIR)/finalizer.c $(SRC_DIR)/common.c $(LIB_DIR)/finalizer_lib.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Pipeline использует все библиотеки
$(BIN_DIR)/pipeline: $(SRC_DIR)/pipeline.c $(SRC_DIR)/common.c $(LIB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(BIN_DIR)/*

.PHONY: all clean