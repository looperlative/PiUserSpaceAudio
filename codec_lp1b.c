/*
 * Copyright 2025 - Robert Amstadt
 *
 * This file is part of PiUserSpaceAudio.
 *
 * PiUserSpaceAudio is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * PiUserSpaceAudio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with PiUserSpaceAudio. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "linux/i2c.h"
#include "linux/i2c-dev.h"
#include "i2c/smbus.h"

#include "bcmhw.h"
#include "codec_lp1b.h"

int codec_lp1b_init(void)
{
    /*
     * Clock not needed for Looperlative CS4270 because it
     * uses a dedicated external xtal oscillator.
     */
    //bcmhw_set_i2s_clk(48000);

    // Reset codec and LED driver.
    bcmhw_gpio_select(17, GPIO_FUNC_OUTPUT);
    bcmhw_gpio_set(17, 0);
    usleep(100);
    bcmhw_gpio_set(17, 1);
    writel(GPSET0, 1 << 17);
    usleep(100);

    // Configure codec
    int i2cfd = open("/dev/i2c-1", O_RDWR);
    if (i2cfd < 0)
    {
	printf("Couldn't open I2C bus device\n");
	return -1;
    }

    if (ioctl(i2cfd, I2C_SLAVE, 0x48) < 0)
    {
	printf("Couldn't set I2C slave address\n");
	return -1;
    }

    i2c_smbus_write_byte_data(i2cfd, 0x02, 0x23);

    int rv = i2c_smbus_read_byte_data(i2cfd, 0x01);
    printf("0x48:0x01 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x02);
    printf("0x48:0x02 = %x\n", rv);

    i2c_smbus_write_byte_data(i2cfd, 0x03, 0x00);
    i2c_smbus_write_byte_data(i2cfd, 0x04, 0x09);
    i2c_smbus_write_byte_data(i2cfd, 0x05, 0x60);
    i2c_smbus_write_byte_data(i2cfd, 0x06, 0x00);
    i2c_smbus_write_byte_data(i2cfd, 0x07, 0x00);
    i2c_smbus_write_byte_data(i2cfd, 0x08, 0x00);
    i2c_smbus_write_byte_data(i2cfd, 0x02, 0x00);

    rv = i2c_smbus_read_byte_data(i2cfd, 0x01);
    printf("0x48:0x01 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x02);
    printf("0x48:0x02 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x03);
    printf("0x48:0x03 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x04);
    printf("0x48:0x04 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x05);
    printf("0x48:0x05 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x06);
    printf("0x48:0x06 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x07);
    printf("0x48:0x07 = %x\n", rv);
    rv = i2c_smbus_read_byte_data(i2cfd, 0x08);
    printf("0x48:0x08 = %x\n", rv);

    close(i2cfd);

    return 0;
}
