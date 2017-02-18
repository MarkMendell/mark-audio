#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef int16_t sample;
unsigned int BUFLEN = 4096;


void
die(char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

size_t
freadordie(sample *buf, size_t count)
{
	if (feof(stdin))
		exit(EXIT_SUCCESS);
	size_t read = fread(buf, sizeof(sample), count, stdin);
	if (ferror(stdin))
		die("snip: fread: %s", strerror(errno));
	return read;
}

int
main(int argc, char **argv)
{
	int channels, start;
	if ((argc < 3) || (argc > 4))
		die("usage: snip channels start [end]");
	char *endptr;
	if (((channels = strtol(argv[1], &endptr, 10)) <= 0) || (endptr == argv[1]))
		die("snip: channels must be positive integer");
	if (((start = strtol(argv[2], &endptr, 10)) < 0) || (endptr == argv[2]))
		die("snip: start must be nonnegative integer");
	int end = -1;
	if ((argc == 4) &&
			(((end = strtol(argv[3], &endptr, 10)) <= start) || (endptr == argv[3])))
		die("snip: end must be integer after start");

	unsigned int buflen = BUFLEN-1;
	while (++buflen % channels)
		;
	sample buf[buflen];
	unsigned int i = 0;
	// Ignore to start
	while (i < start) {
		size_t left = (start - i)*channels;
		i += freadordie(buf, (left > buflen) ? buflen : left);
	}
	// Read & write to end
	while ((end == -1) || (i < end)) {
		size_t left = (end - i)*channels;
		size_t read = freadordie(buf, (left > buflen) ? buflen : left);
		fwrite(buf, sizeof(sample), read, stdout);
		if (ferror(stdout))
			die("snip: fwrite: %s", strerror(errno));
		i += read;
	}
}
