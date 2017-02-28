#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <CoreMIDI/CoreMIDI.h>


/* printf to stderr with a newline and exit with nonzero status. */
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

void
onclientnotify(MIDINotification *msg, void *errname_)
{
	char *errname = errname_;
	if (((msg->messageID == kMIDIMsgObjectAdded) ||
				(msg->messageID == kMIDIMsgObjectRemoved)) &&
			((((MIDIObjectAddRemoveNotification *) msg)->childType &
				~kMIDIObjectType_ExternalMask) == kMIDIObjectType_Source))
		fprintf(stderr, "%s: source %s\n", errname,
			msg->messageID == kMIDIMsgObjectAdded ? "connected" : "disconnected");
	else if (msg->messageID == kMIDIMsgIOError)
		fprintf(stderr, "%s: MIDI IO error OSStatus %d\n", errname,
			((MIDIIOErrorNotification *) msg)->errorCode);
}

void
onmidi(MIDIPacketList *packets, void *errname_, void *_)
{
	char *errname = errname_;
	MIDIPacket *packet = &packets->packet[0];
	for (int packeti=0; packeti<packets->numPackets; packeti++) {
		if (packet->length < 3)
			continue;
		unsigned char status = packet->data[0] & 0xF0;
		unsigned char data1 = packet->data[1];
		unsigned char data2 = packet->data[2];
		if ((status == 0x80) || ((status == 0x90) && (data2 == 0)))
			printf("Note Off\t%u\n", data1);
		else if (status == 0x90)
			printf("Note On\t%u\n", data1);
		if (ferror(stdout))
			die("%s: printf: %s", errname, strerror(errno));
		packet = MIDIPacketNext(packet);
	}
}

int
main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	if (argc != 2)
		die("usage: midi id");
	char *endptr;
	errno = 0;
	long id = strtol(argv[1], &endptr, 10);
	if (errno)
		die("midi: strtol: %s", strerror(errno));
	if (*endptr != '\0')
		die("midi: id must be an integer");

	// Get MIDI source
	MIDIObjectRef rawsource;
	MIDIObjectType rawtype;
	OSStatus res = MIDIObjectFindByUniqueID(id, &rawsource, &rawtype);
	if (res == kMIDIObjectNotFound)
		die("midi: object with id %ld not found", id);
	else if (res != noErr)
		die("midi: MIDIObjectFindByUniqueID %ld: OSStatus %d", id, res);
	MIDIObjectType type = rawtype & ~kMIDIObjectType_ExternalMask;
	if (type != kMIDIObjectType_Source)
		die("midi: object with id %ld not a source but '%s'", id,
			(type == kMIDIObjectType_Device) ? "device" :
			(type == kMIDIObjectType_Entity) ? "entity" :
			(type == kMIDIObjectType_Destination) ? "destination" :
			(rawtype == kMIDIObjectType_Other) ? "other" : "unknown");
	MIDIEndpointRef source = rawsource;

	// Check if offline
	SInt32 offline;
	if (((res = MIDIObjectGetIntegerProperty(source, kMIDIPropertyOffline,
			&offline)) != noErr) && (res != kMIDIUnknownProperty))
		die("midi: MIDIObjectGetIntegerProperty kMIDIPropertyOffline %ld: "
			"OSStatus %d", id, res);
	if (offline)
		fprintf(stderr, "midi %lu: source is disconnected\n", id);

	// Listen to the source
	char clientname[30];
	snprintf(clientname, sizeof(clientname), "midi %d", getpid());
	CFStringRef clientnamecf = CFStringCreateWithCString(NULL, clientname,
		kCFStringEncodingUTF8);
	MIDIClientRef client;
	char errname[30];
	snprintf(errname, sizeof(errname), "midi %ld", id);
	res = MIDIClientCreate(clientnamecf, (MIDINotifyProc)onclientnotify, errname,
		&client);
	CFRelease(clientnamecf);
	if (res != noErr)
		die("midi: MIDIClientCreate: OSStatus %d", res);
	MIDIPortRef port;
	if ((res = MIDIInputPortCreate(client, CFSTR("in"), (MIDIReadProc)onmidi,
			errname, &port)) != noErr)
		die("midi: MIDIInputPortCreate: OSStatus %d", res);
	if ((res = MIDIPortConnectSource(port, source, NULL)) != noErr)
		die("midi: MIDIPortConnectSource: OSStatus %d", res);
	CFRunLoopRun();
}
