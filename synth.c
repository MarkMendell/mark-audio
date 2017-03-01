#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


typedef int16_t sample;
unsigned int BUFLEN = 4096/sizeof(sample);
struct notenode {
	char *key;
	int off;
	FILE *r;
	FILE *w;
	struct notenode *next;
	unsigned int offset;
	pid_t pid;
};


/* Clean up all elements of the list pointed to by head. */
void
freenotelist(struct notenode *head)
{
	while (head->next != NULL) {
		struct notenode *nexthead = head->next;
		if (head->key)
			free(head->key);
		if (head->r)
			fclose(head->r);
		if (head->w)
			fclose(head->w);
		free(head);
		head = nexthead;
	}
	free(head);
}

/* printf args 3+ to stderr, then free the list head (if not NULL) and line and
 * exit with nonzero status. */
void
die(struct notenode *head, char *line, char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	if (head)
		freenotelist(head);
	free(line);
	exit(EXIT_FAILURE);
}

void
writesummednotesordie(struct notenode **headptr, unsigned int samplei,
	unsigned int endi, char *line, char *cmd)
{
	// Tell each note to make samples up to endi
	struct notenode *n = *headptr;
	while (n->next) {
		fprintf(n->w, "%u\n", endi - n->offset);
		if (ferror(n->w) && (errno != EPIPE))
			die(*headptr, line, "synth '%s': fprintf to '%s' from %u to %u: %s", cmd,
				n->key, samplei, endi, strerror(errno));
		n = n->next;
	}

	// Output sum of notes in increments of BUFLEN
	sample readbuf[BUFLEN], sumbuf[BUFLEN];
	while (samplei != endi) {
		n = *headptr;
		struct notenode *prev;
		memset(sumbuf, 0, BUFLEN * sizeof(sample));
		size_t writec = (endi-samplei > BUFLEN) ? BUFLEN : endi-samplei;
		size_t maxread = 0;

		// Add writec samples from note to sumbuf, then move to next note
		while (n->next) {
			size_t readc = fread(readbuf, sizeof(sample), writec, n->r);
			if (ferror(n->r))
				die(*headptr, line, "synth '%s': fread for '%s': %s", cmd, n->key,
					strerror(errno));
			
			if (feof(n->r)) {  // End of note
				// Fail if note had bad exit status
				int status, ret;
				do ret = waitpid(n->pid,&status,0); while ((ret==-1)&&(errno==EINTR));
				if (ret == -1)
					die(*headptr, line, "synth '%s': waitpid for key '%s': %s", cmd,
						n->key, strerror(errno));
				if (WIFSIGNALED(status))
					die(*headptr, line, "synth '%s': key '%s' caught signal %d", cmd,
						n->key, WTERMSIG(status));
				else if (WIFEXITED(status) && (status = WEXITSTATUS(status)))
					die(*headptr, line, "synth '%s': key '%s' exited with status %d", cmd,
						n->key, status);
				else if (!WIFEXITED(status))
					die(*headptr, line, "synth '%s': key '%s' (pid %u) not terminated",
						cmd, n->key, n->pid);
				// Remove note
				if (fclose(n->r) || (n->r=NULL) || fclose(n->w) || (n->w=NULL))
					die(*headptr, line, "synth '%s': fclose key '%s': %s", cmd, n->key,
						strerror(errno));
				free(n->key);
				free(n);
				if (n == *headptr)
					*headptr = n->next;
				else
					prev->next = n->next;
			} else
				prev = n;

			if (readc > maxread)
				maxread = readc;
			for (int i=0; i<readc; i++)
				sumbuf[i] += readbuf[i];
			n = n->next;
		}

		// Output the summed notes
		fwrite(sumbuf, sizeof(sample), (endi==UINT_MAX) ? maxread : writec, stdout);
		if (ferror(stdout))
			die(*headptr, line, "synth '%s': fwrite at %u: %s", cmd, samplei,
				strerror(errno));
		if ((endi == UINT_MAX) && ((*headptr)->next == NULL))
			break;
		samplei += writec;
	}
}

struct notenode*
makenodeordie(struct notenode *head, char *line, char *cmd)
{
	struct notenode *node = malloc(sizeof(struct notenode));
	if (node == NULL)
		die(head, line, "synth '%s': malloc note node: %s", cmd, strerror(errno));
	node->next = NULL;
	node->off = 0;
	node->r = NULL;
	node->w = NULL;
	node->key = NULL;
	return node;
}

