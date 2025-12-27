#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <locale.h>
#include <wchar.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "../include/common.h"

#include "../include/tokenizer_lib.h"
#include "../include/normalizer_lib.h"
#include "../include/stopwords_lib.h"
#include "../include/stemmer_lib.h"
#include "../include/finalizer_lib.h"
#include "../include/statistics_lib.h"

#define MAX_PATH_LEN  4096
#define BUFFER_SIZE   65536

static
void apply_full_pipeline( wchar_t *text )
{
    /*
        1. Токенизация
        2. Нормализация
        3. Удаление стоп-слов
        4. Стемминг
        5. Финальная обработка
    */

    if ( !text || wcslen(text) == 0 )
        return;

    // LR 3
    tokenize_text( text );
    normalize_text( text );
    remove_stopwords( text );
    // LR 4
    stem_text( text );
    remove_single_letter_words( text );
}

static
double get_current_time_seconds()
{
    struct timeval tv;
    gettimeofday( &tv, NULL );
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static
void process_file_pipeline( const char *input_path,
                            const char *output_path,
                            TextStatistics *total_stats )
{   
    double start_time = get_current_time_seconds();
    
    char *text_utf8 = read_utf8_file( input_path );
    if ( text_utf8 == NULL )
    {
        fprintf( stderr, "Erorr reading file: %s\n", input_path );
        return;
    }
    
    if ( strlen( text_utf8 ) == 0 )
    {
        free( text_utf8 );
        return;
    }
    
    struct stat file_info;
    if ( stat( input_path, &file_info ) != 0 )
    {
        fprintf( stderr, "Error getting file info: %s\n", input_path );
        free( text_utf8 );
        return;
    }
    size_t file_size = file_info.st_size;
    

    wchar_t *text_wchar = utf8_to_wchar( text_utf8 );
    free( text_utf8 );
    
    if ( text_wchar == NULL )
    {
        fprintf( stderr, "Error converting file: %s\n", input_path );
        return;
    }
    
    size_t content_len = wcslen( text_wchar );
    wchar_t* original = (wchar_t*)malloc( (content_len + 1) * sizeof(wchar_t) );
    if ( original == NULL )
    {
        fprintf( stderr, "Memmory alloc error\n" );
        free( text_wchar );
        return;
    }
    wcscpy( original, text_wchar );
    
    apply_full_pipeline( text_wchar );
    
    double end_time = get_current_time_seconds();
    double processing_time = end_time - start_time;
    
    TextStatistics file_stats;
    collect_statistics( text_wchar, file_size,
                        processing_time, &file_stats );
    
    if ( total_stats )
    {
        total_stats->token_count += file_stats.token_count;
        total_stats->total_token_length += file_stats.total_token_length;
        total_stats->input_size_bytes += file_stats.input_size_bytes;
        total_stats->processing_time_sec += file_stats.processing_time_sec;
    }
    
    FILE *output_file = fopen( output_path, "wb" );
    if ( !output_file )
    {
        fprintf( stderr, "Error creating file: %s\n", output_path );
        free( text_wchar );
        free( original );
        return;
    }
    
    if ( !write_wstring_to_file( text_wchar, output_file) )
        fprintf( stderr, "Error writing file: %s\n", output_path );
    
    fclose( output_file );
    free( text_wchar );
    free( original );
}

static
int process_directory_pipeline( const char* input_dir,
                                const char* output_dir )
{
    DIR* dir = opendir( input_dir );
    if ( !dir )
    {
        fprintf( stderr, "Error openning directory: %s\n", input_dir );
        return 0;
    }
    
    struct dirent* entry;
    int processed_files = 0;
    
    TextStatistics total_stats = { 0 };

    
    double total_start_time = get_current_time_seconds();
    
    while ( (entry = readdir( dir ) ) != NULL )
    {
        if ( (strcmp(entry->d_name, ".") == 0) ||
             (strcmp(entry->d_name, "..") == 0) )
            continue;
        
        if ( !is_valid_filename( entry->d_name ) )
            continue;
        
        char input_path[ MAX_PATH_LEN ];
        char output_path[ MAX_PATH_LEN ];
        
        snprintf( input_path, sizeof(input_path),
                  "%s/%s", input_dir, entry->d_name );
        snprintf( output_path, sizeof(output_path),
                  "%s/%s", output_dir, entry->d_name );
        
        process_file_pipeline( input_path, output_path, &total_stats );
        processed_files++;
    }
    
    closedir( dir );
    
    double total_end_time = get_current_time_seconds();
    double total_real_time = total_end_time - total_start_time;
    
    print_summary_statistics( &total_stats, processed_files );
    
    printf( "\nОбщее время выполнения: %.4f секунд\n", total_real_time );
    
    return processed_files;
}

int main( int argc, char* argv[] )
{
    setlocale( LC_ALL, "" );
    
    if ( argc != 3 )
    {
        printf("Использование: %s <входная_директория> <выходная_директория>\n", argv[0]);
        return 1;
    }
    
    const char* input_dir = argv[ 1 ];
    const char* output_dir = argv[ 2 ];
    
    DIR* test_dir = opendir( input_dir );
    if ( !test_dir )
    {
        fprintf(stderr, "No directory named: %s\n", input_dir);
        return 1;
    }
    closedir( test_dir );
    
    create_directory( output_dir );
    
    double program_start_time = get_current_time_seconds();
    int result = process_directory_pipeline( input_dir, output_dir );
    double program_end_time = get_current_time_seconds();
    
    printf( "\nОбработано файлов: %d\n", result );
    printf( "Общее время работы программы: %.4f секунд\n", 
            program_end_time - program_start_time );
    
    return 0;
}