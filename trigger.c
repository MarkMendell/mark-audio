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
void
die(FILE *child, char *line, char *errfmt, ...)
{
	va_list argp;
	va_start(argp, errfmt);
	vfprintf(stderr, errfmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	free(line);
	if (child)
		fclose(child);
	exit(EXIT_FAILURE);
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
	// Execute the command in a child process
	int fds[2];
	if (pipe(fds))
		die(NULL, NULL, "trigger '%s': pipe: %s", cmd, strerror(errno));
	pid_t pid = fork();
	if (pid == -1)
		die(NULL, NULL, "trigger '%s': fork: %s", cmd, strerror(errno));
	if (!pid) {  // child
		int nullfd = open("/dev/null", O_RDONLY);
		if (nullfd == -1)
			die(NULL, NULL, "trigger '%s' child: open: %s", cmd, strerror(errno));
		int fd;
		if ((fd=0, dup2(nullfd, fd) == -1) || (fd=1, dup2(fds[1], fd) == -1))
			die(NULL, NULL, "trigger '%s' child: dup2 %d: %s", cmd, fd,
				strerror(errno));
		if (close(fds[0]) || close(fds[1]) || close(nullfd))
			die(NULL, NULL, "trigger '%s' child: close: %s", cmd, strerror(errno));
		if (execlp("sh", "sh", "-c", cmd, NULL))
			die(NULL, NULL, "trigger '%s' child: execlp: %s", cmd, strerror(errno));
	}
	if (close(fds[1]))
		die(NULL, NULL, "trigger '%s': close: %s", cmd, strerror(errno));
	FILE *child = fdopen(fds[0], "r");
	if (child == NULL)
		die(NULL, NULL, "trigger '%s': fdopen: %s", cmd, strerror(errno));
	setbuf(child, NULL);

	unsigned int samplei = 0;  // count of samples written
	char *line = NULL;  // input command
	ssize_t len;  // length of input command
	size_t _;
	sample buf[BUFLEN];
	while ((len = getline(&line, &_, stdin)) != -1) {
		// Parse line of format "index command"
		if (line[len-1] != '\n')
			die(child, line, "trigger '%s': got partial command (no newline): %s",
				cmd, line);
		line[--len] = '\0';
		unsigned int cmdi = 0;  // when to start command
		unsigned int chari = 0;
		do
			if (!isdigit(line[chari]))
				die(child, line, "trigger '%s': lines must be 'index[\tcommand]', "
					"not: %s", cmd, line);
			else
				cmdi = 10*cmdi + (line[chari] - '0');
		while ((++chari < len) && (!isspace(line[chari])));
		if (cmdi < samplei)
			die(child, line, "trigger '%s': command '%s' out of order", cmd, line);

		// Write up to the next cmd index
		while (samplei != cmdi) {
			size_t toread = (cmdi-samplei > BUFLEN) ? BUFLEN : cmdi-samplei;
			size_t readc = fread(buf, sizeof(sample), toread, child);
			if (ferror(child))
				die(child, line, "trigger '%s': fread: %s", cmd, strerror(errno));
			fwrite(buf, sizeof(sample), readc, stdout);
			if (ferror(stdout))
				die(child, line, "trigger '%s': fwrite: %s", cmd, strerror(errno));
			if (feof(child))
				goto cleanup;
			samplei += readc;
		}
		
		if (chari++ == len)  // no command
			continue;
		else if (!strcasecmp(&line[chari], "off"))
			goto cleanup;
		else
			die(child, line, "trigger '%s': unknown command '%s' at %u", cmd,
				&line[chari], cmdi);
	}
	if (ferror(stdin))
		die(child, line, "trigger '%s': getline: %s", cmd, strerror(errno));

cleanup:
	free(line);
	if (kill(pid, SIGTERM))
		die(child, line, "trigger '%s': kill: %s", cmd, strerror(errno));
	if (fclose(child))
		die(NULL, NULL, "trigger '%s': fclose: %s", cmd, strerror(errno));
}
