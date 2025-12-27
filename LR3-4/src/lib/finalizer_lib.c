#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <locale.h>
#include <wchar.h>
#include <errno.h>
#include "../../include/finalizer_lib.h"


static
int is_digit_char( wchar_t c )
{
    if (c >= L'0' && c <= L'9')
        return true;

    return false;
}

static
bool is_word_char( wchar_t c )
{
    if ( (c >= L'а' && c <= L'я') ||
         (c >= L'a' && c <= L'z') )
        return true;

    return false;
}

void remove_single_letter_words( wchar_t *text )
{
    if ( !text )
        return;
    
    size_t len = wcslen( text );
    if ( len == 0 )
        return;
    
    wchar_t* result = (wchar_t*)malloc( (len + 1) * sizeof(wchar_t) );
    if ( !result )
    {
        fprintf( stderr, "Memmroy alloc error\n" );
        return;
    }
    
    size_t result_pos = 0;
    size_t word_start = 0;
    int in_word = 0;
    
    for ( size_t i = 0; i <= len; i++ )
    {
        wchar_t c = text[ i ];
        
        if ( is_word_char(c) &&
             (c != L'\0') )
        {
            if ( !in_word )
            {
                in_word = 1;
                word_start = i;
            }
        } else
        {
            if ( in_word )
            {
                size_t word_len = i - word_start;
                wchar_t first_char = text[ word_start ];
                
                if ( (word_len >= 2) || is_digit_char(first_char) )
                {
                    for ( size_t j = 0; j < word_len; j++ )
                    {
                        result[ result_pos++ ] = text[ word_start + j ];
                    }
                }
                in_word = 0;
            }
            
            if ( c != L'\0' )
                result[ result_pos++ ] = c;
        }
    }
    
    result[ result_pos ] = L'\0';
    wcscpy( text, result );
    free( result );
}