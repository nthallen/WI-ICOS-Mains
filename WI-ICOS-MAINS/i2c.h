#ifndef I2C_H_INCLUDED
#define I2C_H_INCLUDED
#include "subbus.h"

#define I2C_BASE_ADDR 0x10
#define I2C_ADC_STATUS_OFFSET 0x00
#define I2C_ADC_STATUS_NREGS 1
#define I2C_ADC_OFFSET (I2C_ADC_STATUS_OFFSET+I2C_ADC_STATUS_NREGS)
#define I2C_ADC_NREGS 11
#define I2C_NREGS (I2C_ADC_OFFSET+I2C_ADC_NREGS)
#define I2C_HIGH_ADDR (I2C_BASE_ADDR+I2C_NREGS-1)
#define I2C_ENABLE_DEFAULT true

extern subbus_driver_t sb_i2c;
void i2c_enable(bool value);

#endif
