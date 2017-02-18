#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>


#define MAX_SAMPLE (1 << 15)
typedef int16_t sample;
static uint32_t SAMPLE_RATE = 44100;
static uint32_t BUFFER_LEN = 44100;


int 
main(int argc, char **argv)
{
  double freq;
  if ((argc != 2) || (!(freq = strtod(argv[1], NULL)) && (errno == EINVAL))) {
    fputs("usage: tri frequency\n", stderr);
    exit(EXIT_FAILURE);
  }
  double phase_percent = 0;
  double phase_percent_incr = freq / SAMPLE_RATE;
  sample buffer[BUFFER_LEN];
  while (1) {
    for (unsigned int i=0; i<BUFFER_LEN; i++) {
      if (phase_percent < 0.25) {
        buffer[i] = 4*phase_percent * MAX_SAMPLE;
      } else if (phase_percent < 0.75) {
        buffer[i] = (2 - 4*phase_percent) * MAX_SAMPLE;
      } else {
        buffer[i] = (4*phase_percent - 4) * MAX_SAMPLE;
      }
      phase_percent = fmod(phase_percent + phase_percent_incr, 1.0);
    }
    fwrite(&buffer, sizeof(sample), BUFFER_LEN, stdout);
  }
}
