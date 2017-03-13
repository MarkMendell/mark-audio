#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef int16_t sample;
int BUFLEN = 4096/sizeof(sample);


void 
die(FILE *cmd, char *line, char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	if (cmd)
		fclose(cmd);
	free(line);
	exit(EXIT_FAILURE);
}

void
writeproductordie(double gain, unsigned long writeleft, char *line, FILE *cmd)
{
  sample buf[BUFLEN];
	int off = 0;
	size_t bytelen = BUFLEN * sizeof(sample);
	while (writeleft) {
		size_t goal = (writeleft>bytelen) ? bytelen : writeleft;
		ssize_t got;
		do got = read(0, (char*)buf+off, goal)+off; while ((got == -1) && (errno == EINTR));
		if (got == -1)
			die(cmd, line, "gain: read: %s", strerror(errno));
    for (size_t i=0; i<got/sizeof(sample); i++)
      buf[i] = (sample)(((double)buf[i]) * gain);
    fwrite(buf, sizeof(sample), got/sizeof(sample), stdout);
		if (ferror(stdout))
			die(cmd, line, "gain: fwrite: %s", strerror(errno));
		if (!got)
			break;
		if (writeleft != UINT_MAX)
			writeleft -= got/sizeof(sample);
		off = got % sizeof(sample);
		memmove(buf, buf+(got/sizeof(sample)), off);
  }
}

double
parsegain(char *s, FILE *cmd, char *line)
{
	char *endptr;
	double gain = (errno=0, strtod(s, &endptr));
	if (errno)
		die(cmd, line, "gain: strtod '%s': %s", s, strerror(errno));
	if (!strlen(s) || (*endptr != '\0') || (gain < 0.0))
		die(cmd, line, "gain: gain must be nonnegative number, not '%s'", s);
	return gain;
}

int 
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	double gain = 1.0;
	if (argc > 1)
		gain = parsegain(argv[1], NULL, NULL);
	FILE *cmd;
	if (fcntl(3, F_GETFD) == -1) {
		if ((cmd = fopen("/dev/null", "r")) == NULL)
			die(NULL, NULL, "gain: fopen /dev/null");
	} else if ((cmd = fdopen(3, "r")) == NULL)
		die(NULL, NULL, "gain: fdopen 3");

	unsigned long samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	// Read gain change lines and update after writing to that point
	while ((len = getline(&line, &_, cmd)) != -1) {
		if (line[len-1] != '\n')
			die(cmd, line, "gain: got partial command (no newline): %s", line);
		line[--len] = '\0';

		// Parse next sample index to change gain
		char *gainstr;
		unsigned long cuei = (errno=0, strtoul(line, &gainstr, 10));
		if (errno)
			die(cmd, line, "gain: strtoul for '%s': %s", line, strerror(errno));
		if (cuei < samplei)
			die(cmd, line, "gain: command '%s' out of order", line);

		// Write up to the next cue index
		writeproductordie(gain, cuei - samplei, line, cmd);
		samplei = cuei;
		if ((gainstr++ - line) == len)  // no command
			continue;

		// Parse next gain
		gain = parsegain(gainstr, cmd, line);
	}
	if (ferror(cmd))
		die(cmd, line, "gain: getline: %s", strerror(errno));

	// Write remaining input and cleanup
	writeproductordie(gain, ULONG_MAX, line, cmd);
	fclose(cmd);
	free(line);
}
