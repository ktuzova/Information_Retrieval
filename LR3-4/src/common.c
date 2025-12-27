#include "../include/common.h"

char* read_utf8_file( const char *filename )
{
    FILE* file = fopen( filename, "rb" );
    if ( file == NULL )
    {
        fprintf(stderr, "Не могу открыть файл %s: %s\n", filename, strerror(errno));
        return NULL;
    }
    
    fseek( file, 0, SEEK_END );
    long file_size = ftell( file );
    fseek( file, 0, SEEK_SET );
    
    if ( file_size <= 0 )
    {
        fclose( file );

        char *empty = malloc( 1 * sizeof( char ) );
        empty[ 0 ] = '\0';
        return empty;
    }
    
    char* utf8_str = (char*)malloc( file_size + 1 );
    if ( utf8_str == NULL )
    {
        fclose( file );
        fprintf( stderr, "Не могу выделить память для файла %s\n", filename );
        return NULL;
    }
    
    size_t read_bytes = fread( utf8_str, 1, file_size, file );
    utf8_str[ read_bytes ] = '\0';
    fclose( file );
    
    return utf8_str;
}

wchar_t* utf8_to_wchar( const char *utf8_str )
{
    if ( utf8_str == NULL )
        return NULL;
    
    iconv_t conversion_descriptor = iconv_open( "WCHAR_T", "UTF-8" );
    if ( conversion_descriptor == (iconv_t)-1 )
    {
        conversion_descriptor = iconv_open( "wchar_t", "UTF-8" );
        if ( conversion_descriptor == (iconv_t)-1 )
        {
            perror( "iconv_open failed" );
            return NULL;
        }
    }
    
    size_t utf8_str_len = strlen( utf8_str );
    size_t out_len = (utf8_str_len + 1) * sizeof( wchar_t ) * 2;
    
    wchar_t* wchar_str = (wchar_t*)malloc( out_len );
    if ( wchar_str == NULL ) {
        iconv_close( conversion_descriptor );
        fprintf( stderr, "Не могу выделить память для широкой строки\n" );
        return NULL;
    }
    
    char* in_ptr = (char*)utf8_str;
    char* out_ptr = (char*)wchar_str;
    size_t in_bytes_left = utf8_str_len;
    size_t out_bytes_left = out_len;
    
    size_t result = iconv( conversion_descriptor,
                           &in_ptr, &in_bytes_left,
                           &out_ptr, &out_bytes_left );
    if ( result == (size_t)-1 )
        perror( "iconv warning" );
    
    iconv_close( conversion_descriptor );
    
    size_t bytes_written = out_len - out_bytes_left;
    if ( bytes_written >= sizeof( wchar_t ) )
        wchar_str[ bytes_written / sizeof(wchar_t) - 1 ] = L'\0';
    else
        wchar_str[ 0 ] = L'\0';
    
    return wchar_str;
}

char* wchar_to_utf8( const wchar_t *wchar_str )
{
    if ( wchar_str == NULL )
        return NULL;

    iconv_t conversion_descriptor = iconv_open( "UTF-8", "WCHAR_T" );
    if ( conversion_descriptor == (iconv_t)-1 )
    {
        conversion_descriptor = iconv_open( "UTF-8", "wchar_t" );
        if ( conversion_descriptor == (iconv_t)-1 )
        {
            perror( "iconv_open failed" );
            return NULL;
        }
    }

    size_t in_len = wcslen( wchar_str ) * sizeof(wchar_t);
    size_t out_len = ( wcslen( wchar_str ) * 4 ) + 1;

    char* utf8_str = (char*)malloc( out_len );
    if ( utf8_str == NULL )
    {
        iconv_close( conversion_descriptor );
        fprintf( stderr, "Не могу выделить память для UTF-8 строки\n" );
        return NULL;
    }

    char* in_ptr = (char*)wchar_str;
    char* out_ptr = utf8_str;
    size_t in_bytes_left = in_len;
    size_t out_bytes_left = out_len;

    size_t result = iconv( conversion_descriptor,
                           &in_ptr, &in_bytes_left,
                           &out_ptr, &out_bytes_left );
    
    if ( result == (size_t)-1 )
    {
        perror( "iconv failed" );
        free( utf8_str );
        iconv_close( conversion_descriptor );
        return NULL;
    }

    iconv_close( conversion_descriptor );

    utf8_str[ out_len - out_bytes_left ] = '\0';
    return utf8_str;
}

int write_wstring_to_file( const wchar_t *wchar_str,
                           FILE *file )
{
    if ( wchar_str == NULL || file == NULL )
        return 0;
    
    char* utf8_str = wchar_to_utf8( wchar_str );
    if ( utf8_str == NULL )
        return 0;
    
    size_t len = strlen( utf8_str );
    size_t written = fwrite( utf8_str, 1, len, file );
    
    free( utf8_str );
    return (written == len) ? 1 : 0;
}

void create_directory( const char *path )
{
    struct stat file_state = { 0 };
    if ( stat( path, &file_state ) == -1 )
    {
        if ( mkdir( path, 0777 ) != 0 )
            fprintf( stderr, "Ошибка создания директории: %s: %s\n", path, strerror(errno) );
        else
            printf( "Создана директория: %s\n", path );
    }
}

int is_valid_filename( const char *filename )
{
    // Filename is valid if it's name is "number.txt" (ex: 451435.txt)
    const char *dot = strchr( filename, '.' );
    if ( dot == NULL )
        return 0;
    
    if ( strcmp( dot, ".txt" ) != 0 )
        return 0;
    
    for (const char *p = filename; p < dot; p++)
    {
        if ( isdigit((unsigned char)*p) == 0 )
            return 0;
    }
    
    return 1;
}
