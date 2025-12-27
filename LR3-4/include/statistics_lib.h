#ifndef STATISTICS_H
#define STATISTICS_H

#include <wchar.h>

typedef struct {
    size_t token_count;
    size_t total_token_length;
    size_t input_size_bytes;
    double processing_time_sec;
} TextStatistics;

void collect_statistics( const wchar_t *text, size_t input_bytes, 
                         double processing_time, TextStatistics *stats);

void print_summary_statistics( const TextStatistics *total_stats, int file_count );

#endif // STATISTICS_H