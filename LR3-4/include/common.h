#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <locale.h>
#include <wchar.h>
#include <errno.h>
#include <iconv.h>

#define MAX_PATH_LEN  4096
#define BUFFER_SIZE   65536


wchar_t* utf8_to_wchar( const char *utf8_str );
char* wchar_to_utf8( const wchar_t *wchar_str );

char* read_utf8_file( const char *filename );
int write_wstring_to_file( const wchar_t *wchar_str,
                           FILE *file );

void create_directory( const char *path );

int is_valid_filename( const char *filename );

#endif // COMMON_H