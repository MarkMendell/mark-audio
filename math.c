#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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

double add(double a, double b) { return a + b; }
double div_(double a, double b) {
	if (b == 0)
		die("math: div by 0");
	return a / b;
}
double mult(double a, double b) { return a * b; }


struct token {
	char *s;
	union {
		double (*f1)(double);
		double (*f2)(double, double);
		double x;
	} val;
	unsigned int args;
};

struct token TOKENS[] = {
	{ "e", { .x = 2.71828182845904523536028747135266250 }, 0 },
	{ "pow", { .f2 = pow }, 2 },
	{ "rnd", { .f1 = round }, 1 },
	{ "+", { .f2 = add }, 2 },
	{ "*", { .f2 = mult }, 2 },
	{ "/", { .f2 = div_ }, 2 }
};


int
main(int argc, char **argv)
{
	int truncate = (argc > 1) && (!strcmp(argv[1], "-i"));
	if (argc > 50)
		die("math: lol");

	double stack[50];
	int len = 0;
	for (int argi = 1+truncate; argi<argc; argi++) {
		int found = 0;
		// Starting with digits or - ==> parse as a double
		if (isdigit(argv[argi][0]) || (argv[argi][0] == '-')) {
			char *endptr;
			errno = 0;
			if ((stack[len++] = strtod(argv[argi], &endptr)) == 0) {
				if (endptr == argv[argi])
					die("math: bad double '%s'", argv[argi]);
				else if ((errno == ERANGE) || (errno == EINVAL))
					die("math: strtod: %s", strerror(errno));
			}
			continue;
		}
		// Not a double so check for matching token
		for (int tokeni=0; tokeni<sizeof(TOKENS)/sizeof(struct token); tokeni++) {
			struct token tok = TOKENS[tokeni];
			if (!strcmp(argv[argi], tok.s)) {
				found = 1;
				// Constant; add to the stack
				if (tok.args == 0)
					stack[len++] = (double)tok.val.x;
				// Function without enough args
				else if (tok.args > len)
					die("math: '%s' needs %u args, only have %u", tok.s, tok.args, len);
				// Function; replace args with result on stack
				else {
					double d;
					if (tok.args == 1)
						d = tok.val.f1(stack[len-1]);
					else if (tok.args == 2)
						d = tok.val.f2(stack[len-1], stack[len-2]);
					else
						die("math: huh");
					stack[(len -= (tok.args-1)) - 1] = d;
				}
				break;
			}
		}
		if (!found)
			die("math: no matching token for '%s'", argv[argi]);
	}

	if (len != 1)
		die("math: stack ended with %u elements", len);
	if (truncate)
		printf("%ld\n", (long)stack[0]);
	else
		printf("%lf\n", stack[0]);
}
