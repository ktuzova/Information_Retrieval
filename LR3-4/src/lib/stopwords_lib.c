#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <locale.h>
#include <wchar.h>
#include <errno.h>
#include "../../include/stopwords_lib.h"

#define MAX_WORD_LEN 256


static const wchar_t* RUSSIAN_STOPWORDS[] =
{
    L"и", L"в", L"во", L"не", L"что", L"он", L"на", L"я", L"с", L"со",
    L"как", L"а", L"то", L"все", L"она", L"так", L"его", L"но", L"да",
    L"ты", L"к", L"у", L"же", L"вы", L"за", L"бы", L"по", L"только",
    L"ее", L"мне", L"было", L"вот", L"от", L"меня", L"еще", L" нет",
    L"о", L"из", L"ему", L"теперь", L"когда", L"даже", L"ну", L"ли",
    L"если", L"уже", L"или", L"ни", L"быть", L"был", L"него", L"до",
    L"вас", L"нибудь", L"опять", L"уж", L"вам", L"ведь", L"там", L"потом",
    L"себя", L"ничего", L"ей", L"может", L"они", L"тут", L"где", L"есть",
    L"надо", L"ней", L"для", L"мы", L"тебя", L"их", L"чем", L"была",
    L"сам", L"чтоб", L"без", L"будто", L"чего", L"раз", L"тоже", L"себе",
    L"под", L"будет", L"ж", L"тогда", L"кто", L"этот", L"того", L"потому",
    L"этого", L"какой", L"совсем", L"ним", L"здесь", L"этом", L"один",
    L"почти", L"мой", L"тем", L"чтобы", L"нее", L"сейчас", L"были",
    L"куда", L"зачем", L"всех", L"никогда", L"можно", L"при", L"наконец",
    L"два", L"об", L"другой", L"хоть", L"после", L"над", L"больше",
    L"тот", L"через", L"эти", L"нас", L"про", L"всего", L"них", L"какая",
    L"много", L"разве", L"три", L"эту", L"моя", L"впрочем", L"хорошо",
    L"свою", L"этой", L"перед", L"иногда", L"лучше", L"чуть", L"том",
    L"нельзя", L"такой", L"им", L"более", L"всегда", L"конечно", L"всю",
    L"между"
};

static const int RUSSIAN_STOPWORDS_COUNT = sizeof(RUSSIAN_STOPWORDS) / sizeof(RUSSIAN_STOPWORDS[0]);


static
bool is_word_char( wchar_t c )
{
    if ( (c >= L'а' && c <= L'я') || 
         (c >= L'А' && c <= L'Я') || 
         (c == L'ё' || c == L'Ё') ||
         (c >= L'a' && c <= L'z') || 
         (c >= L'A' && c <= L'Z') )
        return true;

    return false;
}

static
bool is_stopword( const wchar_t *word )
{
    if ( !word || wcslen(word) == 0 )
        return false;
    
    for ( int i = 0; i < RUSSIAN_STOPWORDS_COUNT; i++ )
    {
        if ( wcscmp( word, RUSSIAN_STOPWORDS[i] ) == 0 )
            return true;
    }
    
    return false;
}

static
void remove_stopwords_direct( const wchar_t *input_buffer,
                              wchar_t *output_buffer )
{
    if ( !input_buffer || !output_buffer )
        return;
    
    size_t input_buffer_len = wcslen( input_buffer );
    if ( input_buffer_len == 0 )
    {
        output_buffer[ 0 ] = L'\0';
        return;
    }
    
    size_t result_pos = 0;
    size_t word_start = 0;
    bool in_word = false;
    
    for ( size_t i = 0; i <= input_buffer_len; i++ )
    {
        wchar_t cur_char = input_buffer[ i ];
        
        if ( is_word_char( cur_char ) &&
             (cur_char != L'\0') )
        {
            if ( !in_word )
            {
                in_word = true;
                word_start = i;
            }
        } else
        {
            if ( in_word )
            {
                size_t word_len = i - word_start;
                
                if ( word_len < MAX_WORD_LEN )
                {
                    wchar_t word[ MAX_WORD_LEN ];
                    wcsncpy( word, &input_buffer[ word_start ], word_len );
                    word[ word_len ] = L'\0';

                    if ( !is_stopword( word ) )
                    {
                        for ( size_t j = 0; j < word_len; j++ )
                            output_buffer[ result_pos++ ] = input_buffer[ word_start + j ];

                        output_buffer[ result_pos++ ] = L' ';
                    }
                }
                
                in_word = false;
            }
            
            if ( cur_char == L'\n' || cur_char == L'\t' ||
                 cur_char == L'\r' || cur_char == L' '  ||
                 cur_char == L','  || cur_char == L'.'  ||
                 cur_char == L'!'  || cur_char == L'?'  ||
                 cur_char == L';'  || cur_char == L':'  ||
                 cur_char == L'('  || cur_char == L')'  ||
                 cur_char == L'['  || cur_char == L']'  ||
                 cur_char == L'{'  || cur_char == L'}' )
            {
                if ( (result_pos > 0) &&
                     (output_buffer[ result_pos - 1 ] == L' ') )
                    result_pos--;
                output_buffer[ result_pos++ ] = cur_char;
            } else if ( cur_char == L'\0' )
                break;
        }
    }
    
    if ( (result_pos > 0) &&
         (output_buffer[ result_pos - 1 ] == L' ') )
        result_pos--;
    
    output_buffer[ result_pos ] = L'\0';
}

void remove_stopwords( wchar_t *text )
{
    if ( !text || ( wcslen( text ) == 0 ) )
        return;
  
    size_t len = wcslen( text );
    wchar_t *processed_text = (wchar_t*)malloc((len * 2 + 1) * sizeof(wchar_t));
    if ( processed_text == NULL )
    {
        fprintf( stderr, "Memmory alloc error\n" );
        return;
    }
    
    remove_stopwords_direct( text, processed_text );
    wcscpy( text, processed_text );
    free( processed_text );
}