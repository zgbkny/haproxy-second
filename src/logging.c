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
	//setvbuf(log_file, dataptr, _IOFBF, data_len);
	fprintf(log_file, "%s\n", dataptr);
	fflush(log_file);
	//fprintf(stderr, "%s\n", dataptr);

}
