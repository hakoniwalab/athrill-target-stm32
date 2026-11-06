/*
 * Pixhawk 6C board attachment, stage 1.
 *
 * The H743 itself is selected before CPU reset by board_profile.c.  Sensor,
 * storage and actuator attachments belong to later stages; keeping this
 * function explicit prevents fmu-v6c from falling through to fmu-v5 wiring.
 */
#include "fmu_v6c.h"

void fmu_v6c_attach(void)
{
}
