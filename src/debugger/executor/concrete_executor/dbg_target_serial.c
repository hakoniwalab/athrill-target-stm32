#include "concrete_executor/target/dbg_target_serial.h"
#include <stdio.h>

Std_ReturnType dbg_serial_in(uint8 c, uint8 d)
{
	(void)c;
	(void)d;
	return STD_E_INVALID;
}
bool dbg_serial_getchar(uint8 c, uint8 *d)
{
	(void)c;
	(void)d;
	return FALSE;
}
bool dbg_serial_putchar(uint8 c, uint8 d)
{
	FILE *f = c ? stderr : stdout;

	fputc(d, f);
	fflush(f);
	return TRUE;
}
bool dbg_serial_getchar_file(uint8 c, uint8 *d)
{
	return dbg_serial_getchar(c, d);
}
bool dbg_serial_putchar_file(uint8 c, uint8 d)
{
	return dbg_serial_putchar(c, d);
}
void dbg_serial_flush_file(uint8 c)
{
	(void)c;
	fflush(stdout);
	fflush(stderr);
}
