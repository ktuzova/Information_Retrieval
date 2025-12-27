#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include "../include/common.h"
#include "../include/normalizer_lib.h"


static
void process_file( const char *input_path,
                   const char *output_path )
{
    printf( "Нормализация файла: %s\n", input_path );
    
    char *text_utf8 = read_utf8_file( input_path );
    if ( text_utf8 == NULL )
    {
        fprintf( stderr, "Error reading file: %s\n", input_path );
        return;
    }
    
    if ( strlen( text_utf8 ) == 0 )
    {
        printf( "Файл пустой: %s\n", input_path );
        free( text_utf8 );
        return;
    }
    
    wchar_t *text_wchar = utf8_to_wchar( text_utf8 );
    free( text_utf8 );
    
    if ( text_wchar == NULL )
    {
        fprintf( stderr, "Error converting file: %s\n", input_path );
        return;
    }
    
    normalize_text( text_wchar );
    
    FILE *output_file = fopen( output_path, "wb" );
    if ( !output_file )
    {
        fprintf( stderr, "Error creating file: %s: %s\n", output_path, strerror(errno) );
        free( text_wchar );
        return;
    }
    
    setvbuf( output_file, NULL, _IOFBF, BUFFER_SIZE );
    
    if ( !write_wstring_to_file( text_wchar, output_file ) )
        fprintf( stderr, "Error writing file\n" );
    
    fclose( output_file );
    free( text_wchar );
    
    printf( "Нормализован: %s -> %s\n", input_path, output_path );
}

static
int process_directory( const char *input_dir,
                       const char *output_dir )
{
    DIR *dir = opendir( input_dir );
    if ( !dir )
    {
        fprintf( stderr, "Error open directory: %s: %s\n", input_dir, strerror(errno) );
        return 0;
    }
    
    struct dirent *entry;
    int processed_files = 0;
    
    while ( (entry = readdir( dir )) != NULL )
    {
        if ( (strcmp(entry->d_name, ".") == 0) ||
             (strcmp(entry->d_name, "..") == 0) )
            continue;
        
        if ( !is_valid_filename( entry->d_name ) )
        {
            printf( "Пропускаем файл с невалидным именем: %s\n", entry->d_name );
            continue;
        }
        
        char input_path[ MAX_PATH_LEN ];
        char output_path[ MAX_PATH_LEN ];
        
        snprintf( input_path, sizeof(input_path),
                  "%s/%s", input_dir, entry->d_name );
        snprintf( output_path, sizeof(output_path),
                  "%s/%s", output_dir, entry->d_name );
        
        process_file( input_path, output_path );
        processed_files++;
    }
    
    closedir( dir );
    return processed_files;
}

int main( int argc, char *argv[] )
{
    setlocale( LC_ALL, "" );
    
    if ( argc != 3 )
    {
        printf( "Использование: %s <входная_директория> <выходная_директория>\n", argv[0] );
        return 1;
    }
    
    const char *input_dir = argv[ 1 ];
    const char *output_dir = argv[ 2 ];
    
    DIR *test_dir = opendir( input_dir );
    if ( !test_dir )
    {
        fprintf( stderr, "Входная директория не существует: %s\n", input_dir );
        return 1;
    }
    closedir( test_dir );
    
    create_directory( output_dir );
    
    int processed_files = process_directory( input_dir, output_dir );
    
    printf( "\nОбработано файлов: %d\n", processed_files );
    
    return 0;
}