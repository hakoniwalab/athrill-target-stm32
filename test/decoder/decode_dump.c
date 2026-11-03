/*
 * Prints the generated decoders' instruction name for each Thumb instruction
 * given as hex (4 digits for a 16-bit instruction, 8 digits for a 32-bit one,
 * first halfword first): from the arguments, or from stdin one per line.
 * Used by athrill-arm-common's tools/crosscheck_t32_decoder32.py and the
 * decoder test.
 */
#include <stdio.h>
#include <string.h>
#include "t32_decode16.h"
#include "t32_decode32.h"

static void dump(const char *text)
{
	unsigned long value;
	size_t digits = strspn(text, "0123456789abcdefABCDEF");

	if (sscanf(text, "%lx", &value) != 1) {
		return;
	}
	if (digits > 4) {
		printf("%08lx %s\n", value, t32_decode32_names[t32_decode32((uint32_t)value)]);
	}
	else {
		printf("%04lx %s\n", value, t32_decode16_names[t32_decode16((uint16_t)value)]);
	}
}

int main(int argc, char *argv[])
{
	char line[64];
	int i;

	if (argc > 1) {
		for (i = 1; i < argc; i++) {
			dump(argv[i]);
		}
		return 0;
	}
	while (fgets(line, sizeof(line), stdin) != NULL) {
		dump(line);
	}
	return 0;
}
