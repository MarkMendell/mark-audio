#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


unsigned int BUFLEN = 4096;
typedef int16_t sample;
struct inputnode {
	char *line;
	FILE *pipe;
	struct inputnode *prev;
	struct inputnode *next;
};

/* Clean up all elements of the input list pointed to by head. */
void
freeinputlist(struct inputnode *head)
{
	while (head->next != NULL) {
		struct inputnode *nexthead = head->next;
		free(head->line);
		pclose(head->pipe);
		free(head);
		head = nexthead;
	}
	free(head);
}

/* Free the input list head (if not NULL) and line, then printf the rest of the
 * args to stderr and exit with nonzero status. */
// TODO: swap comment order
void
die(struct inputnode *head, char *line, char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	if (head)
		freeinputlist(head);
	free(line);
	exit(EXIT_FAILURE);
}

/* Writes the sum of each sample read across the list of inputs provided until
 * writeleft*channels many samples have been written. If writeleft is -1,
 * samples are written until all inputs have reached EOF. As inputs reach EOF,
 * they are removed from the list. If all inputs reach EOF before count_end many
 * samples have been written, 0 is written for the remaining count. If the
 * number of samples read is not a multiple of channels, an error message is
 * shown. */
void
writesummedinputsordie(struct inputnode **headptr, int writeleft,
	unsigned int channels, char *line)
{
	unsigned int buflen = BUFLEN - 1;
	while (++buflen % channels);
	sample readbuf[buflen];
	sample sumbuf[buflen];
	// Output sum of inputs in increments of buflen
	while (writeleft) {
		struct inputnode *input = *headptr;
		memset(sumbuf, 0, buflen * sizeof(sample));
		size_t writec = (writeleft == -1) ? buflen :
			(writeleft*channels > buflen) ? buflen : writeleft*channels;
		size_t maxread = 0;
		// Add writec samples from input to sumbuf, then move to next input
		while (input->next != NULL) {
			size_t readc = fread(readbuf, sizeof(sample), writec, input->pipe);
			if (ferror(input->pipe))
				die(*headptr, line, "cue: fread for '%s': %s", input->line,
					strerror(errno));
			// End of input, remove it from list
			if (feof(input->pipe)) {
				if (readc % channels)
					fprintf(stderr, "cue: missing channel samples from '%s'\n",
						input->line);
				int status = pclose(input->pipe);
				if (status == -1)
					die(*headptr, line, "cue: pclose pipe for '%s': %s", input->line,
						strerror(errno));
				if (status)
					fprintf(stderr, "cue: '%s' exited with status %d\n", input->line,
						status);
				if (input == *headptr) {
					*headptr = input->next;
				} else {
					input->prev->next = input->next;
					input->next->prev = input->prev;
				}
				free(input->line);
				free(input);
			}
			if (readc > maxread)
				maxread = readc;
			for (int i=0; i<readc; i++)
				sumbuf[i] += readbuf[i];
			input = input->next;
		}
		// Output the summed inputs
		fwrite(sumbuf, sizeof(sample), (writeleft == -1) ? maxread : writec, stdout);
		if (ferror(stdout))
			die(*headptr, line, "cue: fwrite: %s", strerror(errno));
		if ((writeleft == -1) && ((*headptr)->next == NULL))
			break;
		else if (writeleft != -1)
			writeleft -= writec;
	}
}

int
main(int argc, char **argv)
{
	if (argc != 2)
		die(NULL, NULL, "usage: cue channels");
	unsigned int channels = atoi(argv[1]);
	if (channels < 1)
		die(NULL, NULL, "cue: channels must be positive integer");
	setbuf(stdout, NULL);

	struct inputnode *head = malloc(sizeof(struct inputnode));  // list of inputs
	if (head == NULL)
		die(NULL, NULL, "cue: malloc head: %s", strerror(errno));
	head->next = NULL;
	unsigned int samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	while ((len = getline(&line, &_, stdin)) != -1) {
		// Parse line of format "index command"
		if (line[len-1] != '\n')
			die(head, line, "cue: got partial command (no newline): %s", line);
		line[--len] = '\0';
		unsigned int cuei = 0;  // when to start command
		unsigned int chari = 0;
		do
			if (!isdigit(line[chari]))
				die(head, line, "cue: lines must be 'index[\tcommand]', not: %s", line);
			else
				cuei = 10*cuei + (line[chari] - '0');
		while ((++chari < len) && (!isspace(line[chari])));
		if (cuei < samplei)
			die(head, line, "cue: cue '%s' out of order", line);

		// Write up to the next cue index
		writesummedinputsordie(&head, cuei - samplei, channels, line);
		samplei = cuei;
		if (chari++ == len)  // no command
			continue;

		// Run command in a subprocess and add it to our list of inputs
		struct inputnode *oldhead = head;
		if ((head = malloc(sizeof(struct inputnode))) == NULL)
			die(oldhead, line, "cue: malloc new input %u: %s", cuei, strerror(errno));
		if ((head->line = malloc(len+1)) == NULL) {
			char *errmsg = strerror(errno);
			free(head);
			die(oldhead, line, "cue: malloc line '%s': %s", line, errmsg);
		}
		head->next = oldhead;
		oldhead->prev = head;
		strcpy(head->line, line);
		errno = 0;
		if ((head->pipe = popen(&line[chari], "r")) == NULL) {
			char *errmsg = errno ? strerror(errno) : "unknown popen error";
			free(head->line);
			free(head);
			die(oldhead, line, "cue: popen for '%s': %s", line, errmsg);
		}
	}

	// Write the remaining input and clean up
	if (ferror(stdin))
		die(head, line, "cue: getline: %s", strerror(errno));
	writesummedinputsordie(&head, -1, channels, line);
	freeinputlist(head);
	free(line);
}
