#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef int16_t sample;
size_t BUFLEN = 4096/sizeof(sample);
struct inputnode {
	char *line;
	FILE *pipe;
	struct inputnode *next;
};


/* printf args 3+ to stderr, then close all children listed in head (if not
 * NULL) and exit with nonzero status. */
void
die(struct inputnode *head, char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	while (head && head->next) {
		pclose(head->pipe);
		head = head->next;
	}
	exit(EXIT_FAILURE);
}

/* Writes the sum of each sample read across the list of inputs provided until
 * writeleft many samples have been written. If writeleft is ULONG_MAX, samples
 * are written until all inputs have reached EOF. As inputs reach EOF, they are
 * removed from the list. If all inputs reach EOF before count_end many samples
 * have been written, 0 is written for the remaining count. */
void
writesummedinputsordie(struct inputnode **headptr, unsigned long writeleft)
{
	sample readbuf[BUFLEN], sumbuf[BUFLEN];
	// Output sum of inputs in increments of BUFLEN
	while (writeleft) {
		struct inputnode *input = *headptr;
		struct inputnode *prev;
		memset(sumbuf, 0, BUFLEN * sizeof(sample));
		size_t writec = (writeleft > BUFLEN) ? BUFLEN : writeleft;
		size_t maxread = 0;
		// Add writec samples from input to sumbuf, then move to next input
		while (input->next) {
			size_t readc = fread(readbuf, sizeof(sample), writec, input->pipe);
			if (ferror(input->pipe))
				die(*headptr, "cue: fread for '%s': %s", input->line,
					strerror(errno));
			// End of input, remove it from list
			if (feof(input->pipe)) {
				int status = pclose(input->pipe);
				if (status == -1)
					die(*headptr, "cue: pclose pipe for '%s': %s", input->line,
						strerror(errno));
				if (status)
					fprintf(stderr, "cue: '%s' exited with status %d\n", input->line,
						status);
				if (input == *headptr)
					*headptr = input->next;
				else
					prev->next = input->next;
				free(input->line);
				free(input);
			} else
				prev = input;
			if (readc > maxread)
				maxread = readc;
			for (int i=0; i<readc; i++)
				sumbuf[i] += readbuf[i];
			input = input->next;
		}
		// Output the summed inputs
		fwrite(sumbuf,sizeof(sample),(writeleft==ULONG_MAX)?maxread:writec,stdout);
		if (ferror(stdout))
			die(*headptr, "cue: fwrite: %s", strerror(errno));
		if ((writeleft == ULONG_MAX) && ((*headptr)->next == NULL))
			break;
		else if (writeleft != ULONG_MAX)
			writeleft -= writec;
	}
}

int
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	signal(SIGPIPE, SIG_DFL);

	struct inputnode *head = malloc(sizeof(struct inputnode));  // list of inputs
	if (head == NULL)
		die(NULL, "cue: malloc head: %s", strerror(errno));
	head->next = NULL;
	unsigned long samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	// Execute each input line at its timestamp, summing old commands inbetween
	while ((len = getline(&line, &_, stdin)) != -1) {
		if (line[len-1] != '\n')
			die(head, "cue: got partial command (no newline): %s", line);
		line[--len] = '\0';

		// Parse index for cue
		char *cmd;
		unsigned long cuei = (errno=0, strtoul(line, &cmd, 10));
		if (errno)
			die(head, "cue: strtoul for cue '%s': %s", line, strerror(errno));
		if (cuei < samplei)
			die(head, "cue: cue '%s' out of order", line);

		// Write up to the next cue index
		writesummedinputsordie(&head, cuei - samplei);
		samplei = cuei;
		if ((cmd++ - line) == len)  // no command
			continue;

		// Run command in a subprocess and add it to our list of inputs
		struct inputnode *oldhead = head;
		if ((head = malloc(sizeof(struct inputnode))) == NULL)
			die(oldhead, "cue: malloc new input %u: %s", cuei, strerror(errno));
		if ((head->line = malloc(len+1)) == NULL)
			die(oldhead, "cue: malloc line '%s': %s", line, strerror(errno));
		head->next = oldhead;
		strcpy(head->line, line);
		if (errno=0, (head->pipe = popen(cmd, "r")) == NULL)
			die(oldhead, "cue: popen for '%s': %s", line, errno ? strerror(errno) :
				"unknown popen error");
	}
	if (ferror(stdin))
		die(head, "cue: getline: %s", strerror(errno));

	// Write the remaining input
	writesummedinputsordie(&head, ULONG_MAX);
}
