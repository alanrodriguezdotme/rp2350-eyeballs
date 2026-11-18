// PCF85063 real-time clock. The pendant uses only its one spare byte of
// memory, which the clock's backup supply keeps while the board is off.
#ifndef PCF85063_H
#define PCF85063_H

#include <stdbool.h>
#include <stdint.h>

// Both return false if the chip did not answer.
bool pcf85063_read_ram(uint8_t *value);
bool pcf85063_write_ram(uint8_t value);

#endif
