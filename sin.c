#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


unsigned int BUFLEN = 4096;
typedef int16_t sample;
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
writesamplesordie(double freq, double *phaseptr, int count, char *line)
{
	sample buf[BUFLEN];
	unsigned int buflen = BUFLEN;
	unsigned int outi=0;
	while ((count == -1) || (outi < count)) {
		if ((outi % buflen) == 0) {
			buflen = ((count == -1) || ((count - outi) > BUFLEN)) ?
				BUFLEN : (count - outi);
			for (int bufi=0; bufi<buflen; bufi++)
				if (freq) {
					buf[bufi] = (sample)(sin(M_PI*2*(*phaseptr)) * MAXSAMPLE);
					*phaseptr = fmod(*phaseptr + (freq / SAMPLERATE), 1.0);
				} else {
					buf[bufi] = 0.0;
					*phaseptr = 0.0;
				}
		}
		fwrite(buf, sizeof(sample), buflen, stdout);
		if (ferror(stdout))
			die(line, "sin: fwrite: %s", strerror(errno));
		if (count != -1)
			outi += buflen;
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
	unsigned int samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	sample buf[BUFLEN];
	while ((len = getline(&line, &_, stdin)) != -1) {
		// Parse line of format "index command"
		if (line[len-1] != '\n')
			die(line, "sin: got partial command (no newline): %s", line);
		line[--len] = '\0';
		unsigned int cuei = 0;  // when to start command
		unsigned int chari = 0;
		do
			if (!isdigit(line[chari]))
				die(line, "sin: lines must be 'index[\tcommand]', not: %s", line);
			else
				cuei = 10*cuei + (line[chari] - '0');
		while ((++chari < len) && (!isspace(line[chari])));
		if (cuei < samplei)
			die(line, "sin: command '%s' out of order", line);

		// Write up to the next cue index
		writesamplesordie(freq, &phase, cuei - samplei, line);
		samplei = cuei;

		// Parse next frequency
		if (chari++ != len)
			freq = parsefreqordie(&line[chari], NULL);
	}
	if (ferror(stdin))
		die(line, "sin: getline: %s", strerror(errno));
	free(line);

	// Write current frequency forever
	writesamplesordie(freq, &phase, -1, NULL);
}
