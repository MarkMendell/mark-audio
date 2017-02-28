#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef int16_t sample;
unsigned int BUFLEN = 4096/sizeof(sample);
struct inputnode {
	char *line;
	FILE *pipe;
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

/* printf args 3+ to stderr, then free the input list head (if not NULL) and
 * line and exit with nonzero status. */
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
 * writeleft many samples have been written. If writeleft is -1, samples are
 * written until all inputs have reached EOF. As inputs reach EOF, they are
 * removed from the list. If all inputs reach EOF before count_end many samples
 * have been written, 0 is written for the remaining count. */
void
writesummedinputsordie(struct inputnode **headptr, int writeleft, char *line)
{
	sample readbuf[BUFLEN], sumbuf[BUFLEN];
	// Output sum of inputs in increments of BUFLEN
	while (writeleft) {
		struct inputnode *input = *headptr;
		struct inputnode *prev;
		memset(sumbuf, 0, BUFLEN * sizeof(sample));
		size_t writec = ((writeleft==-1)||(writeleft>BUFLEN)) ? BUFLEN : writeleft;
		size_t maxread = 0;
		// Add writec samples from input to sumbuf, then move to next input
		while (input->next != NULL) {
			size_t readc = fread(readbuf, sizeof(sample), writec, input->pipe);
			if (ferror(input->pipe))
				die(*headptr, line, "cue: fread for '%s': %s", input->line,
					strerror(errno));
			// End of input, remove it from list
			if (feof(input->pipe)) {
				int status = pclose(input->pipe);
				if (status == -1)
					die(*headptr, line, "cue: pclose pipe for '%s': %s", input->line,
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
		writesummedinputsordie(&head, cuei - samplei, line);
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
		strcpy(head->line, line);
		errno = 0;
		if ((head->pipe = popen(&line[chari], "r")) == NULL) {
			char *errmsg = errno ? strerror(errno) : "unknown popen error";
			free(head->line);
			free(head);
			die(oldhead, line, "cue: popen for '%s': %s", line, errmsg);
		}
	}
	if (ferror(stdin))
		die(head, line, "cue: getline: %s", strerror(errno));

	// Write the remaining input and clean up
	writesummedinputsordie(&head, -1, line);
	freeinputlist(head);
	free(line);
}
