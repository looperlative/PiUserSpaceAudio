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

#define _GNU_SOURCE
#include <sched.h>

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

#include "bcmhw.h"
#include "codecs.h"
#include "pusa.h"

pid_t gettid(void);

struct pusa_codec_s
{
    char *name;
    int (*init)(void);
} pusa_codecs[] =
{
    { "lp1b", 		codec_lp1b_init },
    { "pisound", 	codec_pisound_init },
    { NULL, NULL }
};

int pusa_rx_errors = 0;
int pusa_tx_errors = 0;
int pusa_rx_counter = 0;
int pusa_tx_counter = 0;
int pusa_tx_counter_at_first_found = 0;
int pusa_prefill_count = 0;
int pusa_done = 0;
pusa_audio_handler_t pusa_audio_handler = NULL;

void *pusa_audio_thread(void *arg)
{
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(3, &cpus);
    sched_setaffinity(0, sizeof(cpus), &cpus);

    struct sched_param sparam;
    sparam.sched_priority = 99;
    sched_setscheduler(gettid(), SCHED_FIFO, &sparam);

    /*
     * It is possible that we were running before and never stopped.  There is
     * no reset bit, but we can stop the interface, wait, and then enable it again.
     */
    writel(PCM_CS_A, 0);
    usleep(10000);

    writel(PCM_CS_A, PCM_CS_EN);
    usleep(10000);

    /*
     * Remove RAM standby mode.  Doc isn't clear on whether this is required for the Pi Zero,
     * but we don't want to take a chance.
     */
    writel(PCM_CS_A, PCM_CS_STBY | PCM_CS_EN);
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

    writel(PCM_CS_A, readl(PCM_CS_A) | PCM_CS_TXTHR_LVL1 | PCM_CS_RXTHR_LVL1);

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
	pusa_prefill_count = i;
    }

    while (1)
    {
	/*
	 * Wait for read FIFO to have data.  Meanwhile, make certain that
	 * write FIFO is kept full.
	 */
	unsigned long status = readl(PCM_CS_A);
	writel(PCM_CS_A, status);

	if (status & PCM_CS_RXERR)
	    pusa_rx_errors++;
	if (status & PCM_CS_TXERR)
	    pusa_tx_errors++;
	if (status & PCM_CS_RXR)
	{
	    break;
	}
	else if ((status & PCM_CS_TXW) != 0)
	{
	    writel(PCM_FIFO_A, 0);
	    pusa_tx_counter++;
	}
    }

    pusa_tx_counter_at_first_found = pusa_tx_counter;
    pusa_tx_counter = 0;

    while (!pusa_done)
    {
	/*
	 * Read FIFO if data available and the send to TX FIFO. Keep count of RX and TX errors.
	 */
	unsigned long status = readl(PCM_CS_A);
	writel(PCM_CS_A, status);

	if (status & PCM_CS_RXERR)
	    pusa_rx_errors++;
	if (status & PCM_CS_TXERR)
	    pusa_tx_errors++;
	if (status & PCM_CS_RXR)
	{
	    int data[2];

	    data[0] = readl(PCM_FIFO_A);
	    data[1] = readl(PCM_FIFO_A);

	    if (pusa_audio_handler != NULL)
		pusa_audio_handler(data, 2);

	    writel(PCM_FIFO_A, data[0]);
	    writel(PCM_FIFO_A, data[1]);

	    pusa_tx_counter++;
	    pusa_rx_counter++;
	}
    }
}

struct pusa_codec_s *pusa_find_codec(const char *name)
{
    struct pusa_codec_s *codec = pusa_codecs;
    while (codec->name)
    {
	if (strcmp(name, codec->name) == 0)
	    return codec;

	codec++;
    }

    return NULL;
}

int pusa_init(const char *codec_name, pusa_audio_handler_t func)
{
    pusa_audio_handler = func;

    /*
     * Disable run time limit on real-time thread.  By default, Linux
     * doesn't allow a real-time thread to comsume 100% of a CPU, but
     * in our case, we absolutely must have the audio thread consume
     * 100% of one core.
     *
     * If you don't do this, Linux interrupts the process once a second
     * and pauses it for many milliseconds.  I don't know the exact amount
     * of time, but by experimentation, it is long enough to drain the
     * hardware FIFO.
     */
    FILE *fp = fopen("/proc/sys/kernel/sched_rt_runtime_us", "w");
    if (fp == NULL)
	return -1;

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
	return -1;

    /*
     * CODEC specific initialization.
     */
    struct pusa_codec_s *codec = pusa_find_codec(codec_name);
    if (codec == NULL || (*codec->init)() < 0)
	return -1;

    /*
     * Start audio thread.
     */
    pthread_t rt_tid;

    pthread_create(&rt_tid, NULL, pusa_audio_thread, NULL);
    return 0;
}

void pusa_print_stats(void)
{
    printf("tx %d (%d), rx %d, tx errors %d, rx errors %d, prefill %d\n",
	   pusa_tx_counter, pusa_tx_counter_at_first_found, pusa_rx_counter,
	   pusa_tx_errors, pusa_rx_errors, pusa_prefill_count);
}
