#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


int DELAY = 10;  // milliseconds
int SAMPLERATE = 44100;
int BUFLEN = 4096;


/* printf to stderr with a newline and exit with nonzero status. */
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

long
msordie(void)
{
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t))
		die("stamp: clock_gettime: %s", strerror(errno));
	return t.tv_sec*1000 + t.tv_nsec/1000000;
}

int
main(void)
{
	setbuf(stdout, NULL);
	long start = msordie();
	int flags = fcntl(0, F_GETFL);
	if (flags == -1)
		die("stamp: fcntl F_GETFL: %s", strerror(errno));
	if (fcntl(0, F_SETFL, flags | O_NONBLOCK) == -1)
		die("stamp: fcntl F_SETFL nonblocking: %s", strerror(errno));

	char buf[BUFLEN+1];
	buf[0] = '\t';
	size_t end = 1;
	struct pollfd inpoll = { .fd = 0, .events = POLLIN };
	int ret = 0;
	do {
		printf("%ld", ((msordie()-start+DELAY)*SAMPLERATE)/1000);
		if (ferror(stdout))
			die("stamp: printf: %s", strerror(errno));
		if (ret) {
			size_t count = read(0, &buf[end], BUFLEN-end);
			if ((count == -1) && (errno != EAGAIN) && (errno != EINTR))
				die("stamp: read: %s", strerror(errno));
			if (count == 0) {
				if (putchar('\n') == EOF)
					die("stamp: putchar: %s", strerror(errno));
				break;
			}
			for (int i=end; i<end+count; i++)
				if (buf[i] == '\n') {
					if (i != 1) {
						fwrite(buf, 1, i, stdout);
						if (ferror(stdout))
							die("stamp: fwrite: %s", strerror(errno));
					}
					memmove(&buf[1], &buf[i+1], end+count-(i+1));
					end = end+count-(i+1)+1;
					count = 0;
					i = 1;
				}
			end += count;
			if (end == BUFLEN+1)
				die("stamp: line too long");
		}
		if (putchar('\n') == EOF)
			die("stamp: putchar: %s", strerror(errno));
	} while (((ret = poll(&inpoll, 1, DELAY)) != -1) || (errno == EAGAIN) ||
			(errno == EINTR));
	if (ret == -1)
		die("stamp: poll: %s", strerror(errno));
}
