#include "pcf85063.h"

#include "DEV_Config.h"
#include "hardware/i2c.h"

#define PCF85063_ADDR 0x51
#define PCF85063_REG_RAM 0x03

bool pcf85063_read_ram(uint8_t *value)
{
    uint8_t reg = PCF85063_REG_RAM;
    if (i2c_write_blocking(I2C_PORT, PCF85063_ADDR, &reg, 1, true) != 1) {
        return false;
    }
    return i2c_read_blocking(I2C_PORT, PCF85063_ADDR, value, 1, false) == 1;
}

bool pcf85063_write_ram(uint8_t value)
{
    uint8_t data[2] = { PCF85063_REG_RAM, value };
    return i2c_write_blocking(I2C_PORT, PCF85063_ADDR, data, 2, false) == 2;
}
