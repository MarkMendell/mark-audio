#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


size_t BUFLEN = 4096;
typedef int16_t sample;


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
writeproductordie(double multiplier, int writeleft, unsigned int channels,
	char *line, FILE *cmd)
{
	unsigned int buflen = BUFLEN - 1;
	while (++buflen % channels)
		;
  sample buf[buflen];
	while (writeleft) {
		size_t writec = (writec == -1) ? buflen :
			(writeleft*channels > buflen) ? buflen : writeleft*channels;
		size_t read = fread(buf, sizeof(sample), writec, stdin);
		if (ferror(stdin))
			die(cmd, line, "gain: fread: %s", strerror(errno));
    for (size_t i=0; i<read; i++)
      buf[i] = (sample)(((double)buf[i]) * multiplier);
    fwrite(buf, sizeof(sample), read, stdout);
		if (ferror(stdout))
			die(cmd, line, "gain: fwrite: %s", strerror(errno));
		if (feof(stdin))
			break;
		if (writeleft != -1)
			writeleft -= writec;
  }
}

int 
main(int argc, char *argv[])
{
	if ((argc < 2) || (argc > 3))
		die(NULL, NULL, "usage: gain channels [multiplier]");
	unsigned int channels = atoi(argv[1]);
	if (channels < 1)
		die(NULL, NULL, "gain: channels must be positive integer");
	double multiplier = 1.0;
	if ((argc == 3) &&
			(((multiplier = strtod(argv[2], NULL)) < 0.0f) || (errno == EINVAL)))
    die(NULL, NULL, "gain: multiplier must be nonnegative number");
	FILE *cmd;
	if (fcntl(3, F_GETFD) == -1) {
		if ((cmd = fopen("/dev/null", "r")) == NULL)
			die(NULL, NULL, "gain: fopen /dev/null");
	} else if ((cmd = fdopen(3, "r")) == NULL)
		die(NULL, NULL, "gain: fdopen 3");
	setbuf(stdout, NULL);
  
	unsigned int samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	while ((len = getline(&line, &_, cmd)) != -1) {
		// Parse line of format "index command"
		if (line[len-1] != '\n')
			die(cmd, line, "gain: got partial command (no newline): %s", line);
		line[--len] = '\0';
		unsigned int cuei = 0;  // when to start command
		unsigned int chari = 0;
		do
			if (!isdigit(line[chari]))
				die(cmd, line, "gain: lines must be 'index[\tcommand]', not: %s", line);
			else
				cuei = 10*cuei + (line[chari] - '0');
		while ((++chari < len) && (!isspace(line[chari])));
		if (cuei < samplei)
			die(cmd, line, "gain: command '%s' out of order", line);

		// Write up to the next cue index
		writeproductordie(multiplier, cuei - samplei, channels, line, cmd);
		samplei = cuei;
		if (chari++ == len)  // no command
			continue;

		// Parse next multiplier
		if (((multiplier = strtod(&line[chari], NULL)) < 0.0f) || (errno == EINVAL))
			die(cmd, line, "gain: multiplier for '%s' not a nonnegative number", line);
	}

	// Write remaining input
	if (ferror(cmd))
		die(cmd, line, "gain: getline: %s", strerror(errno));
	writeproductordie(multiplier, -1, channels, line, cmd);
	fclose(cmd);
	free(line);
}
