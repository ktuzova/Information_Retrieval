#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <dirent.h>
#include <locale.h>
#include <wchar.h>
#include <errno.h>
#include "../../include/stemmer_lib.h"

#define MAX_WORD_LENGTH 100
#define MAX_LINE_LEN 65536


static
bool is_russian_letter( wchar_t c )
{
    if (c >= L'а' && c <= L'я')
        return true;

    return false;
}

static
bool is_russian_word( const wchar_t *word )
{
    if ( (word == NULL) ||
         (wcslen(word) == 0) )
        return false;
    
    for ( size_t i = 0; i < wcslen( word ); i++ )
    {
        if ( !is_russian_letter( word[ i ] ) )
            return false;
    }
    return true;
}

static
int is_vowel( wchar_t c )
{
    return (wcschr( L"аеиоуыэюяё", c ) != NULL);
}

static
int is_consonant( wchar_t c )
{
    return (wcschr( L"бвгджзйклмнпрстфхцчшщ", c ) != NULL);
}

static
void find_regions( const wchar_t *word,
                   size_t *r1,
                   size_t *r2,
                   size_t *rv )
{
    size_t len = wcslen( word );
    size_t i;
    
    // Находим RV (регион после первой гласной)
    *rv = len;
    for ( i = 0; i < len; i++ )
    {
        if ( is_vowel( word[ i ] ) )
        {
            *rv = i + 1;
            break;
        }
    }
    
    // Находим R1 (после первой гласной после согласной)
    *r1 = len;
    for ( i = 0; i < len - 1; i++ )
    {
        if ( is_consonant( word[ i ] ) &&
             is_vowel( word[ i + 1 ] ) )
        {
            *r1 = i + 2;
            break;
        }
    }
    
    // Находим R2 (после первой гласной в R1)
    *r2 = len;
    for ( i = *r1; i < len - 1; i++ )
    {
        if ( is_consonant( word[ i ] ) &&
             is_vowel( word[ i + 1 ] ))
        {
            *r2 = i + 2;
            break;
        }
    }
}

static
void remove_ending( wchar_t *word,
                    const wchar_t *ending )
{
    int word_len = wcslen( word );
    int end_len = wcslen( ending );
    
    if ( (word_len >= end_len) && 
         (wcscmp(word + word_len - end_len, ending) == 0) )
        word[word_len - end_len] = L'\0';
}

static
int has_ending( const wchar_t *word,
                const wchar_t *ending )
{
    int word_len = wcslen(word);
    int end_len = wcslen(ending);
    
    return ( (word_len >= end_len) && 
             (wcscmp(word + word_len - end_len, ending) == 0) );
}

