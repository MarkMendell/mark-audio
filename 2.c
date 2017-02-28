#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


int MAXFD = 4096;
int MAXFDS = 50;
typedef int16_t sample;
int BUFLEN = 4096/sizeof(sample);

struct fdpipe {
	char type;
	int pipefd;
};


int
main(int argc, char **argv)
{
	int sysmaxfd = sysconf(_SC_OPEN_MAX);
	if (sysmaxfd == -1)
		die("2: sysconf(_SC_OPEN_MAX): %s", strerror(errno));
	else if (sysmaxfd < 0)
		die("2: huh???");
	int maxfd = (sysmaxfd > MAXFD) ? MAXFD : sysmaxfd;

	struct pollfd pfds[MAXFDS*3];  // parent<>outside parent<>left parent<>right
	struct fdpipe fdpipes[MAXFDS];
	int fdslen = 0;
	for (int fd=0; fd<maxfd; fd++) {
		if (fd == STDERR_FILENO)
			continue;
		int flags = fcntl(fd, F_GETFL);
		if (flags != -1) {
			if ((flags & O_ACCMODE) == O_RDWR)
				die("2: files opened for both reading and writing are not supported "
					"(fd %d)", fd);
			char type = ((flags & O_ACCMODE) == O_RDONLY) ? 'r' : 'w';
			if ((type == 'r') && (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1))
				die("2: fcntl %d nonblock: %s", fd, strerror(errno));
			pfds[fdslen] = { .fd = fd, .events = POLLIN };
			for (int ch=0; ch<2; ch++) {
				int p[2];
				if (pipe(p))
					die("2: pipe: %s", strerror(errno));
				pfds[(1+ch)*MAXFDS+fdslen] = { .fd = p[(type=='w')], .events = POLLIN };
				if (type == 'r')
			}
			fdpipes[fdslen] = ;
			if (++fdslen > MAXFDS)
				die("2: too many fds");
		}
	}

	int lfds[fdslen*2], rfds[fdslen*2];
	for (int fdi=0; fdi<fdslen*2; fdi++) {
		int ends[2];
		for (int chi=0; chi<2; chi++)
			for (int prwi=0; prwi<2; prwi++) {
				if (pipe(ends))
					die("2: pipe: %s", strerror(errno));
				pfds[(1+2*chi+prwi)*fdslen+fdi] = ends[prwi];
				(chi ? rfds : lfds)[fdi+fdslen*(1-prwi)] = ends[1-prwi];
			}
	}

	pid_t pid = fork();
	if (pid == -1)
		die("2: fork: %s", strerror(errno));
	// Left channel
	else if (pid) {
	} else if ((pid = fork()) == -1)
		die("2: fork: %s", strerror(errno));
	// Right channel
	else if (pid) {
	// Parent
	} else {
		struct fdbuf obufs[fdslen], lbufs[fdslen], rbufs[fdslen];
		for (int i=0; i<fdslen*3; i++) {
			obufs[i].len = 0;
			lbufs[i].len = 0;
			rbufs[i].len = 0;
		}
		int res;
		while (((res = poll(pfds, fdslen*5)) != -1) ||
				((errno == EAGAIN) || (errno == EINTR))) {
			for (int i=0; i<fdslen*5; i++) {
				int fd = pfds[i].fd;
				struct fdbuf ibuf = (i < fdslen) ? obufs[i] :
					(i < fdslen*3) ? lbufs[i%fdslen] : rbufs[i%fdslen];
				if (i < 
				// Either an error or hangup, so do the read/write and trigger it
				if (pfds[i].revents & (POLLERR | POLLHUP)) {
					int flags = fcntl(fd, F_GETFL);
					if (flags == -1)
						die("2: fctnl(%d,F_GETFL) after POLLERR | POLLHUP: %s", fd,
							strerror(errno));
					if ((flags & O_ACCMODE) == O_RDONLY) {
						if ((res = read(fd, bufs[0].buf, 1)))
					}
				}
			}
		}
	}
}