int
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	signal(SIGPIPE, SIG_IGN);
	// Join args with spaces to make the shell command
	int cmdlen = 0;
	for (int i=1; i<argc; i++)
		cmdlen += 1 + strlen(argv[i]);
	char cmd[cmdlen];
	cmd[0] = '\0';
	for (int i=1; i<argc; i++) {
		strcat(cmd, argv[i]);
		if (i != argc-1)
			strcat(cmd, " ");
	}

	struct notenode *head = makenodeordie(NULL, NULL, cmd);
	unsigned int samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	while ((len = getline(&line, &_, stdin)) != -1) {
		// Parse line of format "index command"
		if (line[len-1] != '\n')
			die(head, line, "synth '%s': got partial command (no newline): %s", cmd,
				line);
		line[--len] = '\0';
		unsigned int cmdi = 0;  // when to start command
		unsigned int chari = 0;
		do
			if (!isdigit(line[chari]))
				die(head, line, "synth '%s': lines must be 'index[\tcommand]', not: %s",
					cmd, line);
			else
				cmdi = 10*cmdi + (line[chari] - '0');
		while ((++chari < len) && (!isspace(line[chari])));
		if (cmdi < samplei)
			die(head, line, "synth '%s': command '%s' out of order", cmd, line);

		// Write up to the next cmd index
		writesummednotesordie(&head, samplei, cmdi, line, cmd);
		samplei = cmdi;
		if (chari++ == len)  // no command
			continue;

		// Get matching note node
		int keylen = -1;
		do keylen++; while ((chari+keylen<len) && (!isspace(line[chari+keylen])));
		struct notenode *n = head;
		while (n->next != NULL) {
			if (!n->off && (strlen(n->key) == keylen) &&
					!strncmp(n->key, &line[chari], keylen))
				break;
			n = n->next;
		}
		// Make one if there isn't a match
		if (n->next == NULL) {
			n->offset = cmdi;
			n->next = makenodeordie(head, line, cmd);
			if ((n->key = malloc(keylen+1)) == NULL)
				die(head, line, "synth '%s': malloc key for '%s': %s", cmd, line,
					strerror(errno));
			*stpncpy(n->key, &line[chari], keylen) = '\0';
			// Spawn subprocess with 'key' environment variable set to the key
			int fds[4];
			if (pipe(fds) || pipe(&fds[2]))
				die(head, line, "synth '%s': pipe: %s", cmd, strerror(errno));
			if (!(n->w = fdopen(fds[1], "w")) || !(n->r = fdopen(fds[2], "r")))
				die(head, line, "synth '%s': fdopen: %s", cmd, strerror(errno));
			setbuf(n->w, NULL), setbuf(n->r, NULL);
			pid_t pid = fork();
			if (pid == -1)
				die(head, line, "synth '%s': fork: %s", cmd, strerror(errno));
			if (!pid) { // child; close files connected to other notes, then execlp
				struct notenode *nn = head;
				while (nn->next) {
					if (fclose(nn->r)||(nn->r=NULL) || fclose(nn->w)||(nn->w=NULL))
						die(head, line, "synth '%s' child: fclose: %s",cmd,strerror(errno));
					nn = nn->next;
				}
				int fd;
				if ((fd=0, dup2(fds[0], fd) == -1) || (fd=1, dup2(fds[3], fd) == -1))
					die(head, line, "synth '%s' child: dup2 %d: %s", cmd, fd,
						strerror(errno));
				if (close(fds[0]) || close(fds[3]))
					die(head, line, "synth '%s' child: close: %s", cmd, strerror(errno));
				if (setenv("key", n->key, 1))
					die(head, line, "synth '%s' child: setenv: %s", cmd, strerror(errno));
				if (execlp("sh", "sh", "-c", cmd, NULL))
					die(head, line, "synth '%s' child: execlp: %s", cmd, strerror(errno));
			}
			// (parent)
			if (close(fds[0]) || close(fds[3]))
				die(head, line, "synth '%s': close child pipe ends: %s", cmd,
					strerror(errno));
			n->pid = pid;
		}

		// Send the child the command (if the command is 'off', also mark it as off)
		chari += keylen;
		if (chari++ == len)  // no command
			continue;
		if (strcasecmp(&line[chari], "off"))
			n->off = 1;
		fprintf(n->w, "%u\t%s\n", cmdi - n->offset, &line[chari]);
		if (ferror(n->w))
			die(head, line, "synth '%s': fprintf '%s' to '%s' at %u: %s", cmd,
				&line[chari], n->key, cmdi, strerror(errno));
	}
	if (ferror(stdin))
		die(head, line, "synth '%s': getline: %s", cmd, strerror(errno));

	// Send off to all the children
	struct notenode *n = head;
	while (n->next) {
		if (!n->off) {
			fprintf(n->w, "%u\toff\n", samplei);
			if ((ferror(n->w)) && (errno != EPIPE))
				die(head, line, "synth '%s': fprintf off to '%s' at %u: %s", cmd,
					n->key, samplei, strerror(errno));
		}
		n->off = 1;
		n = n->next;
	}

	// Read and write until all children are done, then cleanup
	writesummednotesordie(&head, samplei, UINT_MAX/*( ͡° ͜ʖ ͡°)*/, line, cmd);
	freenotelist(head);
	free(line);
}
