#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>


size_t BUFLEN = 4096;
typedef int16_t sample;
typedef int32_t multsafesample;
sample MAXSAMPLE = INT16_MAX;


int 
main(void)
{
	int filec = 0;
	while ((fcntl(++filec + 2, F_GETFD) != -1));
  FILE *files[filec];
	if ((files[0] = fdopen(0, "r")) == NULL) {
		perror("mult: fdopen 0");
		return EXIT_FAILURE;
	}
  for (int i=1; i<filec; i++)
    if ((files[i] = fdopen(i + 2, "r")) == NULL) {
			fprintf(stderr, "mult: fdopen %d: %s\n", i+2, strerror(errno));
			return EXIT_FAILURE;
    }

  sample readbuf[BUFLEN];
  sample productbuf[BUFLEN];
	size_t minread;
	do {
    for (int i=0; i<BUFLEN; i++)
      productbuf[i] = MAXSAMPLE;
		size_t maxread = 0;
    minread = BUFLEN;
    for (int filei=0; filei<filec; filei++) {
      size_t read = fread(readbuf, sizeof(sample), BUFLEN, files[filei]);
			if (ferror(files[filei])) {
				char *errmsg = strerror(errno);
				fprintf(stderr, "mult: fread %d: %s\n", fileno(files[i]), errmsg);
				for (int i=1; i<filec; i++)
					fclose(files[i]);
				return EXIT_FAILURE;
			}
      if (read < minread)
				minread = read;
			if (read > maxread)
				maxread = read;
      for (size_t i=0; i<read; i++) {
        multsafesample product = (multsafesample)productbuf[i] * (multsafesample)readbuf[i];
        productbuf[i] = product / (double)MAXSAMPLE;
      }
    }
    fwrite(productbuf, sizeof(sample), maxread, stdout);
		if (ferror(stdout)) {
			perror("mult: fwrite");
			for (int i=1; i<filec; i++)
				fclose(files[i]);
			return EXIT_FAILURE;
		}
  } while (minread == BUFLEN);
  
  for (int i=1; i<filec; i++)
    fclose(files[i]);
}
