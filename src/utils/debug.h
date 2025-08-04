#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// Colori ANSI per log
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Macro base per log
#define LOG_BASE(level, color, stream, fmt, ...) fprintf(stream, color "[" level "] " ANSI_COLOR_RESET fmt "\n", ##__VA_ARGS__)
#define LOG_BASE_TAG(level, color, stream, tag, fmt, ...) fprintf(stream, color "[" level "][%s] " ANSI_COLOR_RESET fmt "\n", tag, ##__VA_ARGS__)

// Macro di livello superiore senza tag
#define LOG_INFO(fmt, ...) LOG_BASE("INFO", ANSI_COLOR_GREEN, stdout, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) LOG_BASE("WARNING", ANSI_COLOR_YELLOW, stderr, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_BASE("ERROR", ANSI_COLOR_RED, stderr, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_MSG_ERROR(fmt, ...) LOG_BASE("MSG ERROR", ANSI_COLOR_MAGENTA, stderr, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

// Macro di livello superiore con tag (deve essere definito LOG_TAG)
#define LOG_INFO_TAG(fmt, ...) LOG_BASE_TAG("INFO", ANSI_COLOR_GREEN, stdout, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_WARNING_TAG(fmt, ...) LOG_BASE_TAG("WARNING", ANSI_COLOR_YELLOW, stderr, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR_TAG(fmt, ...) LOG_BASE_TAG("ERROR", ANSI_COLOR_RED, stderr, LOG_TAG, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_MSG_ERROR_TAG(fmt, ...) LOG_BASE_TAG("MSG ERROR", ANSI_COLOR_MAGENTA, stderr, LOG_TAG, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

// Debug solo se DEBUG è definito
#ifdef DEBUG
#define LOG_DEBUG(fmt, ...) \
    LOG_BASE("DEBUG", ANSI_COLOR_BLUE, stdout, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#define LOG_DEBUG_TAG(fmt, ...) \
    LOG_BASE_TAG("DEBUG", ANSI_COLOR_BLUE, stdout, LOG_TAG, "%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#define LOG_DEBUG_TAG(fmt, ...) ((void)0)
#endif

#endif // DEBUG_H