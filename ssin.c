#include <errno.h>
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
die(char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

double
parsefreq(char *s)
{
	char *endptr;
	double freq = (errno=0, strtod(s, &endptr));
	if (errno)
		die("ssin: strtod '%s': %s", s, strerror(errno));
	if ((*endptr != '\0') || (freq < 0.0))
		die("ssin: frequency must be nonnegative number, not '%s'", s);
	return freq;
}

int
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	double freq = (argc > 1) ? parsefreq(argv[1]) : 0.0;

	double phase = 0.0;  // 0.0-1.0 progress of phase
	unsigned long samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	sample buf[BUFLEN];
	// Read new frequency change, write til its time, then update it
	while ((len = getline(&line, &_, stdin)) != -1) {
		if (line[len-1] != '\n')
			die("ssin: got partial command (no newline): %s", line);
		line[--len] = '\0';

		// Parse time to change frequency
		char *cmd;
		unsigned long cmdi = (errno=0, strtoul(line, &cmd, 10));
		if (errno)
			die("ssin: strtoul for cue '%s': %s", line, strerror(errno));
		if (cmdi < samplei)
			die("ssin: command '%s' out of order", line);

		// Write up to the next cue index
		while (samplei < cmdi) {
			size_t writec = ((cmdi - samplei) > BUFLEN) ? BUFLEN : (cmdi - samplei);
			for (int bufi=0; bufi<writec; bufi++)
				if (freq) {
					buf[bufi] = (sample)(sin(M_PI*2*phase) * MAXSAMPLE);
					phase = fmod(phase + (freq / SAMPLERATE), 1.0);
				} else {
					buf[bufi] = 0.0;
					phase = 0.0;
				}
			fwrite(buf, sizeof(sample), writec, stdout);
			if (ferror(stdout))
				die("ssin: fwrite: %s", strerror(errno));
			samplei += writec;
		}

		// Parse next frequency
		if ((cmd++ - line) < len)
			freq = parsefreq(cmd);
	}
	if (ferror(stdin))
		die("ssin: getline: %s", strerror(errno));
}
