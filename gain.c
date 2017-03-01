#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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
writeproductordie(double multiplier, int writeleft, char *line, FILE *cmd)
{
  sample buf[BUFLEN];
	while (writeleft) {
		size_t goal = ((writeleft==-1) || (writeleft>BUFLEN)) ? BUFLEN : writeleft;
		ssize_t got;
		do got = read(0, buf, goal); while ((got == -1) && (errno == EINTR));
		if (got == -1)
			die(cmd, line, "gain: read: %s", strerror(errno));
    for (size_t i=0; i<got; i++)
      buf[i] = (sample)(((double)buf[i]) * multiplier);
    fwrite(buf, 1, got, stdout);
		if (ferror(stdout))
			die(cmd, line, "gain: fwrite: %s", strerror(errno));
		if (!got)
			break;
		if (writeleft != -1)
			writeleft -= got;
  }
}

int 
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	double multiplier = 1.0;
	char *endptr;
	if ((argc > 1) && (((multiplier = strtod(argv[1], &endptr)) < 0.0) ||
			(!strlen(argv[1])) || (*endptr != '\0')))
		die(NULL, NULL, "gain: multiplier must be nonnegative number");
	FILE *cmd;
	if (fcntl(3, F_GETFD) == -1) {
		if ((cmd = fopen("/dev/null", "r")) == NULL)
			die(NULL, NULL, "gain: fopen /dev/null");
	} else if ((cmd = fdopen(3, "r")) == NULL)
		die(NULL, NULL, "gain: fdopen 3");
  
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
		writeproductordie(multiplier, cuei - samplei, line, cmd);
		samplei = cuei;
		if (chari++ == len)  // no command
			continue;

		// Parse next multiplier
		if (((multiplier = strtod(&line[chari], NULL)) < 0.0f) || (errno == EINVAL))
			die(cmd, line, "gain: multiplier for '%s' not a nonnegative number", line);
	}
	if (ferror(cmd))
		die(cmd, line, "gain: getline: %s", strerror(errno));

	// Write remaining input and cleanup
	writeproductordie(multiplier, -1, line, cmd);
	fclose(cmd);
	free(line);
}
