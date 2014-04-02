#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <common/logging.h>

static FILE *log_file = NULL;

void logtest()
{
}

void logging(int level, const char *format, ...) 
{
	if (level == TRACE) {
	//	return;
	}
	va_list argp;
	char dataptr[1024];
	int data_len;

	if (level < 0 || format == NULL)
		return;

	va_start(argp, format);
	data_len += vsnprintf(dataptr, sizeof(dataptr) - 1 - data_len, format, argp);
	va_end(argp);
	if (!log_file) {
		log_file = freopen("../log.txt","w",stderr);
	}
	switch(level) {
	case TRACE:
		fprintf(log_file, "[TRACE]%s\n", dataptr);
		break;
	case DEBUG:
		fprintf(log_file, "[DEBUG]%s\n", dataptr);
		break;
	case INFO:
		fprintf(log_file, "[INFO]%s\n", dataptr);
		break;
	case WARN:
		fprintf(log_file, "[WARN]%s\n", dataptr);
		break;
	case ERROR:
		fprintf(log_file, "[ERROR]%s\n", dataptr);
		break;
	case FATAL:
		fprintf(log_file, "[FATAL]%s\n", dataptr);
		break;

	}
	//setvbuf(log_file, dataptr, _IOFBF, data_len);
	
	fflush(log_file);
	//fprintf(stderr, "%s\n", dataptr);

}
