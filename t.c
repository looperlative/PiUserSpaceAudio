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
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <time.h>
#include "bcmhw.h"
#include "codec_lp1b.h"

static int rx_errors = 0;
static int tx_errors = 0;
static int tx_counter = 0;
static int prefill_count = 0;

pid_t gettid(void);

void *audio_thread(void *arg)
{
    struct sched_param sparam;
    sparam.sched_priority = 99;
    sched_setscheduler(gettid(), SCHED_FIFO, &sparam);

    /* Remove RAM standby mode.  Doc isn't clear on whether this is required for the Pi Zero,
       but we don't want to take a chance.
    */
    writel(PCM_CS_A, PCM_CS_STBY);
    usleep(1000);

    /*
     * ALT0 function for each:
     * GPIO18 - CLK, GPIO19 - FS, GPIO20 - DIN, GPIO21 - DOUT
     */
    bcmhw_gpio_select(18, GPIO_FUNC_ALT0);
    bcmhw_gpio_select(19, GPIO_FUNC_ALT0);
    bcmhw_gpio_select(20, GPIO_FUNC_ALT0);
    bcmhw_gpio_select(21, GPIO_FUNC_ALT0);

    /* 32-bit data, 1-bit shift for I2S */
    writel(PCM_RXC_A, (PCM_RXC_CH1WEX | PCM_RXC_CH1EN | PCM_RXC_CH1POS(1) | PCM_RXC_CH1WID(8) |
		       PCM_RXC_CH2WEX | PCM_RXC_CH2EN | PCM_RXC_CH2POS(33) | PCM_RXC_CH2WID(8)));
    writel(PCM_TXC_A, (PCM_TXC_CH1WEX | PCM_TXC_CH1EN | PCM_TXC_CH1POS(1) | PCM_TXC_CH1WID(8) |
		       PCM_TXC_CH2WEX | PCM_TXC_CH2EN | PCM_TXC_CH2POS(33) | PCM_TXC_CH2WID(8)));

    /* 64-bit total frame length, 32-bit frame sync length */
    writel(PCM_MODE_A,
	   PCM_MODE_CLK_DIS | PCM_MODE_FSI | PCM_MODE_CLKI |
	   PCM_MODE_FLEN(63) | PCM_MODE_FSLEN(32));
    writel(PCM_MODE_A,
	   PCM_MODE_CLK_DIS | PCM_MODE_FSI | PCM_MODE_CLKI | PCM_MODE_FSM | PCM_MODE_CLKM |
	   PCM_MODE_FLEN(63) | PCM_MODE_FSLEN(32));
    writel(PCM_MODE_A,
	   PCM_MODE_FSI | PCM_MODE_CLKI | PCM_MODE_FSM | PCM_MODE_CLKM |
	   PCM_MODE_FLEN(63) | PCM_MODE_FSLEN(32));

    writel(PCM_CS_A, readl(PCM_CS_A) | PCM_CS_TXTHR_LVL1);

    /* Clear FIFOs */
    writel(PCM_CS_A, readl(PCM_CS_A) | PCM_CS_TXCLR | PCM_CS_RXCLR);
    usleep(1000);

    /* Enable rx and tx */
    writel(PCM_CS_A, readl(PCM_CS_A) | PCM_CS_TXON | PCM_CS_RXON | PCM_CS_RXSEX);

    /* Enable I2S */
    writel(PCM_CS_A, readl(PCM_CS_A) | PCM_CS_EN | PCM_CS_RXSEX);

    for (int i = 1; (readl(PCM_CS_A) & PCM_CS_TXW) != 0 && i <= 64; i++)
    {
	writel(PCM_FIFO_A, 0);
	prefill_count = i;
    }

    int first_found = 0;

    while (1)
    {
	/*
	 * Read FIFO if data available and the send to TX FIFO. Keep count of RX and TX errors.
	 */
	unsigned long status = readl(PCM_CS_A);
	writel(PCM_CS_A, status);

	if (status & PCM_CS_RXERR)
	    rx_errors++;
	if (status & PCM_CS_TXERR)
	    tx_errors++;
	if (status & PCM_CS_RXD)
	{
	    first_found = 1;

	    long data = readl(PCM_FIFO_A);
	    writel(PCM_FIFO_A, data);
	    tx_counter++;
	}
	else if (!first_found && (status & PCM_CS_TXW) != 0)
	    writel(PCM_FIFO_A, 0);
    }
}

int main()
{
    /*
     * Disable run time limit on real-time thread.  By default, Linux
     * doesn't allow a real-time thread to comsume 100% of a CPU, but
     * in our case, we absolutely must have the audio thread consume
     * 100% of one core.
     */
    FILE *fp = fopen("/proc/sys/kernel/sched_rt_runtime_us", "w");
    if (fp == NULL)
    {
	fprintf(stderr, "Couldn't modify rt runtime limit\n");
	return 1;
    }
    fprintf(fp, "-1");
    fclose(fp);

    /*
     * Set priority of the main thread to something reasonable.
     */
    struct sched_param sparam;
    sparam.sched_priority = -20;
    sched_setscheduler(getpid(), SCHED_FIFO, &sparam);

    /*
     * Prepare the hardware library code for use.  Mostly needs
     * to mmap device registers.
     */
    if (bcmhw_init() < 0)
	exit(1);

    /*
     * CODEC specific initialization.
     */
    if (codec_lp1b_init() < 0)
	exit(1);

    /*
     * Start audio thread.
     */
    pthread_t rt_tid;

    pthread_create(&rt_tid, NULL, audio_thread, NULL);

    while (1)
    {
	printf("tx sends %d, tx errors %d, rx errors %d, prefill %d\n",
	       tx_counter, tx_errors, rx_errors, prefill_count);
	sleep(1);
    }
}
