#ifndef _COMMON_LOGGING_H
#define _COMMON_LOGGING_H

enum {
	TRACE = 0,
	DEBUG,
	INFO,
	WARN,
	ERROR,
	FATAL
};
void logtest();
void logging(int level, const char *format, ...);

#endif /*_COMMON_LOGGING_H*/