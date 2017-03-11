#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include <AudioToolbox/AudioQueue.h>
#include <CoreAudio/CoreAudioTypes.h>


typedef uint16_t sample;
int LEAVESIGS[]={SIGHUP,SIGINT,SIGPIPE,SIGQUIT,SIGTERM,SIGTSTP,SIGTTIN,SIGTTOU};
struct ttyinfo {
	int fd;
	struct termios old;
	struct termios new;
} TTY;
int SAMPLERATE = 44100;
int BUFS = 2;
int BUFLEN = 4096;
int BUFINCR = 44100;
struct state {
	int ch;
	int playing;
	sample *toplay;
	unsigned long left;
	unsigned long total;
	pthread_mutex_t lock;
	pthread_mutex_t runmut;
	pthread_cond_t runcond;
	int reap;  // ༼ ༎ຶ ෴    ༎ຶ༽
};


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

unsigned long
parseposordie(char *s, char *name)
{
	char *endptr;
	unsigned long l = (errno=0, strtoul(s, &endptr, 10));
	if (errno)
		die("select: strtoul '%s' (l): %s", s, strerror(errno));
	if (!l || *endptr || !*s)
		die("select: %s must be a positive integer", name);
	return l;
}
void

onrunchange(void *info_, AudioQueueRef queue, AudioQueuePropertyID prop)
{
	struct state *info = info_;
	int res = pthread_mutex_lock(&info->runmut);
	if (res)
		die("select: pthread_mutex_lock onrunchange: %s", strerror(res));
	if ((res = pthread_cond_signal(&info->runcond)))
		die("select: pthread_cond_signal: %s", strerror(res));
	if ((res = pthread_mutex_unlock(&info->runmut)))
		die("select: pthread_mutex_unlock onrunchange: %s", strerror(res));
}

void
settty(struct termios *info)
{
	int r;
	do r = tcsetattr(TTY.fd,TCSAFLUSH,info); while ((r==-1) && (errno==EINTR));
	if (r == -1)
		die("select: tcsetattr: %s", strerror(errno));
}

void
onleavesig(int sig)
{
	// Reset terminal settings
	settty(&TTY.old);

	// Trigger default signal handler
	signal(sig, SIG_DFL);
	raise(sig);
}

void
onsigcont(int sig)
{
	// Reinstall all signal handlers
	signal(SIGCONT, onsigcont);
	for (int i=0; i<sizeof(LEAVESIGS)/sizeof(int); i++)
		signal(LEAVESIGS[i], onleavesig);

	// Set terminal to unbuffered mode
	settty(&TTY.new);
}

void
loadbuf(AudioQueueRef queue, AudioQueueBufferRef buf, struct state *info)
{
	size_t buflen = BUFLEN/sizeof(sample);
	size_t toload = (info->left > buflen) ? buflen : info->left;
	memcpy(buf->mAudioData, info->toplay, toload*sizeof(sample));
	buf->mAudioDataByteSize = toload*sizeof(sample);
	OSStatus osres = AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
	if (osres != noErr)
		die("select: AudioQueueEnqueueBuffer: OSStatus %d", osres);
	info->toplay += toload;
	info->left -= toload;
}

void
start(AudioQueueRef queue, AudioQueueBufferRef *bufs, sample *input,
	unsigned long len, unsigned long starti, struct state *info)
{
	if (starti == len) {
		fputs("(end)\n", stderr);
		return;
	}
	info->playing = 1;
	info->toplay = input+starti;
	info->left = info->total = len - starti;
	OSStatus osres = AudioQueueFlush(queue);
	if (osres != noErr)
		die("select: AudioQueueflush: OSStatus %d", osres);
	for (int i=0; i<BUFS; i++)
		if (info->left)
			loadbuf(queue, bufs[i], info);
	if ((osres = AudioQueueStart(queue, NULL)) != noErr)
		die("select: AudioQueueStart: OSStatus %d", osres);
	int res = pthread_cond_wait(&info->runcond, &info->runmut);
	if (res)
		die("select: pthread_cond_wait start: %s", strerror(res));
}

unsigned long
getframe(AudioQueueRef queue, int ch, unsigned long max)
{
	AudioTimeStamp t;
	OSStatus osres = AudioQueueGetCurrentTime(queue, NULL, &t, NULL);
	if (osres != noErr)
		die("select: AudioQueueGetCurrentTime: OSStatus %d", osres);
	return t.mSampleTime<0?0: t.mSampleTime*ch > max ? max : t.mSampleTime*ch;
}

