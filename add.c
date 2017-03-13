#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef int16_t sample;
int BUFLEN = 4096/sizeof(sample);


/* printf the 3rd+ args to stderr and exit with nonzero status. */
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

int 
main(int argc, char **argv)
{
	// Open all streams as FILE's
	int filec = 0;
	do filec++; while (fcntl(filec + 2, F_GETFD) != -1);
	FILE *files[filec];
	files[0] = stdin;
	for (int i=1; i<filec; i++)
		if ((files[i] = fdopen(i+2, "r")) == NULL)
			die("add: fdopen %d: %s\n", i+2, strerror(errno));

	sample readbuf[BUFLEN];
	sample sumbuf[BUFLEN];
	size_t maxread;
	// Add the streams and output until they all run out
	do {
		memset(sumbuf, 0, sizeof(readbuf));
		maxread = 0;
		for (int filei=0; filei<filec; filei++) {
			size_t read = fread(readbuf, sizeof(sample), BUFLEN, files[filei]);
			if (ferror(files[filei])) {
				char *errmsg = strerror(errno);
				die("add: fread %d: %s\n", fileno(files[filei]), errmsg);
			}
			if (read > maxread)
				maxread = read;
			for (size_t i=0; i<read; i++)
				sumbuf[i] += readbuf[i];
		}
		fwrite(sumbuf, sizeof(sample), maxread, stdout);
		if (ferror(stdout))
			die("add: fwrite");
	} while (maxread);
}
