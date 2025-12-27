#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include "../../include/tokenizer_lib.h"


static
bool is_delimiter( wchar_t c )
{
    if ( iswspace( c ) || iswpunct( c ) )
        return 1;

    switch ( c )
    {
        case L'«': case L'»': case L'„': case L'“':
        case L'”': case L'‹': case L'›': case L'「':
        case L'」': case L'—': case L'–': case L'…':
            return 1;
        default:
            return 0;
    }
}

static
void tokenize_text_direct( const wchar_t *input_buffer,
                           wchar_t *output_buffer )
{
    if ( !input_buffer || !output_buffer )
        return;
    
    size_t input_buffer_len = wcslen( input_buffer );
    bool in_token = false;
    bool is_first_token = true;
    size_t tokenized_text_len = 0;
    
    for ( size_t i = 0; i < input_buffer_len; i++ )
    {
        wchar_t cur_char = input_buffer[ i ];
        
        if ( is_delimiter( cur_char ) )
        {
            if ( in_token )
                in_token = 0;
        } else
        {
            if ( (in_token == false) && (is_first_token == false) )
                output_buffer[ tokenized_text_len++ ] = L' ';
            
            output_buffer[ tokenized_text_len++ ] = cur_char;
            in_token = true;
            
            if ( is_first_token )
                is_first_token = false;
        }
    }
    
    output_buffer[ tokenized_text_len ] = L'\0';
}

void tokenize_text( wchar_t *text )
{
    if ( !text || (wcslen( text ) == 0) )
        return;
    
    wchar_t *tokenized_text = (wchar_t*)malloc( (wcslen( text ) * 2 + 1) * sizeof(wchar_t) );
    if ( tokenized_text == NULL )
    {
        fprintf( stderr, "Memmory alloc error\n" );
        return;
    }
    
    tokenize_text_direct( text, tokenized_text );
    wcscpy( text, tokenized_text );
    free( tokenized_text );
}