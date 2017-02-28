#include <stdarg.h>
#include <stdio.h>

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

int
main(int argc, char **argv)
{
	ItemCount sources = MIDIGetNumberOfSources();
	for (ItemCount sourcei=0; sourcei<sources; sourcei++) {
		MIDIEndpointRef source = MIDIGetSource(sourcei);
		if (source == (MIDIEndpointRef)NULL)
			die("midils: MIDIGetSource %lu", sourcei);
		SInt32 id;
		OSStatus res = MIDIObjectGetIntegerProperty(source, kMIDIPropertyUniqueID,
			&id);
		if (res != noErr)
			die("midils: MIDIObjectGetIntegerProperty kMIDIPropertyUniqueID %lu: "
				"OSStatus %d", sourcei, res);
		CFStringRef namecf;
		if ((res = MIDIObjectGetStringProperty(source, kMIDIPropertyDisplayName,
				&namecf)) != noErr)
			die("midils: MIDIObjectGetStringProperty kMIDIPropertyDisplayName for "
				"source %ld: OSStatus %d", (long)id, res);
		char name[256];
		if (!CFStringGetCString(namecf, name, sizeof(name), kCFStringEncodingUTF8))
			die("midils: CFStringGetCString for source %ld (weird characters or name "
				"too long)", (long)id);
		printf("%ld\t%s\n", (long)id, name);
	}
}
