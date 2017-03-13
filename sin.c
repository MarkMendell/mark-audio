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

int
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	double freq = 0.0;
	if (argc > 1) {
		char *endptr;
		freq = (errno=0, strtod(argv[1], &endptr));
		if (errno)
			die("sin: strtod '%s': %s", argv[1], strerror(errno));
		if ((*endptr != '\0') || (freq < 0.0))
			die("sin: frequency must be nonnegative number, not '%s'", argv[1]);
	}

	double phase = 0.0;
	sample buf[BUFLEN];
	while (!feof(stdout)) {
		for (int bufi=0; bufi<BUFLEN; bufi++)
			if (freq) {
				buf[bufi] = (sample)(sin(M_PI*2*phase) * MAXSAMPLE);
				phase = fmod(phase + (freq / SAMPLERATE), 1.0);
			} else {
				buf[bufi] = 0.0;
				phase = 0.0;
			}
		fwrite(buf, sizeof(sample), BUFLEN, stdout);
		if (ferror(stdout))
			die("sin: fwrite: %s", strerror(errno));
	}
}
