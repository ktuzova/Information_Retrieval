#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>

#include "../include/common.h"
#include "../include/normalizer_lib.h"
#include "../include/stemmer_lib.h"

#define MEMORY_LIMIT 20 * 1024 * 1024
#define MAX_TERM_LEN 256
#define HASH_TABLE_SIZE 65536
#define MAGIC_NUMBER 0x42584449

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
typedef struct PostingNode {
    uint32_t doc_id;
    struct PostingNode *next;
} PostingNode;

typedef struct TermEntry {
    char *term;
    PostingNode *postings;
    struct TermEntry *next;
} TermEntry;

typedef struct {
    TermEntry **table;
    size_t count;
    size_t mem_usage;
} SPIMIIndex;

typedef struct {
    char *term;
    uint32_t *postings;
    uint32_t count;
    uint32_t block_id;
    FILE *block_file;
    uint32_t terms_left;
    int is_valid;
} MergeItem;

typedef struct {
    char *term;
    uint64_t offset;
    uint32_t count;
} DictionaryEntry;

typedef struct DictHashEntry {
    char *term;
    uint64_t offset;
    uint32_t count;
    struct DictHashEntry *next;
} DictHashEntry;

typedef struct {
    DictHashEntry **table;
    uint32_t size;
    uint32_t count;
} DictHashTable;

typedef struct {
    char *file_path;
    FILE *file_handle;
    uint64_t postings_start;
    DictHashTable *hash_table;
    DictionaryEntry *dictionary_array;
    uint32_t dict_size;
} IndexHandle;

typedef enum {
    TOKEN_TERM,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *value;
} Token;

typedef struct {
    Token * tokens;
    int count;
    int capacity;
    int current;
} TokenList;

typedef struct {
    uint32_t *lists[ 2 ];
    uint32_t counts[ 2 ];
    int top;
} EvalStack;