unsigned long
stop(AudioQueueRef queue, unsigned long starti, struct state *info, int reap)
{
	unsigned long stopi = starti + getframe(queue, info->ch, info->total);
	info->playing = 0;
	// We have to unlock before stopping so that the loading threads (onneedpcm)
	// can finish
	int res = pthread_mutex_unlock(&info->lock);
	if (res)
		die("select: pthread_mutex_unlock stop: %s", strerror(res));
	OSStatus osres = AudioQueueStop(queue, true);
	if (osres != noErr)
		die("select: AudioQueueStop stop: OSStatus %d", osres);
	// NOTE: reap is ugly as hell - the original design assumed you could stop
	// completely within a callback, but the complete stopping requires callback
	// threads to finish, so we 'reap' the condition in the input thread instead.
	if (reap && ((res = pthread_cond_wait(&info->runcond, &info->runmut))))
			die("select: pthread_cond_wait stop: %s", strerror(res));
	info->reap = !reap;
	if ((res = pthread_mutex_lock(&info->lock)))
		die("select: pthread_mutex_lock stop: %s", strerror(res));
	return stopi;
}

void
onneedpcm(void *info_, AudioQueueRef queue, AudioQueueBufferRef buf)
{
	struct state *info = info_;
	int res = pthread_mutex_lock(&info->lock);
	if (res)
		die("select: pthread_mutex_lock (load audio): %s", strerror(res));
	if (info->playing && info->left)
		loadbuf(queue, buf, info);
	if (info->playing && !info->left) {
		unsigned long left = info->total - getframe(queue, info->ch, info->total);
		struct timespec ts = {
			.tv_sec = left / SAMPLERATE*info->ch,
			.tv_nsec=(left%(SAMPLERATE*info->ch))*1000000000ULL/(SAMPLERATE*info->ch)
		};
		if ((res = pthread_mutex_unlock(&info->lock)))
			die("select: pthread_mutex_unlock (load sleep): %s", strerror(res));
		do res = nanosleep(&ts, &ts); while ((res == -1) && (errno == EINTR));
		if ((res = pthread_mutex_lock(&info->lock)))
			die("select: pthread_mutex_lock (load wake): %s", strerror(res));
		if (info->playing && !info->left) {
			OSStatus osres = AudioQueueFlush(queue);
			if (osres != noErr)
				die("select: AudioQueueFlush: OSStatus %d", osres);
			stop(queue, 0, info, 0);
			fputs("(end)\n", stderr);
		}
	}
	if ((res = pthread_mutex_unlock(&info->lock)))
		die("select: pthread_mutex_unlock (load end): %s", strerror(res));
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		die("usage: select channels [count]");
	struct state info = { .reap=0 };
	info.ch = parseposordie(argv[1], "channels");
	unsigned long count = (argc>2) ? parseposordie(argv[2], "count") : ULONG_MAX;

	// Read stdin into a buffer
	sample *input = NULL;
	unsigned long len = 0;
	while (!feof(stdin)) {
		if (!(input = realloc(input, (len + BUFINCR)*sizeof(sample))))
			die("select: realloc: %s", strerror(errno));
		len += fread(input+len, sizeof(sample), BUFINCR, stdin);
		if (ferror(stdin))
			die("select: fread: %s", strerror(errno));
	}

	// Set up audio queue
	AudioStreamBasicDescription format = {
		.mSampleRate = SAMPLERATE,
		.mFormatID = kAudioFormatLinearPCM,
		.mFormatFlags = kAudioFormatFlagIsSignedInteger,
		.mBytesPerPacket = 2 * info.ch,
		.mFramesPerPacket = 1,
		.mBytesPerFrame = 2 * info.ch,
		.mChannelsPerFrame = info.ch,
		.mBitsPerChannel = 16
	};
	AudioQueueRef queue;
	OSStatus osres = AudioQueueNewOutput(&format, onneedpcm, &info, NULL, NULL, 0,
		&queue);
	if (osres != noErr)
		die("select: AudioQueueNewOutput: OSStatus %d", osres);
	AudioQueueBufferRef bufs[BUFS];
	for (int i=0; i<BUFS; i++)
		if ((osres = AudioQueueAllocateBuffer(queue, BUFLEN, &bufs[i])) != noErr)
			die("select: AudioQueueAllocateBuffer %d: OSStatus %d", i, osres);
	int res = pthread_mutex_init(&info.runmut, NULL);
	if (res)
		die("select: pthread_mutex_init run: %s", strerror(res));
	if ((res = pthread_mutex_lock(&info.runmut)))
		die("select: pthread_mutex_lock init: %s", strerror(res));
	if ((res = pthread_cond_init(&info.runcond, NULL)))
		die("select: pthread_cond_init: %s", strerror(res));
	if ((osres = AudioQueueAddPropertyListener(queue,
			kAudioQueueProperty_IsRunning, onrunchange, &info)) != noErr)
		die("select: AudioQueueAddPropertyListener: OSStatus %d", osres);

	// Open the tty unbuffered
	if ((TTY.fd = open("/dev/tty", O_RDONLY)) == -1)
		die("select: open /dev/tty: %s", strerror(errno));
	if (tcgetattr(TTY.fd, &TTY.old))
		die("select: tcgetattr: %s", strerror(errno));
	TTY.new = TTY.old;
	TTY.new.c_lflag &= ~(ECHO | ICANON);
	TTY.new.c_cc[VMIN] = 1;
	TTY.new.c_cc[VTIME] = 0;
	onsigcont(0);

	// Initialize state for playing, progress, etc.
	info.playing = 0;
	double fincr = 0;
	unsigned long starti=0, lrincr=info.ch*100, udincr=SAMPLERATE*info.ch, incr=0;
		if ((res = pthread_mutex_init(&info.lock, NULL)))
		die("select: pthread_mutex_init: %s", strerror(res));

	// Read commands from input
	ssize_t r;
	char c;
	while (((r = read(TTY.fd,&c,1)) > 0) || ((r == -1) && (errno == EINTR))) {
		if (r == -1)
			continue;
		// See above note... 'reap' is a wart that works, ideally redesigned out
		if (info.reap && ((res = pthread_cond_wait(&info.runcond, &info.runmut))))
				die("select: pthread_cond_wait stop: %s", strerror(res));
		info.reap = 0;
		if ((res = pthread_mutex_lock(&info.lock)))
			die("select: pthread_mutex_lock (input): %s", strerror(res));
		unsigned long i, mag;
		int dir = 0, wasplaying = 0;
		switch (c) {
		case ' ':
		case '/':
			fincr = incr = 0;
			if (info.playing) {
				i = stop(queue, starti, &info, 1);
				if (c == ' ')
					starti = i;
			} else
				start(queue, bufs, input, len, starti, &info);
			break;
		case 'A':
			dir = -1;
		case 'B':
			dir = dir ? dir : 1;
			udincr = fincr ? SAMPLERATE*info.ch*fincr :
				incr ? SAMPLERATE*info.ch*incr : udincr;
			fincr = incr = 0;
		case 'C':
			dir = dir ? dir : 1;
		case 'D':
			dir = dir ? dir : -1;
			lrincr = (!fincr && incr) ? incr*info.ch : lrincr;
			fincr = incr = 0;
			wasplaying = info.playing;
			if (wasplaying)
				starti = stop(queue, starti, &info, 1);
			mag = ((c == 'A') || (c == 'B')) ? udincr : lrincr;
			if ((dir == -1) && (mag >= starti)) {
				starti = 0;
				fputs("(start)\n", stderr);
			} else if ((dir == 1) && (mag >= (len - starti))) {
				starti = len;
				fputs("(end)\n", stderr);
			} else
				starti += dir*mag;
			if (wasplaying && (starti < len))
				start(queue, bufs, input, len, starti, &info);
			break;
		case '\n':
			fincr = incr = 0;
			i = starti + (info.playing ? getframe(queue, info.ch, info.total) : 0);
			printf("%lu\n", i/info.ch);
			if (!--count)
				return settty(&TTY.old), EXIT_SUCCESS;
			break;
		case '.':
			fincr = incr + 0.000001;  // ( ͡° ͜ʖ ͡°)
			incr = 1;  // now represents decimal spot
			break;
		default:
			if (isdigit(c)) {
				if (fincr)
					fincr += (c-'0') * pow(0.1, incr++);
				else
					incr = incr*10 + (c-'0');
			}
			break;
		}
		if ((res = pthread_mutex_unlock(&info.lock)))
			die("select: pthread_mutex_unlock (input): %s", strerror(res));
	}
	if (r == -1)
		die("select: read tty: %s", strerror(errno));
}
