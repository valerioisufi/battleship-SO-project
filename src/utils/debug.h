// Colori ANSI per log
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define LOG_INFO(fmt, ...) \
    printf(ANSI_COLOR_GREEN "[INFO] " fmt ANSI_COLOR_RESET "\n", ##__VA_ARGS__)

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, ANSI_COLOR_RED "[ERROR] %s:%d " fmt ANSI_COLOR_RESET "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#ifdef DEBUG
#define LOG_DEBUG(fmt, ...) \
    printf(ANSI_COLOR_BLUE "[DEBUG] %s:%d " fmt ANSI_COLOR_RESET "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) ((void)0)
#endif