static
double get_current_time_seconds()
{
    struct timeval tv;
    gettimeofday( &tv, NULL );
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
// ФУНКЦИИ ПОСТРОЕНИЯ ИНДЕКСА

// Создание и управление SPIMI индексом
static
SPIMIIndex *create_spimi_index()
{
    SPIMIIndex *index = malloc( sizeof(SPIMIIndex) );
    index->table = calloc( HASH_TABLE_SIZE, sizeof(TermEntry *) );
    index->count = 0;
    index->mem_usage = 0;
    return index;
}

static
void free_spimi_index( SPIMIIndex *index )
{
    for ( int i = 0; i < HASH_TABLE_SIZE; i++ )
    {
        TermEntry *entry = index->table[ i ];
        while ( entry )
        {
            TermEntry *next = entry->next;
            PostingNode *posting = entry->postings;
            while ( posting )
            {
                PostingNode *next_posting = posting->next;
                free( posting );
                posting = next_posting;
            }
            free( entry->term );
            free( entry );
            entry = next;
        }
    }
    free( index->table );
    free( index );
}

static
uint32_t hash_string( const char *str )
{
    uint32_t hash = 5381;
    int c;
    while ( c = *str++ )
        hash = ((hash << 5) + hash) + c;

    return hash % HASH_TABLE_SIZE;
}

static
void add_posting( SPIMIIndex *index,
                  const char *term,
                  uint32_t doc_id )
{
    uint32_t hash = hash_string( term );
    
    TermEntry *entry = index->table[ hash ];
    while ( entry )
    {
        if ( strcmp( entry->term, term ) == 0 )
            break;
        entry = entry->next;
    }
    
    if ( !entry )
    {
        entry = malloc( sizeof(TermEntry) );
        entry->term = strdup( term );
        entry->postings = NULL;
        entry->next = index->table[ hash ];
        index->table[ hash ] = entry;
        index->count++;
        index->mem_usage += strlen( term ) + sizeof(TermEntry);
    }
    
    PostingNode *last = NULL;
    PostingNode *current = entry->postings;
    
    while ( current )
    {
        if ( current->doc_id == doc_id ) return;
        last = current;
        current = current->next;
    }
    
    PostingNode *new_posting = malloc( sizeof( PostingNode ) );
    new_posting->doc_id = doc_id;
    new_posting->next = NULL;
    
    if ( last )
        last->next = new_posting;
    else
        entry->postings = new_posting;
    
    index->mem_usage += sizeof( PostingNode );
}

static
int compare_terms( const void *a, const void *b )
{
    const TermEntry *entry_a = *(const TermEntry **)a;
    const TermEntry *entry_b = *(const TermEntry **)b;
    return strcmp( entry_a->term, entry_b->term );
}

static
void write_spimi_block( SPIMIIndex * index, int block_id )
{
    char filename[ 50 ];
    sprintf( filename, "block_%d.bin", block_id );
    
    TermEntry **terms = malloc( index->count * sizeof(TermEntry *) );
    int term_idx = 0;
    
    for ( int i = 0; i < HASH_TABLE_SIZE; i++ )
    {
        TermEntry *entry = index->table[ i ];
        while ( entry )
        {
            terms[ term_idx++ ] = entry;
            entry = entry->next;
        }
    }
    
    qsort( terms, index->count, sizeof(TermEntry *), compare_terms );
    
    FILE *file = fopen( filename, "wb" );
    if ( !file )
    {
        perror( "Ошибка создания файла блока" );
        free( terms );
        return;
    }
    
    uint64_t count = index->count;
    fwrite( &count, sizeof(uint64_t), 1, file );
    
    for ( int i = 0; i < index->count; i++ )
    {
        TermEntry *entry = terms[ i ];
        size_t term_len = strlen( entry->term );
        uint32_t term_len32 = (uint32_t)term_len;
        
        uint32_t posting_count = 0;
        PostingNode *posting = entry->postings;
        while ( posting )
        {
            posting_count++;
            posting = posting->next;
        }
        
        fwrite( &term_len32, sizeof(uint32_t), 1, file );
        fwrite( entry->term, 1, term_len, file );
        fwrite( &posting_count, sizeof(uint32_t), 1, file );
        
        posting = entry->postings;
        while ( posting )
        {
            fwrite( &posting->doc_id, sizeof(uint32_t), 1, file );
            posting = posting->next;
        }
    }
    
    fclose( file );
    free( terms );
}

static
void merge_blocks( int block_count )
{
    if ( block_count == 0 )
    {
        printf( "Нет блоков для слияния\n" );
        return;
    }
    
    FILE **block_files = malloc( block_count * sizeof(FILE *) );
    uint64_t *term_counts = malloc( block_count * sizeof(uint64_t) );
    
    for ( int i = 0; i < block_count; i++ )
    {
        char filename[ 50 ];
        sprintf( filename, "block_%d.bin", i );
        block_files[i] = fopen( filename, "rb" );
        if ( !block_files[ i ] )
        {
            perror( "Ошибка открытия файла блока" );
            term_counts[ i ] = 0;
            continue;
        }
        fread( &term_counts[ i ], sizeof(uint64_t), 1, block_files[ i ] );
    }
    
    FILE *final_index = fopen( "final_index.bin", "wb" );
    if ( !final_index )
    {
        perror( "Ошибка создания финального индекса" );
        return;
    }
    
    uint32_t magic = MAGIC_NUMBER;
    uint8_t version = 1;
    uint64_t dict_size = 0;
    uint64_t postings_start = 0;
    uint64_t postings_size = 0;
    
    fwrite( &magic, sizeof(uint32_t), 1, final_index );
    fwrite( &version, sizeof(uint8_t), 1, final_index );
    fwrite( &dict_size, sizeof(uint64_t), 1, final_index );
    fwrite( &postings_start, sizeof(uint64_t ), 1, final_index );
    fwrite( &postings_size, sizeof(uint64_t), 1, final_index );
    
    MergeItem *current_items = malloc( block_count * sizeof(MergeItem) );
    uint64_t total_terms = 0;
    uint64_t unique_terms = 0;
    
    for ( int i = 0; i < block_count; i++ )
    {
        if ( term_counts[ i ] > 0 )
        {
            MergeItem *item = &current_items[ i ];
            
            uint32_t term_len;
            fread( &term_len, sizeof(uint32_t), 1, block_files[ i ] );
            
            item->term = malloc( term_len + 1 );
            fread( item->term, 1, term_len, block_files[ i ] );
            item->term[ term_len ] = '\0';
            
            uint32_t posting_count;
            fread( &posting_count, sizeof(uint32_t), 1, block_files[ i ] );
            
            item->postings = malloc( posting_count * sizeof(uint32_t) );
            fread( item->postings, sizeof(uint32_t), posting_count, block_files[ i ] );
            
            item->count = posting_count;
            item->block_id = i;
            item->terms_left = term_counts[ i ] - 1;
            item->is_valid = 1;
        } else
            current_items[ i ].is_valid = 0;

        total_terms += term_counts[ i ];
    }
    
    printf( "Общее количество терминов в блоках: %lu\n", total_terms );
    
    FILE *temp_postings = fopen( "temp_postings.bin", "wb" );
    if ( !temp_postings )
    {
        perror( "Ошибка создания временного файла постлистов" );
        return;
    }
    
    uint64_t current_posting_offset = 0;
    
    while ( 1 )
    {
        int min_idx = -1;
        for ( int i = 0; i < block_count; i++ )
        {
            if ( current_items[ i ].is_valid )
            {
                if ( (min_idx == -1) ||
                     strcmp( current_items[ i ].term, current_items[ min_idx ].term ) < 0 )
                    min_idx = i;
            }
        }
        
        if ( min_idx == -1 )
            break;
        
        MergeItem *current = &current_items[ min_idx ];
        
        uint32_t *merged_postings = malloc( current->count * sizeof(uint32_t) );
        memcpy( merged_postings, current->postings, current->count * sizeof(uint32_t) );
        uint32_t merged_count = current->count;
        
        for ( int i = 0; i < block_count; i++ )
        {
            if ( (i != min_idx) &&
                 current_items[ i ].is_valid &&
                 strcmp( current_items[ i ].term, current->term ) == 0 )
            {
                uint32_t *new_merged = malloc( (merged_count + current_items[ i ].count) * sizeof(uint32_t) );
                uint32_t idx1 = 0;
                uint32_t idx2 = 0;
                uint32_t idxm = 0;
                
                while ( (idx1 < merged_count) &&
                        (idx2 < current_items[ i ].count) )
                {
                    if ( merged_postings[ idx1 ] < current_items[ i ].postings[ idx2 ] )
                        new_merged[ idxm++ ] = merged_postings[ idx1++ ];
                    else if ( merged_postings[ idx1 ] > current_items[ i ].postings[ idx2 ] )
                        new_merged[ idxm++ ] = current_items[ i ].postings[ idx2++ ];
                    else
                    {
                        new_merged[ idxm++ ] = merged_postings[ idx1++ ];
                        idx2++;
                    }
                }
                
                while ( idx1 < merged_count )
                    new_merged[ idxm++ ] = merged_postings[ idx1++ ];
                while ( idx2 < current_items[ i ].count )
                    new_merged[ idxm++ ] = current_items[ i ].postings[ idx2++ ];
                
                free( merged_postings );
                merged_postings = new_merged;
                merged_count = idxm;
                
                if ( current_items[ i ].terms_left > 0 )
                {
                    free( current_items[ i ].term );
                    free( current_items[ i ].postings );
                    
                    uint32_t term_len;
                    fread( &term_len, sizeof(uint32_t), 1, block_files[ i ] );
                    
                    current_items[ i ].term = malloc( term_len + 1 );
                    fread( current_items[ i ].term, 1, term_len, block_files[ i ] );
                    current_items[ i ].term[ term_len ] = '\0';
                    
                    uint32_t posting_count;
                    fread( &posting_count, sizeof(uint32_t), 1, block_files[ i ] );
                    
                    current_items[ i ].postings = malloc( posting_count * sizeof(uint32_t) );
                    fread( current_items[ i ].postings, sizeof(uint32_t), posting_count, block_files[ i ] );
                    
                    current_items[ i ].count = posting_count;
                    current_items[ i ].terms_left--;
                } else
                {
                    current_items[ i ].is_valid = 0;
                    free( current_items[ i ].term );
                    free( current_items[ i ].postings );
                }
            }
        }
        
        uint32_t term_len = (uint32_t)strlen( current->term );
        fwrite( &term_len, sizeof(uint32_t), 1, final_index );
        fwrite( current->term, 1, term_len, final_index );
        fwrite( &current_posting_offset, sizeof(uint64_t), 1, final_index );
        fwrite( &merged_count, sizeof(uint32_t), 1, final_index );
        
        unique_terms++;
        
        fwrite( merged_postings, sizeof(uint32_t), merged_count, temp_postings );
        current_posting_offset += merged_count * sizeof( uint32_t );
        
        free( merged_postings );
        
        if ( current->terms_left > 0 )
        {
            free( current->term );
            free( current->postings );
            
            uint32_t term_len;
            fread( &term_len, sizeof(uint32_t), 1, block_files[ min_idx ] );
            
            current->term = malloc( term_len + 1 );
            fread( current->term, 1, term_len, block_files[ min_idx ] );
            current->term[term_len] = '\0';
            
            uint32_t posting_count;
            fread( &posting_count, sizeof(uint32_t), 1, block_files[ min_idx ] );
            
            current->postings = malloc( posting_count * sizeof(uint32_t) );
            fread( current->postings, sizeof(uint32_t), posting_count, block_files[ min_idx ] );
            
            current->count = posting_count;
            current->terms_left--;
        } else
        {
            current->is_valid = 0;
            free( current->term );
            free( current->postings );
        }
    }
    
    postings_start = ftell( final_index );
    
    fclose( temp_postings );
    temp_postings = fopen( "temp_postings.bin", "rb" );
    if ( temp_postings )
    {
        char buffer[ 8192 ];
        size_t bytes_read;
        while ( (bytes_read = fread( buffer, 1, sizeof(buffer), temp_postings )) > 0 )
            fwrite( buffer, 1, bytes_read, final_index );

        fclose( temp_postings );
        remove( "temp_postings.bin" );
    }
    
    postings_size = current_posting_offset;
    
    fseek( final_index, 5, SEEK_SET );
    fwrite( &unique_terms, sizeof(uint64_t), 1, final_index );
    fwrite( &postings_start, sizeof(uint64_t), 1, final_index );
    fwrite( &postings_size, sizeof(uint64_t), 1, final_index );
    
    fclose( final_index );
    
    for ( int i = 0; i < block_count; i++ )
    {
        if ( block_files[ i ] )
            fclose( block_files[ i ] );
    }
    
    free( block_files );
    free( term_counts );
    free( current_items );
    
    printf( "Финальный индекс сохранен в final_index.bin\n" );
    printf( "Словарь содержит %lu уникальных терминов\n", unique_terms );
}

// Вспомогательные функции для работы с файлами
static
int extract_doc_id( const char *filename )
{
    const char *basename = strrchr( filename, '/' );
    if ( basename )
        basename++;
    else
        basename = filename;
    
    const char * dot = strrchr( basename, '.' );
    if ( !dot ||
         (strcmp( dot, ".txt" ) != 0) )
        return -1;
    
    char num_str[ 50 ];
    strncpy( num_str, basename, dot - basename );
    num_str[ dot - basename ] = '\0';
    
    return atoi( num_str );
}

static
int compare_files( const void *a, const void *b )
{
    char *file_a = *(char **)a;
    char *file_b = *(char **)b;
    int id_a = extract_doc_id( file_a );
    int id_b = extract_doc_id( file_b );

    return id_a - id_b;
}

static
char **get_sorted_files( const char *dir_path, int *file_count )
{
    DIR *dir = opendir( dir_path );
    if ( !dir )
    {
        perror( "Ошибка открытия директории" );
        return NULL;
    }
    
    struct dirent *entry;
    char **files = NULL;
    int capacity = 100;
    int count = 0;
    
    files = malloc( capacity * sizeof(char *) );
    
    while ( (entry = readdir( dir )) != NULL )
    {
        if ( strstr( entry->d_name, ".txt" ) )
        {
            if ( count >= capacity )
            {
                capacity *= 2;
                files = realloc( files, capacity * sizeof( char * ) );
            }
            
            char *full_path = malloc( strlen( dir_path ) + strlen( entry->d_name ) + 2 );
            sprintf( full_path, "%s/%s", dir_path, entry->d_name );
            files[ count ] = full_path;
            count++;
        }
    }
    
    closedir( dir );
    
    if ( count > 0 )
        qsort( files, count, sizeof( char * ), compare_files );
    
    *file_count = count;
    return files;
}

static
void remove_intermediate_files( int block_count )
{
    for ( int i = 0; i < block_count; i++ )
    {
        char filename[50];
        sprintf( filename, "block_%d.bin", i );
        remove( filename );
    }
}

// Основная функция построения индекса
void build_index( const char *input_dir )
{
    printf( "Начало построения индекса...\n" );
    printf( "Входная директория: %s\n", input_dir );
    
    double start_time = get_current_time_seconds();

    int file_count = 0;
    char ** files = get_sorted_files( input_dir, &file_count );
    if ( file_count == 0 )
    {
        printf( "В директории нет .txt файлов\n" );
        return;
    }
    
    printf( "Найдено %d файлов\n", file_count );
    
    SPIMIIndex *index = create_spimi_index();
    int block_id = 0;
    char **block_files = NULL;
    int block_count = 0;
    
    for ( int i = 0; i < file_count; i++ )
    {
        uint32_t doc_id = extract_doc_id( files[ i ] );
        printf( "Обработка документа %u (%s)\n", doc_id, files[ i ] );
        
        FILE *file = fopen( files[ i ], "r" );
        if ( !file )
        {
            fprintf( stderr, "Ошибка открытия файла: %s (errno: %d)\n", files[i], errno );
            continue;
        }
        
        char token[ MAX_TERM_LEN ];
        char unique_tokens[ 1000 ][ MAX_TERM_LEN ];
        int unique_count = 0;
        
        while ( fscanf( file, "%255s", token ) == 1 )
        {
            int is_unique = 1;
            for ( int j = 0; j < unique_count; j++ )
            {
                if ( strcmp( token, unique_tokens[ j ] ) == 0 )
                {
                    is_unique = 0;
                    break;
                }
            }
            
            if ( is_unique )
            {
                if ( unique_count < 1000 )
                {
                    strcpy( unique_tokens[ unique_count ], token );
                    add_posting( index, token, doc_id );
                    unique_count++;
                }
            }
        }
        
        fclose( file );
        
        if ( index->mem_usage > MEMORY_LIMIT )
        {
            printf( "Достигнут лимит памяти, сохранение блока %d\n", block_id );
            write_spimi_block( index, block_id );
            
            block_files = realloc( block_files, (block_count + 1) * sizeof(char *) );
            block_files[ block_count ] = malloc( 50 );
            sprintf( block_files[ block_count ], "block_%d.bin", block_id );
            block_count++;
            
            free_spimi_index( index );
            index = create_spimi_index();
            
            block_id++;
        }
    }
    
    if ( index->count > 0 )
    {
        printf( "Сохранение последнего блока %d\n", block_id );
        write_spimi_block( index, block_id );
        
        block_files = realloc( block_files, (block_count + 1) * sizeof(char *) );
        block_files[ block_count ] = malloc( 50 );
        sprintf( block_files[ block_count ], "block_%d.bin", block_id );
        block_count++;
    }
    
    free_spimi_index( index );
    
    for ( int i = 0; i < file_count; i++ )
        free( files[ i ] );

    free( files );
    
    if ( block_count > 0 )
    {
        printf( "Слияние %d блоков...\n", block_count );
        merge_blocks( block_count );
        
        remove_intermediate_files( block_count );
        
        for ( int i = 0; i < block_count; i++ )
            free( block_files[ i ] );

        free( block_files );
    }
    
    double end_time = get_current_time_seconds();
    double processing_time = end_time - start_time;

    printf( "Построение индекса завершено\n" );
    printf( "Время построения индекса) %.4f секунд\n", processing_time );
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
// ФУНКЦИИ ЗАГРУЗКИ ИНДЕКСА

// Хэш-таблица для словаря
static
DictHashTable *create_dict_hash_table( uint32_t size )
{
    DictHashTable *table = malloc( sizeof(DictHashTable) );
    table->size = size;
    table->count = 0;
    table->table = calloc( size, sizeof(DictHashEntry *) );
    return table;
}

static
uint32_t dict_hash_string( const char *str, uint32_t table_size )
{
    uint32_t hash = 5381;
    int c;
    while ( c = *str++ )
        hash = ((hash << 5) + hash) + c;

    return hash % table_size;
}

static
void dict_hash_table_insert( DictHashTable * table,
                             const char * term,
                             uint64_t offset,
                             uint32_t count )
{
    uint32_t hash = dict_hash_string( term, table->size );
    
    DictHashEntry *entry = malloc( sizeof(DictHashEntry) );
    entry->term = strdup( term );
    entry->offset = offset;
    entry->count = count;
    
    entry->next = table->table[ hash ];
    table->table[ hash ] = entry;
    table->count++;
}

static
DictHashEntry *dict_hash_table_find( DictHashTable *table,
                                     const char *term )
{
    uint32_t hash = dict_hash_string( term, table->size );
    DictHashEntry *entry = table->table[ hash ];
    
    while ( entry )
    {
        if ( strcmp( entry->term, term ) == 0 )
            return entry;

        entry = entry->next;
    }
    return NULL;
}

static
void free_dict_hash_table( DictHashTable *table )
{
    if ( !table )
        return;
    
    for ( uint32_t i = 0; i < table->size; i++ )
    {
        DictHashEntry *entry = table->table[ i ];
        while ( entry )
        {
            DictHashEntry *next = entry->next;
            free( entry->term );
            free( entry );
            entry = next;
        }
    }
    free( table->table );
    free( table );
}

// Загрузка индекса
IndexHandle *load_index( const char * file_path )
{
    IndexHandle *handle = malloc( sizeof(IndexHandle) );
    handle->file_path = strdup( file_path );
    handle->file_handle = NULL;
    handle->dictionary_array = NULL;
    handle->dict_size = 0;
    
    FILE *file = fopen( file_path, "rb" );
    if ( !file )
    {
        perror( "Ошибка открытия индекса" );
        free( handle->file_path );
        free( handle );
        return NULL;
    }
    
    uint32_t magic;
    uint8_t version;
    uint64_t dict_size;
    
    fread( &magic, sizeof(uint32_t), 1, file );
    fread( &version, sizeof(uint8_t), 1, file );
    
    if ( (magic != MAGIC_NUMBER) ||
         (version != 1) )
    {
        fprintf( stderr, "Неверный формат файла индекса: magic=%x, version=%d\n", magic, version );
        fclose( file );
        free( handle->file_path );
        free( handle );
        return NULL;
    }
    
    fread( &dict_size, sizeof(uint64_t), 1, file );
    fread( &handle->postings_start, sizeof(uint64_t), 1, file );
    
    uint64_t postings_size;
    fread( &postings_size, sizeof(uint64_t), 1, file );
    
    if ( dict_size > 10000000 )
    {
        fprintf( stderr, "Слишком большое количество терминов: %lu. Файл поврежден?\n", dict_size );
        fclose( file );
        free( handle->file_path );
        free( handle );
        return NULL;
    }
    
    printf( "Загрузка словаря из %lu терминов...\n", dict_size );
    
    uint32_t hash_table_size = (uint32_t)(dict_size * 2);
    if ( hash_table_size < 1024 )
        hash_table_size = 1024;
    handle->hash_table = create_dict_hash_table( hash_table_size );
    
    handle->dictionary_array = malloc( dict_size * sizeof(DictionaryEntry) );
    handle->dict_size = (uint32_t)dict_size;
    
    for ( uint64_t i = 0; i < dict_size; i++ )
    {
        uint32_t term_len;
        if ( fread( &term_len, sizeof(uint32_t), 1, file ) != 1 )
        {
            fprintf( stderr, "Ошибка чтения длины термина %lu\n", i );
            fclose( file );
            free_dict_hash_table( handle->hash_table );
            free( handle->dictionary_array );
            free( handle->file_path );
            free( handle );
            return NULL;
        }
        
        if ( term_len > MAX_TERM_LEN )
        {
            fprintf( stderr, "Термин %lu слишком длинный: %u (макс: %d)\n", i, term_len, MAX_TERM_LEN );
            fclose( file );
            free_dict_hash_table( handle->hash_table );
            free( handle->dictionary_array );
            free( handle->file_path );
            free( handle );
            return NULL;
        }
        
        char *term = malloc( term_len + 1 );
        if ( fread( term, 1, term_len, file ) != term_len )
        {
            fprintf( stderr, "Ошибка чтения термина %lu\n", i );
            free( term );
            fclose( file );
            free_dict_hash_table( handle->hash_table );
            free( handle->dictionary_array );
            free( handle->file_path );
            free( handle );
            return NULL;
        }
        term[ term_len ] = '\0';
        
        uint64_t offset;
        uint32_t count;
        
        if ( fread( &offset, sizeof(uint64_t), 1, file ) != 1 )
        {
            fprintf( stderr, "Ошибка чтения offset для термина %lu\n", i );
            free( term );
            fclose( file );
            free_dict_hash_table( handle->hash_table );
            free( handle->dictionary_array );
            free( handle->file_path );
            free( handle );
            return NULL;
        }
        
        if ( fread( &count, sizeof(uint32_t), 1, file ) != 1 )
        {
            fprintf( stderr, "Ошибка чтения count для термина %lu\n", i );
            free( term );
            fclose( file );
            free_dict_hash_table( handle->hash_table );
            free( handle->dictionary_array );
            free( handle->file_path );
            free( handle );
            return NULL;
        }
        
        dict_hash_table_insert( handle->hash_table,
                                term,
                                offset, count );
        
        handle->dictionary_array[ i ].term = term;
        handle->dictionary_array[ i ].offset = offset;
        handle->dictionary_array[ i ].count = count;
    }
    
    fclose( file );
    handle->file_handle = NULL;
    
    printf( "Индекс загружен. Хэш-таблица содержит %u терминов\n",
            handle->hash_table->count );
    printf( "Коэффициент заполнения: %.2f\n",
            (float)handle->hash_table->count / handle->hash_table->size );
    
    return handle;
}

// Поиск термина и получение постлистов
DictionaryEntry *find_term( IndexHandle *handle,
                            const char *term )
{
    DictHashEntry *hash_entry = dict_hash_table_find( handle->hash_table, term );
    if ( !hash_entry )
        return NULL;
    
    static DictionaryEntry result;
    result.term = hash_entry->term;
    result.offset = hash_entry->offset;
    result.count = hash_entry->count;
    
    return &result;
}

uint32_t *get_postings( IndexHandle *handle,
                        const char *term,
                        uint32_t *count )
{
    DictHashEntry *entry = dict_hash_table_find( handle->hash_table, term );
    if ( !entry )
    {
        *count = 0;
        return NULL;
    }
    
    if ( !handle->file_handle )
    {
        handle->file_handle = fopen( handle->file_path, "rb" );
        if ( !handle->file_handle )
        {
            perror( "Ошибка открытия файла индекса" );
            return NULL;
        }
    }
    
    uint64_t position = handle->postings_start + entry->offset;
    fseek( handle->file_handle, (long)position, SEEK_SET );
    
    uint32_t *postings = malloc( entry->count * sizeof(uint32_t) );
    fread( postings, sizeof(uint32_t), entry->count, handle->file_handle );
    
    *count = entry->count;

    return postings;
}

// Освобождение ресурсов
void free_index( IndexHandle *handle )
{
    if ( !handle )
        return;
    
    if ( handle->file_handle )
        fclose( handle->file_handle );
    free( handle->file_path );
    if ( handle->hash_table )
        free_dict_hash_table( handle->hash_table );
    if ( handle->dictionary_array )
        free( handle->dictionary_array );
    free( handle );
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
// ФУНКЦИИ БУЛЕВА ПОИСКА

// Токенизация запроса
static
TokenList *tokenize_query( const char *query )
{
    TokenList *list = malloc( sizeof(TokenList) );
    list->capacity = 100;
    list->count = 0;
    list->current = 0;
    list->tokens = malloc( list->capacity * sizeof(Token) );
    
    const char *p = query;
    char buffer[ MAX_TERM_LEN ];
    int buf_idx = 0;
    
    while ( *p )
    {
        if ( isspace( *p ) )
        {
            p++;
            continue;
        }
        
        if ( *p == '(' )
        {
            if ( list->count >= list->capacity )
            {
                list->capacity *= 2;
                list->tokens = realloc( list->tokens, list->capacity * sizeof( Token ) );
            }
            list->tokens[ list->count ].type = TOKEN_LPAREN;
            list->tokens[ list->count ].value = NULL;
            list->count++;
            p++;
            continue;
        }
        
        if ( *p == ')' )
        {
            if ( list->count >= list->capacity )
            {
                list->capacity *= 2;
                list->tokens = realloc( list->tokens, list->capacity * sizeof( Token ) );
            }

            list->tokens[ list->count ].type = TOKEN_RPAREN;
            list->tokens[ list->count ].value = NULL;
            list->count++;
            p++;
            continue;
        }
        
        buf_idx = 0;
        while ( *p &&
                !isspace( *p ) &&
                *p != '(' &&
                *p != ')' )
        {
            if ( buf_idx < MAX_TERM_LEN - 1 )
                buffer[ buf_idx++ ] = *p;

            p++;
        }
        buffer[ buf_idx ] = '\0';
        
        if ( strcasecmp( buffer, "AND" ) == 0 )
        {
            if ( list->count >= list->capacity )
            {
                list->capacity *= 2;
                list->tokens = realloc( list->tokens, list->capacity * sizeof( Token ) );
            }
            list->tokens[ list->count ].type = TOKEN_AND;
            list->tokens[ list->count ].value = NULL;
            list->count++;
        } else if ( strcasecmp( buffer, "OR" ) == 0 )
        {
            if ( list->count >= list->capacity )
            {
                list->capacity *= 2;
                list->tokens = realloc( list->tokens, list->capacity * sizeof( Token ) );
            }
            list->tokens[ list->count ].type = TOKEN_OR;
            list->tokens[ list->count ].value = NULL;
            list->count++;
        } else
        {
            if ( list->count >= list->capacity )
            {
                list->capacity *= 2;
                list->tokens = realloc( list->tokens, list->capacity * sizeof( Token ) );
            }

            // Нормализация и стемминг токена
            wchar_t *token_wchar = utf8_to_wchar( buffer );
            normalize_text( token_wchar );
            stem_text( token_wchar );
            char *token_utf8 = wchar_to_utf8( token_wchar );

            //printf( "- %s\n", token_utf8 );

            list->tokens[ list->count ].type = TOKEN_TERM;
            list->tokens[ list->count ].value = strdup( token_utf8 );
            list->count++;

            free( token_wchar );
            free( token_utf8 );
        }
    }
    
    if ( list->count >= list->capacity )
    {
        list->capacity += 1;
        list->tokens = realloc( list->tokens, list->capacity * sizeof( Token ) );
    }
    list->tokens[ list->count ].type = TOKEN_EOF;
    list->tokens[ list->count ].value = NULL;
    list->count++;
    
    return list;
}

static
void free_token_list( TokenList *list )
{
    if ( !list )
        return;
    
    for ( int i = 0; i < list->count; i++ )
    {
        if ( list->tokens[ i ].value )
            free( list->tokens[ i ].value );
    }
    free( list->tokens );
    free( list );
}

// Алгоритм сортировочной станции (Shunting-yard)
static
TokenList *shunting_yard( TokenList *tokens )
{
    TokenList *output = malloc( sizeof(TokenList) );
    output->capacity = tokens->count;
    output->count = 0;
    output->current = 0;
    output->tokens = malloc( output->capacity * sizeof(Token) );
    
    Token * op_stack = malloc( tokens->count * sizeof(Token) );
    int op_top = -1;
    
    int get_priority( TokenType type )
    {
        switch ( type )
        {
            case TOKEN_AND:
                return 2;
            case TOKEN_OR:
                return 1;
            default:
                return 0;
        }
    }
    
    int balance = 0;
    
    for ( int i = 0; i < tokens->count; i++ )
    {
        Token token = tokens->tokens[ i ];
        
        switch ( token.type )
        {
            case TOKEN_TERM:
                output->tokens[ output->count++ ] = token;
                tokens->tokens[ i ].value = NULL;
                break;
                
            case TOKEN_LPAREN:
                balance++;
                op_stack[ ++op_top ] = token;
                break;
                
            case TOKEN_RPAREN:
                balance--;
                if ( balance < 0 )
                {
                    fprintf( stderr, "Ошибка: несбалансированные скобки\n" );
                    free( op_stack );
                    free_token_list( output );
                    return NULL;
                }
                
                while ( (op_top >= 0) &&
                        (op_stack[ op_top ].type != TOKEN_LPAREN) )
                    output->tokens[ output->count++ ] = op_stack[ op_top-- ];
                
                if ( (op_top >= 0) &&
                     (op_stack[ op_top ].type == TOKEN_LPAREN) )
                    op_top--;

                break;
                
            case TOKEN_AND:
            case TOKEN_OR:
                while ( (op_top >= 0) &&
                        (op_stack[ op_top ].type != TOKEN_LPAREN) &&
                        get_priority( op_stack[op_top ].type ) >= get_priority( token.type ) )
                    output->tokens[output->count++] = op_stack[op_top--];

                op_stack[ ++op_top ] = token;
                break;
                
            case TOKEN_EOF:
                break;
        }
    }
    
    if ( balance != 0 )
    {
        fprintf( stderr, "Ошибка: несбалансированные скобки\n" );
        free( op_stack );
        free_token_list( output );
        return NULL;
    }
    
    while ( op_top >= 0 )
    {
        if ( op_stack[ op_top ].type == TOKEN_LPAREN )
        {
            fprintf( stderr, "Ошибка: несбалансированные скобки\n" );
            free( op_stack );
            free_token_list( output );
            return NULL;
        }
        output->tokens[ output->count++ ] = op_stack[ op_top-- ];
    }
    
    output->tokens[ output->count ].type = TOKEN_EOF;
    output->tokens[ output->count ].value = NULL;
    output->count++;
    
    free( op_stack );

    return output;
}

// Операции над списками документов
static
uint32_t *intersect_lists( uint32_t *list1, uint32_t count1,
                           uint32_t *list2, uint32_t count2,
                           uint32_t *result_count )
{
    if ( count1 == 0 || count2 == 0 )
    {
        *result_count = 0;
        return NULL;
    }
    
    if ( count1 > count2 )
    {
        uint32_t *temp_list = list1;
        uint32_t temp_count = count1;
        list1 = list2;
        count1 = count2;
        list2 = temp_list;
        count2 = temp_count;
    }
    
    uint32_t *result = malloc( count1 * sizeof(uint32_t) );
    uint32_t i = 0, j = 0, k = 0;
    
    while ( i < count1 && j < count2 )
    {
        if ( list1[ i ] == list2[ j ] )
        {
            result[ k++ ] = list1[ i ];
            i++;
            j++;
        } else if ( list1[ i ] < list2[ j ] )
        {
            i++;
        } else
            j++;
    }
    
    *result_count = k;

    return result;
}

static
uint32_t *union_lists( uint32_t *list1, uint32_t count1,
                       uint32_t *list2, uint32_t count2,
                       uint32_t *result_count )
{
    uint32_t *result = malloc( (count1 + count2) * sizeof(uint32_t) );
    uint32_t i = 0, j = 0, k = 0;
    
    while ( i < count1 && j < count2 )
    {
        if ( list1[ i ] == list2[ j ] )
        {
            result[ k++ ] = list1[ i ];
            i++;
            j++;
        } else if ( list1[ i ] < list2[ j ] )
        {
            result[ k++ ] = list1[ i ];
            i++;
        } else
        {
            result[ k++ ] = list2[ j ];
            j++;
        }
    }
    
    while ( i < count1 )
        result[ k++ ] = list1[ i++ ];
    
    while ( j < count2 )
        result[ k++ ] = list2[ j++ ];
    
    *result_count = k;
    return result;
}

// Вычисление RPN выражения
static
uint32_t *evaluate_rpn( IndexHandle *handle,
                        TokenList *rpn,
                        uint32_t *result_count )
{
    struct {
        uint32_t **lists;
        uint32_t *counts;
        int top;
        int capacity;
    } stack;
    
    stack.capacity = rpn->count;
    stack.lists = malloc( stack.capacity * sizeof(uint32_t *) );
    stack.counts = malloc( stack.capacity * sizeof(uint32_t) );
    stack.top = -1;
    
    for ( int i = 0; i < rpn->count; i++ )
    {
        Token token = rpn->tokens[ i ];
        
        if ( token.type == TOKEN_TERM )
        {
            uint32_t count;
            uint32_t *postings = get_postings( handle, token.value, &count );
            
            stack.top++;
            stack.lists[ stack.top ] = postings;
            stack.counts[ stack.top ] = postings ? count : 0;
            
        } else if ( (token.type == TOKEN_AND) ||
                    (token.type == TOKEN_OR) )
        {
            if ( stack.top < 1 )
            {
                fprintf( stderr, "Ошибка: недостаточно операндов для оператора\n" );
                while ( stack.top >= 0 )
                {
                    if ( stack.lists[ stack.top ] )
                        free( stack.lists[stack.top] );
                    stack.top--;
                }
                free( stack.lists );
                free( stack.counts );
                *result_count = 0;
                return NULL;
            }
            
            uint32_t *list2 = stack.lists[ stack.top ];
            uint32_t count2 = stack.counts[ stack.top ];
            stack.top--;
            
            uint32_t *list1 = stack.lists[ stack.top ];
            uint32_t count1 = stack.counts[ stack.top ];
            stack.top--;
            
            uint32_t *result_list = NULL;
            uint32_t result_count_local = 0;
            
            if ( token.type == TOKEN_AND )
                result_list = intersect_lists( list1, count1,
                                               list2, count2,
                                               &result_count_local );
            else
                result_list = union_lists( list1, count1,
                                           list2, count2,
                                           &result_count_local );
            
            if ( list1 )
                free( list1 );
            if ( list2 )
                free( list2 );
            
            stack.top++;
            stack.lists[ stack.top ] = result_list;
            stack.counts[ stack.top ] = result_count_local;
            
        } else if ( token.type == TOKEN_EOF )
            break;
    }
    
    if ( stack.top != 0 )
    {
        fprintf( stderr, "Ошибка: некорректное выражение\n" );
        while ( stack.top >= 0 )
        {
            if ( stack.lists[stack.top] )
                free( stack.lists[stack.top] );
            stack.top--;
        }
        free( stack.lists );
        free( stack.counts );
        *result_count = 0;

        return NULL;
    }
    
    uint32_t *result = stack.lists[ 0 ];
    *result_count = stack.counts[ 0 ];
    
    free( stack.lists );
    free( stack.counts );
    
    return result;
}

// Основная функция булева поиска
uint32_t *boolean_search( IndexHandle *handle,
                          const char *query,
                          uint32_t *result_count )
{
    if ( !query ||
         strlen( query ) == 0 )
    {
        *result_count = 0;
        return NULL;
    }
    
    TokenList *tokens = tokenize_query( query );
    if ( !tokens )
    {
        *result_count = 0;
        return NULL;
    }
    
    TokenList *rpn = shunting_yard( tokens );
    free_token_list( tokens );
    
    if ( !rpn )
    {
        *result_count = 0;
        return NULL;
    }
    
    uint32_t * result = evaluate_rpn( handle, rpn, result_count );
    
    free_token_list( rpn );
    
    return result;
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
// ГЛАВНАЯ ФУНКЦИЯ
int main( int argc, char * argv[] )
{
    if ( argc < 2 )
    {
        printf( "Использование:\n" );
        printf( "  Построение индекса: %s <директория_с_файлами>\n", argv[ 0 ] );
        printf( "  Поиск по индексу: %s -search <термин>\n", argv[ 0 ] );
        printf( "  Булев поиск: %s -bool \"запрос\"\n", argv[ 0 ] );
        return 1;
    }
    
    if ( argc == 2 )
        build_index( argv[ 1 ] );
    else if ( (argc == 3) && strcmp( argv[ 1 ], "-search" ) == 0 )
    {
        IndexHandle *index = load_index( "final_index.bin" );
        if ( index )
        {
            const char *search_term = argv[ 2 ];
            uint32_t count;
            printf( "Поиск термина '%s'...\n", search_term );
            uint32_t *postings = get_postings( index, search_term, &count );
            
            if ( postings )
            {
                printf( "Термин '%s' найден в %u документах:\n", search_term, count );
                for ( uint32_t i = 0; i < ((count < 10) ? count : 10); i++ )
                    printf( "  Документ %u\n", postings[i] );

                if ( count > 10 )
                    printf( "  ... и еще %u документов\n", count - 10 );

                free( postings );
            } else
                printf( "Термин '%s' не найден\n", search_term );
            
            free_index( index );
        }
    } else if ( (argc == 3) && strcmp( argv[ 1 ], "-bool" ) == 0 )
    {
        IndexHandle * index = load_index( "final_index.bin" );
        if ( index )
        {
            const char *query = argv[ 2 ];
            printf( "Булев поиск: '%s'\n", query );
            uint32_t count;
            uint32_t *result = boolean_search( index, query, &count );
            
            if ( result && count > 0 )
            {
                printf( "Найдено %u документов:\n", count );
                for ( uint32_t i = 0; i < ((count < 10) ? count : 10); i++ )
                {
                    printf( "  Документ %u\n", result[ i ] );
                }

                if ( count > 10 )
                    printf( "  ... и еще %u документов\n", count - 10 );
                
                    free( result );
            } else
                printf( "По запросу ничего не найдено\n" );
            
            free_index( index );
        }
    } else
    {
        printf( "Неверные аргументы\n" );
        return 1;
    }
    
    return 0;
}