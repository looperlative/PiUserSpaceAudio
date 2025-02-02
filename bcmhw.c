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
#include <sys/mman.h>
#include <string.h>
#include "bcmhw.h"

/*
 * 0: Pi Zero
 * 1: Pi3, Pi Zero 2
 * 2: Pi4
 * 3: Pi5
 */
unsigned long long base_addresses[4] =
{
    0x20000000, 0x3f000000, 0xfe000000, 0x107c000000
};

int base_clocks[4] =
{
    19200000, 19200000, 54000000, 54000000
};

int base_clock = 0;

void *base_address = 0;
void *clks_base;
void *gpio_base;
void *pcm_base;

static int bcmhw_get_hw_type(void)
{
    int fd = open("/sys/firmware/devicetree/base/model", O_RDONLY);
    if (fd < 0)
	return -1;

    char buffer[200];
    int n = read(fd, buffer, sizeof(buffer));
    if (n < 1 || n >= sizeof(buffer))
	return -1;

    close(fd);

    if (strncmp(buffer, "Raspberry Pi Zero 2", 19) == 0)
    {
	printf("Pi Zero 2\n");
	return 1;
    }
    else if (strncmp(buffer, "Raspberry Pi 4", 14) == 0)
    {
	printf("Pi 4\n");
	return 2;
    }
    else
	return -1;
}

void bcmhw_gpio_select(int gpio, int function)
{
    if (gpio < 0 || gpio > 53 || function < 0 || function > 7)
	return;

    int bank = gpio / 10;
    int shift = (gpio % 10) * 3;
    unsigned long addr = (GPFSEL0 + (bank * 4));

    unsigned long previous = readl(addr) & ~(7 << shift);
    writel(addr, previous | (function << shift));
}

void bcmhw_gpio_print(int gpio)
{
    if (gpio < 0 || gpio > 53)
	return;

    int bank = gpio / 10;
    int shift = (gpio % 10) * 3;
    unsigned long addr = (GPFSEL0 + (bank * 4));
    unsigned long masked = readl(addr) & (7 << shift);
    printf("gpio %d (bank %d, shift %d): %08x from %08x\n", gpio, bank, shift, masked, readl(addr));
}

void bcmhw_gpio_set(int gpio, int on)
{
    if (gpio < 0 || gpio > 31)
	return;

    if (on)
	writel(GPSET0, 1 << gpio);
    else
	writel(GPCLR0, 1 << gpio);
}

#define BCMHW_ADDR(x)	(off_t) ((char *) base_address + x)

int bcmhw_init(void)
{
    int hwtype = bcmhw_get_hw_type();
    if (hwtype < 0)
    {
	printf("Unknown platform type\n");
	return -1;
    }

    base_address = (void *) (unsigned long) base_addresses[hwtype];
    base_clock = base_clocks[hwtype];

    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0)
    {
	perror("open failed");
	return -1;
    }

    clks_base = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, BCMHW_ADDR(0x101000));
    if (clks_base == MAP_FAILED)
    {
	perror("clks mmap failed");
	close(mem_fd);
	return -1;
    }

    gpio_base = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, BCMHW_ADDR(0x200000));
    if (gpio_base == MAP_FAILED)
    {
	perror("gpio mmap failed");
	close(mem_fd);
	return -1;
    }

    pcm_base = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, mem_fd, BCMHW_ADDR(0x203000));
    if (pcm_base == MAP_FAILED)
    {
	perror("pcm mmap failed");
	close(mem_fd);
	return -1;
    }

    close(mem_fd);

    unsigned long addr = CM_GPCTL(CM_I2S_CLOCK);
    printf("I2S CLK: %08x (%d): %08x %08x\n", addr, CM_I2S_CLOCK, readl(addr), readl(addr + 4));
    printf("I2S CS:    %08x\n", readl(PCM_CS_A));
    printf("I2S MODE:  %08x\n", readl(PCM_MODE_A));
    printf("I2S RXC:   %08x\n", readl(PCM_RXC_A));
    printf("I2S TXC:   %08x\n", readl(PCM_RXC_A));
    printf("I2S DREQ:  %08x\n", readl(PCM_DREQ_A));
    exit(1);

    return 0;
}

/*
 * Set internal I2S clock.  Note that this isn't required if external clock is used.
 */
int bcmhw_set_i2s_clk(int samplerate)
{
    // For now only allow 48000 sample rate.  Changes required for other rates.
    if (samplerate != 48000)
	return -1;

    unsigned long addr = CM_GPCTL(CM_I2S_CLOCK);
    printf("before: %08x (%d): %08x %08x\n", addr, CM_I2S_CLOCK, readl(addr), readl(addr + 4));

    int divi = (base_clock / 64) / 48000;
    int divf = (((base_clock / 64) % 48000) * 4096) / 48000;

    writel(CM_GPDIV(CM_I2S_CLOCK), CM_GPDIV_PASSWD | CM_GPDIV_DIVI(divi) | CM_GPDIV_DIVF(divf));
    writel(CM_GPCTL(CM_I2S_CLOCK), CM_GPCTL_PASSWD | CM_GPCTL_SRC_OSC | CM_GPCTL_MASH_1STAGE);
    writel(CM_GPCTL(CM_I2S_CLOCK),
	   readl(CM_GPCTL(CM_I2S_CLOCK)) | CM_GPCTL_PASSWD | CM_GPCTL_ENABLE);

    printf(" after: %08x (%d): %08x %08x\n", addr, CM_I2S_CLOCK, readl(addr), readl(addr + 4));

    return 0;
}
