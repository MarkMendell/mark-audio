#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


typedef int16_t sample;
unsigned int BUFLEN = 4096/sizeof(sample);


/* printf args 3+ to stderr, then free the line, fclose the child, and exit with
 * nonzero status. */
// TODO: kill child process?
void
	// TODO: send HUP to pid, don't close files
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
	signal(SIGPIPE, SIG_IGN);
	// Join args with spaces to make the shell command
	int arglen = 0;
	for (int i=1; i<argc; i++)
		arglen += 1 + strlen(argv[i]);
	char arg[arglen];
	arg[0] = '\0';
	for (int i=1; i<argc; i++) {
		strcat(arg, argv[i]);
		if (i != argc-1)
			strcat(arg, " ");
	}
	// Execute the command in a child process
	int fds[4];
	if (pipe(fds) || pipe(fds+2))
		die("offer '%s': pipe: %s", arg, strerror(errno));
	pid_t pid = fork();
	if (pid == -1)
		die("offer '%s': fork: %s", arg, strerror(errno));
	if (!pid) {  // child
		int fd;
		if ((fd=0, dup2(fds[2], fd) == -1) || (fd=1, dup2(fds[1], fd) == -1))
			die("offer '%s' child: dup2 %d: %s", arg, fd,
				strerror(errno));
		if (close(fds[0]) || close(fds[1]) || close(fds[2]) || close(fds[3]))
			die("offer '%s' child: close: %s", arg, strerror(errno));
		if (execlp("sh", "sh", "-c", arg, NULL))
			die("offer '%s' child: execlp: %s", arg, strerror(errno));
	}
	if (close(fds[1]) || close(fds[2]))
		die("offer '%s': close: %s", arg, strerror(errno));
	FILE *r, *w;
	if (!(r = fdopen(fds[0], "r")) || !(w = fdopen(fds[3], "w")))
		die("offer '%s': fdopen: %s", arg, strerror(errno));
	setbuf(r, NULL), setbuf(w, NULL);

	unsigned int samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	sample buf[BUFLEN];
	while ((len = getline(&line, &_, stdin)) != -1) {
		// Parse line of format "index command"
		if (line[len-1] != '\n')
			die("offer '%s': got partial command (no newline): %s",
				arg, line);
		line[--len] = '\0';

		// Parse next sample index to send command
		char *cmd;
		unsigned long cmdi = (errno=0, strtoul(line, &cmd, 10));
		if (errno)
			die("offer: strtoul for cue '%s': %s", line, strerror(errno));
		if (cmdi < samplei)
			// TODO: replace this and other with arg %s
			die("offer: command '%s' out of order", line);

		// Send command through unless it's 'off', which means we stop
		int off = ((cmd++ - line) < len) && !strcasecmp(cmd, "off");
		if (off)
			fprintf(w, "%lu\n", cmdi);
		else {
			fprintf(w, "%s\n", line);
			if (ferror(w))
				die("offer '%s': fread: %s", arg, strerror(errno));
		}

		// Write up to the next cmd index
		while (samplei != cmdi) {
			size_t toread = (cmdi-samplei > BUFLEN) ? BUFLEN : cmdi-samplei;
			size_t readc = fread(buf, sizeof(sample), toread, r);
			if (ferror(r))
				die("offer '%s': fread: %s", arg, strerror(errno));
			fwrite(buf, sizeof(sample), readc, stdout);
			if (ferror(stdout))
				die("offer '%s': fwrite: %s", arg, strerror(errno));
			if (feof(r))
				goto cleanup;
			samplei += readc;
		}
		if (off)
			goto cleanup;
	}
	if (ferror(stdin))
		die("offer '%s': getline: %s", arg, strerror(errno));

cleanup:
	if (fclose(r) || fclose(w))
		die("offer '%s': fclose: %s", arg, strerror(errno));
}
