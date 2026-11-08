#include "concrete_executor/target/dbg_target_serial.h"
#include "device_tree.h"
#include "std_errno.h"
#include <stdio.h>

static std_bool serial_escape;

/* The debugger's serial input to the console UART; \\n and \\r stand for newline and return. */
Std_ReturnType dbg_serial_in(uint8 c, uint8 d)
{
	Std_ReturnType err;

	if (c != 0U) {
		return STD_E_INVALID;
	}
	if (serial_escape) {
		serial_escape = FALSE;
		if (d == 'n') {
			d = '\n';
		}
		else if (d == 'r') {
			d = '\r';
		}
		else {
			err = stm32_device_tree_console_input('\\') ? STD_E_OK : STD_E_LIMIT;
			if (err != STD_E_OK) {
				return err;
			}
		}
	}
	else if (d == '\\') {
		serial_escape = TRUE;
		return STD_E_OK;
	}
	return stm32_device_tree_console_input(d) ? STD_E_OK : STD_E_LIMIT;
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