static
void russian_stemmer( wchar_t *word )
{
    if ( !is_russian_word( word ) )
        return;
    
    if ( wcslen( word ) < 3 )
        return;
    
    size_t r1, r2, rv;
    find_regions( word, &r1, &r2, &rv );
    size_t len = wcslen(word);
    
    // Шаг 1: Perfective gerund
    if ( has_ending( word, L"ивши" ) ||
         has_ending( word, L"ывши" ) || 
         has_ending( word, L"ив" ) ||
         has_ending( word, L"ыв" ))
    {
        if ( len - 2 >= rv )
        {
            remove_ending( word, L"ивши" );
            remove_ending( word, L"ывши" );
            remove_ending( word, L"ив" );
            remove_ending( word, L"ыв" );
        }
    } else if ( has_ending( word, L"вши" ) ||
                has_ending( word, L"в" ) )
    {
        if ( (len - 1 >= rv) &&
             (has_ending( word, L"вши" ) || has_ending( word, L"в" )) )
        {
            remove_ending( word, L"вши" );
            remove_ending( word, L"в" );
        }
    }
    
    // Шаг 2: Adjective
    const wchar_t *adjective_endings[] = {
        L"ее", L"ие", L"ые", L"ое", L"ими", L"ыми", L"ей", L"ий",
        L"ый", L"ой", L"ем", L"им", L"ым", L"ом", L"его", L"ого",
        L"ему", L"ому", L"их", L"ых", L"ую", L"юю", L"ая", L"яя",
        L"ою", L"ею", NULL
    };
    
    for ( int i = 0; adjective_endings[i] != NULL; i++ )
    {
        if ( has_ending( word, adjective_endings[ i ] ) )
        {
            if ( len - wcslen( adjective_endings[ i ] ) >= r1 )
                remove_ending( word, adjective_endings[ i ] );

            break;
        }
    }
    
    // Шаг 3: Participle
    if ( has_ending( word, L"ивш" ) ||
         has_ending( word, L"ывш" ) || 
         has_ending( word, L"ующ" ) )
    {
        if ( len - 3 >= rv )
        {
            remove_ending( word, L"ивш" );
            remove_ending( word, L"ывш" );
            remove_ending( word, L"ующ" );
        }
    } else if ( has_ending( word, L"ем" ) ||
                has_ending( word, L"нн" ) || 
                has_ending( word, L"вш" ) ||
                has_ending( word, L"ющ" ) || 
                has_ending( word, L"щ" ) )
    {
        if ( len - 2 >= rv )
        {
            remove_ending( word, L"ем" );
            remove_ending( word, L"нн" );
            remove_ending( word, L"вш" );
            remove_ending( word, L"ющ" );
            remove_ending( word, L"щ" );
        }
    }
    
    // Шаг 4: Reflexive
    if ( has_ending( word, L"ся" ) ||
         has_ending( word, L"сь" ) )
    {
        if ( len - 2 >= rv )
        {
            remove_ending( word, L"ся" );
            remove_ending( word, L"сь" );
        }
    }
    
    // Шаг 5: Verb
    const wchar_t *verb_endings[] = {
        L"ила", L"ыла", L"ена", L"ейте", L"уйте", L"ите", L"или",
        L"ыли", L"ей", L"уй", L"ил", L"ыл", L"им", L"ым", L"ен",
        L"ило", L"ыло", L"ено", L"ят", L"ует", L"уют", L"ит", L"ыт",
        L"ют", L"ны", L"ть", L"ешь", L"нно", NULL
    };
    
    for ( int i = 0; verb_endings[ i ] != NULL; i++ )
    {
        if ( has_ending( word, verb_endings[ i ] ) )
        {
            if ( len - wcslen( verb_endings[ i ] ) >= rv )
                remove_ending( word, verb_endings[ i ] );

            break;
        }
    }
    
    // Шаг 6: Noun
    const wchar_t *noun_endings[] = {
        L"а", L"ев", L"ов", L"ие", L"ье", L"е", L"иями", L"ями",
        L"ами", L"еи", L"ии", L"и", L"ией", L"ей", L"ой", L"ий",
        L"й", L"иям", L"ям", L"ием", L"ем", L"ам", L"ом", L"о",
        L"у", L"ах", L"иях", L"ях", L"ы", L"ь", L"ию", L"ью",
        L"ю", L"ия", L"ья", L"я", NULL
    };
    
    for ( int i = 0; noun_endings[ i ] != NULL; i++ )
    {
        if ( has_ending( word, noun_endings[ i ] ) )
        {
            if ( len - wcslen( noun_endings[ i ] ) >= r1 )
                remove_ending( word, noun_endings[ i ] );

            break;
        }
    }
    
    // Шаг 7: Superlative
    if ( has_ending( word, L"ейш" ) ||
         has_ending( word, L"ейше" ) )
    {
        if ( len - 4 >= r2 )
        {
            remove_ending( word, L"ейш" );
            remove_ending( word, L"ейше" );
        }
    }
    
    // Шаг 8: Undouble н
    if ( has_ending( word, L"нн" ) )
    {
        if ( len - 2 >= r2 )
        {
            remove_ending( word, L"нн" );
            word[ len - 1 ] = L'\0';
        }
    }
    
    // Шаг 9: Soft sign
    if ( has_ending( word, L"ь" ) )
    {
        if ( len - 1 >= r2 )
            remove_ending( word, L"ь" );
    }
}

void stem_text( wchar_t *line )
{
    if ( !line || wcslen( line ) == 0 )
        return;

    if ( is_russian_word( line ) == false )
        return;
    
    wchar_t result[ MAX_LINE_LEN ] = L"";
    
    wchar_t line_copy[ MAX_LINE_LEN ];
    wcsncpy( line_copy, line, MAX_LINE_LEN - 1 );
    line_copy[ MAX_LINE_LEN - 1 ] = L'\0';
    
    wchar_t *saveptr;
    wchar_t *token = wcstok( line_copy, L" \t\n", &saveptr );
    int first = 1;
    
    while ( token != NULL )
    {
        wchar_t word[MAX_WORD_LENGTH];
        wcsncpy(word, token, MAX_WORD_LENGTH - 1);
        word[MAX_WORD_LENGTH - 1] = L'\0';
        
        if ( is_russian_word( word ) )
            russian_stemmer( word );
        
        if ( !first )
            wcscat( result, L" " );

        wcscat( result, word );
        first = 0;
        
        token = wcstok( NULL, L" \t\n", &saveptr );
    }
    
    wcscpy( line, result );
}