#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>
#include "../../include/statistics_lib.h"

void collect_statistics( const wchar_t *text, size_t input_bytes, 
                         double processing_time, TextStatistics *stats )
{
    if (!text || !stats)
        return;
    
    stats->token_count = 0;
    stats->total_token_length = 0;
    stats->input_size_bytes = input_bytes;
    stats->processing_time_sec = processing_time;
    
    const wchar_t *ptr = text;
    while ( *ptr )
    {
        while ( *ptr == L' ' )
            ptr++;
        if ( *ptr == L'\0' )
            break;
        
        const wchar_t *token_start = ptr;
        while ( *ptr && *ptr != L' ' )
            ptr++;
        
        size_t token_len = ptr - token_start;
        stats->token_count++;
        stats->total_token_length += token_len;
    }
}

void print_summary_statistics(const TextStatistics *total_stats, int file_count)
{
    printf( "Обработано файлов: %d\n", file_count);
    printf( "Общий объем данных: %.2f КБ (%.2f МБ)\n", 
            total_stats->input_size_bytes / 1024.0,
            total_stats->input_size_bytes / (1024.0 * 1024.0) );
    printf( "Суммарное время обработки (только алгоритмы): %.4f секунд\n",
            total_stats->processing_time_sec );
    printf( "Общее количество токенов: %zu\n", total_stats->token_count );
    
    if ( total_stats->token_count > 0 )
    {
        double overall_avg = (double)total_stats->total_token_length / total_stats->token_count;
        printf( "Средняя длина токена (общая): %.2f символов\n", overall_avg );
    }
    
    if ( total_stats->processing_time_sec > 0 )
    {
        double overall_speed = (total_stats->input_size_bytes / 1024.0) / total_stats->processing_time_sec;
        double overall_tokens_speed = total_stats->token_count / total_stats->processing_time_sec;
        printf( "Средняя скорость обработки: %.2f КБ/сек (%.2f МБ/сек)\n", 
                overall_speed, overall_speed / 1024.0 );
        printf( "Средняя скорость токенизации: %.2f токенов/сек\n", overall_tokens_speed );
    }
}