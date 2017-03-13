#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


typedef int16_t sample;
unsigned int BUFLEN = 4096/sizeof(sample);
typedef int32_t multsafesample;
sample MAXSAMPLE = INT16_MAX;
unsigned int SAMPLERATE = 44100;


void 
die(char *line, char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	free(line);
	exit(EXIT_FAILURE);
}

double
parsefreqordie(char *s, char *line)
{
	char *endptr;
	errno = 0;
	double freq = strtod(s, &endptr);
	if (errno)
		die(line, "sin: strtod '%s': %s", s, strerror(errno));
	if ((*endptr != '\0') || (freq < 0.0))
		die(line, "sin: frequency must be nonnegative number, not '%s'", s);
	return freq;
}

void
writesamplesordie(double freq, double *phaseptr, unsigned long count,
	char *line)
{
	sample buf[BUFLEN];
	unsigned long outi = 0;
	while (outi < count) {
		size_t writec = ((count - outi) > BUFLEN) ? BUFLEN : (count - outi);
		for (int bufi=0; bufi<writec; bufi++)
			if (freq) {
				buf[bufi] = (sample)(sin(M_PI*2*(*phaseptr)) * MAXSAMPLE);
				*phaseptr = fmod(*phaseptr + (freq / SAMPLERATE), 1.0);
			} else {
				buf[bufi] = 0.0;
				*phaseptr = 0.0;
			}
		fwrite(buf, sizeof(sample), writec, stdout);
		if (ferror(stdout))
			die(line, "sin: fwrite: %s", strerror(errno));
		outi += writec;
	}
}

int
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	double freq = 0.0;
	if (argc > 1)
		freq = parsefreqordie(argv[1], NULL);

	double phase = 0.0;  // 0.0-1.0 progress of phase
	unsigned long samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	sample buf[BUFLEN];
	// Read new frequency change, write til its time, then update it
	while ((len = getline(&line, &_, stdin)) != -1) {
		if (line[len-1] != '\n')
			die(line, "sin: got partial command (no newline): %s", line);
		line[--len] = '\0';

		// Parse time to change frequency
		char *cmd;
		unsigned long cmdi = (errno=0, strtoul(line, &cmd, 10));
		if (errno)
			die(line, "sin: strtoul for cue '%s': %s", line, strerror(errno));
		if (cmdi < samplei)
			die(line, "sin: command '%s' out of order", line);

		// Write up to the next cue index
		writesamplesordie(freq, &phase, cuei - samplei, line);
		samplei = cuei;

		// Parse next frequency
		if ((cmd++ - line) != len)
			freq = parsefreqordie(cmd, line);
	}
	if (ferror(stdin))
		die(line, "sin: getline: %s", strerror(errno));
	free(line);

	// Write current frequency forever
	writesamplesordie(freq, &phase, ULONG_MAX, NULL);
}
