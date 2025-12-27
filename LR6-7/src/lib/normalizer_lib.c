#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>
#include <errno.h>
#include "../../include/normalizer_lib.h"


static
bool is_russian_letter( wchar_t c )
{
    if ( (c >= L'а' && c <= L'я') || 
         (c >= L'А' && c <= L'Я') || 
         (c == L'ё' || c == L'Ё') )
        return true;

    return false;
}

static
bool is_english_letter( wchar_t c )
{
    if ( (c >= L'a' && c <= L'z') || 
         (c >= L'A' && c <= L'Z') )
        return true;

    return false;
}

static
void normalize_text_direct( const wchar_t *input_buffer,
                            wchar_t *output_buffer )
{
    if ( !input_buffer || !output_buffer )
        return;
    
    size_t len = wcslen(input_buffer);
    bool in_word = false;
    wchar_t* dest = output_buffer;
    
    for ( size_t i = 0; i < len; i++ )
    {
        wchar_t cur_char = input_buffer[ i ];
        
        bool is_letter = false;
        if ( is_russian_letter(cur_char) ||
             is_english_letter(cur_char) ||
             (cur_char >= L'0' && cur_char <= L'9') ||
             (cur_char == L'-') )
            is_letter = true;

        if ( is_letter )
        {
            if ( !in_word )
                in_word = true;
            
            if ( is_russian_letter( cur_char ) )
            {
                if (cur_char >= L'А' && cur_char <= L'Я')
                    cur_char = L'а' + (cur_char - L'А');
                else if ( (cur_char == L'Ё') ||
                          (cur_char == L'ё') )
                    cur_char = L'е';
            } else if ( is_english_letter( cur_char ) )
                cur_char = L'a' + (cur_char - L'A');
            
            *dest++ = cur_char;
        } else
        {
            if ( in_word )
            {
                *dest++ = L' ';
                in_word = false;
            }
        }
    }
    
    if ( in_word )
        *dest++ = L' ';
    
    if ( (dest > output_buffer) && 
         (*(dest - 1) == L' ') )
        dest--;
    
    *dest = L'\0';
}

void normalize_text( wchar_t *text )
{
    if ( !text || (wcslen(text) == 0) )
        return;

    wchar_t *normalized_text = (wchar_t*)malloc( (wcslen(text) * 2 + 1) * sizeof(wchar_t) );
    if (normalized_text == NULL)
    {
        fprintf(stderr, "Memmory alloc error\n");
        return;
    }
    
    normalize_text_direct(text, normalized_text);
    wcscpy(text, normalized_text);
    free(normalized_text);
}