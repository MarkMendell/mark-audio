#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define BUFLEN 44100
typedef int16_t sample;


int 
main(int argc, char **argv)
{
	int filec = 0;
	while ((fcntl(++filec + 2, F_GETFD) != -1))
		;
  FILE *files[filec];
	if ((files[0] = fdopen(0, "r")) == NULL) {
		perror("add: fdopen 0");
		return EXIT_FAILURE;
	}
  for (int i=1; i<filec; i++)
    if ((files[i] = fdopen(i + 2, "r")) == NULL) {
			fprintf(stderr, "add: fdopen %d: %s\n", i+2, strerror(errno));
			return EXIT_FAILURE;
    }

  sample readbuf[BUFLEN];
  sample sumbuf[BUFLEN];
	size_t maxread;
	do {
		memset(sumbuf, 0, sizeof(readbuf));
		maxread = 0;
    for (int filei=0; filei<filec; filei++) {
      size_t read = fread(readbuf, sizeof(sample), BUFLEN, files[filei]);
			if (ferror(files[filei])) {
				char *errmsg = strerror(errno);
				fprintf(stderr, "add: fread %d: %s\n", fileno(files[filei]), errmsg);
				for (int i=1; i<filec; i++)
					fclose(files[i]);
				return EXIT_FAILURE;
			}
			if (read > maxread)
				maxread = read;
      for (size_t i=0; i<read; i++)
				sumbuf[i] += readbuf[i];
    }
    fwrite(sumbuf, sizeof(sample), maxread, stdout);
		if (ferror(stdout)) {
			perror("add: fwrite");
			for (int i=1; i<filec; i++)
				fclose(files[i]);
			return EXIT_FAILURE;
		}
  } while (maxread);
	
  for (int i=1; i<filec; i++)
    fclose(files[i]);
}
