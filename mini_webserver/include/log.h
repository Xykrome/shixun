#ifndef LOG_H
#define LOG_H

int log_init(const char *path);
void log_info(const char *message);
void log_error(const char *message);
void log_close(void);

#endif