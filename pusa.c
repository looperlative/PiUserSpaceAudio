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

// Memory page size
#define PAGE_SIZE 4096

// DMA buffer size in samples (must be a multiple of 2 for stereo I2S)
#define DMA_BUFFER_SIZE 512
#define NUM_DMA_BUFFERS 2

// DMA channel numbers for RX and TX
#define DMA_RX_CHANNEL 1
#define DMA_TX_CHANNEL 2

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
int pusa_max_loops = 0;
int pusa_rx_counter = 0;
int pusa_tx_counter = 0;
int pusa_tx_counter_at_first_found = 0;
int pusa_prefill_count = 0;
int pusa_done = 0;
static int pusa_rt_tid = 0;
static __thread int pusa_is_rt_thread = 0;

// DMA buffers and control blocks
static int *rx_buffer_virt[NUM_DMA_BUFFERS];  // Virtual address of RX buffers
static int *tx_buffer_virt[NUM_DMA_BUFFERS];  // Virtual address of TX buffers
static unsigned int rx_buffer_phys[NUM_DMA_BUFFERS];  // Physical address of RX buffers
static unsigned int tx_buffer_phys[NUM_DMA_BUFFERS];  // Physical address of TX buffers
static dma_cb_t *rx_cb_virt[NUM_DMA_BUFFERS];  // Virtual address of RX control blocks
static dma_cb_t *tx_cb_virt[NUM_DMA_BUFFERS];  // Virtual address of TX control blocks
static unsigned int rx_cb_phys[NUM_DMA_BUFFERS];  // Physical address of RX control blocks
static unsigned int tx_cb_phys[NUM_DMA_BUFFERS];  // Physical address of TX control blocks

static int current_rx_buffer = 0;  // Index of current RX buffer
static int current_tx_buffer = 0;  // Index of current TX buffer

pusa_audio_handler_t pusa_audio_handler = NULL;

static pusa_rt_func pusa_rt_modifier_func;
static int pusa_rt_modifier_return;
static void *pusa_rt_modifier_parm;
volatile int pusa_rt_modifier_go = 0;
static pthread_mutex_t pusa_rt_modifier_lock = PTHREAD_MUTEX_INITIALIZER;

static pusa_rt_func long_funcs[5000] = { 0 };
static int long_count = 0;

static unsigned long time1_times[100];
static unsigned long time2_times[100];
volatile unsigned long num_times = 0;

// Function to allocate memory suitable for DMA (uncached and aligned)
static void *dma_alloc(size_t size, unsigned int *phys_addr)
{
    int mem_fd;
    void *virt_addr;
    
    // Open /dev/mem for physical memory access
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        perror("Failed to open /dev/mem");
        return NULL;
    }
    
    // Round up size to page boundary
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // Allocate uncached memory
    virt_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, 0);
    if (virt_addr == MAP_FAILED) {
        perror("mmap failed");
        close(mem_fd);
        return NULL;
    }
    
    // Get physical address (simple approximation - real implementation requires more work)
    // In practice, we would need a kernel driver to get the actual bus address
    *phys_addr = (unsigned int)virt_addr;
    
    close(mem_fd);
    return virt_addr;
}

// Function to initialize DMA for I2S audio
static int init_dma(void)
{
    int i;
    
    // Allocate buffers and control blocks for RX and TX
    for (i = 0; i < NUM_DMA_BUFFERS; i++) {
        // Allocate RX buffer
        rx_buffer_virt[i] = (int *)dma_alloc(DMA_BUFFER_SIZE * sizeof(int), &rx_buffer_phys[i]);
        if (!rx_buffer_virt[i]) {
            printf("Failed to allocate RX buffer %d\n", i);
            return -1;
        }
        
        // Allocate TX buffer
        tx_buffer_virt[i] = (int *)dma_alloc(DMA_BUFFER_SIZE * sizeof(int), &tx_buffer_phys[i]);
        if (!tx_buffer_virt[i]) {
            printf("Failed to allocate TX buffer %d\n", i);
            return -1;
        }
        
        // Clear buffers
        memset(rx_buffer_virt[i], 0, DMA_BUFFER_SIZE * sizeof(int));
        memset(tx_buffer_virt[i], 0, DMA_BUFFER_SIZE * sizeof(int));
        
        // Allocate RX control block
        rx_cb_virt[i] = (dma_cb_t *)dma_alloc(sizeof(dma_cb_t), &rx_cb_phys[i]);
        if (!rx_cb_virt[i]) {
            printf("Failed to allocate RX control block %d\n", i);
            return -1;
        }
        
        // Allocate TX control block
        tx_cb_virt[i] = (dma_cb_t *)dma_alloc(sizeof(dma_cb_t), &tx_cb_phys[i]);
        if (!tx_cb_virt[i]) {
            printf("Failed to allocate TX control block %d\n", i);
            return -1;
        }
        
        // Configure RX control block
        rx_cb_virt[i]->ti = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP |
                           (DMA_DREQ_PCM_RX << DMA_TI_PERMAP_SHIFT) |
                           DMA_TI_SRC_DREQ | DMA_TI_DEST_INC;
        rx_cb_virt[i]->source_ad = (unsigned int)PCM_FIFO_A & 0x7FFFFFFF; // Physical address of PCM FIFO
        rx_cb_virt[i]->dest_ad = rx_buffer_phys[i];
        rx_cb_virt[i]->txfr_len = DMA_BUFFER_SIZE * sizeof(int);
        rx_cb_virt[i]->stride = 0;
        rx_cb_virt[i]->nextconbk = rx_cb_phys[(i + 1) % NUM_DMA_BUFFERS]; // Link to next control block
        
        // Configure TX control block
        tx_cb_virt[i]->ti = DMA_TI_NO_WIDE_BURSTS | DMA_TI_WAIT_RESP |
                           (DMA_DREQ_PCM_TX << DMA_TI_PERMAP_SHIFT) |
                           DMA_TI_DEST_DREQ | DMA_TI_SRC_INC;
        tx_cb_virt[i]->source_ad = tx_buffer_phys[i];
        tx_cb_virt[i]->dest_ad = (unsigned int)PCM_FIFO_A & 0x7FFFFFFF; // Physical address of PCM FIFO
        tx_cb_virt[i]->txfr_len = DMA_BUFFER_SIZE * sizeof(int);
        tx_cb_virt[i]->stride = 0;
        tx_cb_virt[i]->nextconbk = tx_cb_phys[(i + 1) % NUM_DMA_BUFFERS]; // Link to next control block
    }
    
    return 0;
}

// Function to start DMA transfers
static void start_dma(void)
{
    // Reset and configure DMA channels
    
    // Reset RX DMA channel
    writel(DMA_CS(DMA_RX_CHANNEL), DMA_CS_RESET);
    usleep(1000); // Wait for reset to complete
    
    // Reset TX DMA channel
    writel(DMA_CS(DMA_TX_CHANNEL), DMA_CS_RESET);
    usleep(1000); // Wait for reset to complete
    
    // Set RX DMA channel control block address
    writel(DMA_CONBLK_AD(DMA_RX_CHANNEL), rx_cb_phys[0]);
    
    // Set TX DMA channel control block address
    writel(DMA_CONBLK_AD(DMA_TX_CHANNEL), tx_cb_phys[0]);
    
    // Enable DMA for PCM
    unsigned long cs = readl(PCM_CS_A);
    writel(PCM_CS_A, cs | PCM_CS_DMAEN);
    
    // Start RX DMA channel
    writel(DMA_CS(DMA_RX_CHANNEL), DMA_CS_ACTIVE | DMA_CS_WAIT_FOR_OUTSTANDING_WRITES | 
          (8 << DMA_CS_PRIORITY_SHIFT) | (8 << DMA_CS_PANIC_PRIORITY_SHIFT) | DMA_CS_DREQ_STOPS_DMA);
    
    // Start TX DMA channel
    writel(DMA_CS(DMA_TX_CHANNEL), DMA_CS_ACTIVE | DMA_CS_WAIT_FOR_OUTSTANDING_WRITES | 
          (8 << DMA_CS_PRIORITY_SHIFT) | (8 << DMA_CS_PANIC_PRIORITY_SHIFT) | DMA_CS_DREQ_STOPS_DMA);
}

int pusa_execute_in_rt(pusa_rt_func func, void *parm)
{
    if (pusa_is_rt_thread)
    {
	printf("execute_in_rt error: %p, %p\n", func, parm);
	exit(1);
    }

    pthread_mutex_lock(&pusa_rt_modifier_lock);
    pusa_rt_modifier_parm = parm;
    pusa_rt_modifier_func = func;
    pusa_rt_modifier_go = 1;

    while (pusa_rt_modifier_go)
	;

    int rv = pusa_rt_modifier_return;
    pthread_mutex_unlock(&pusa_rt_modifier_lock);

    return rv;
}

void *pusa_audio_thread(void *arg)
{
    pusa_rt_tid = gettid();
    pusa_is_rt_thread = 1;

    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET(2, &cpus);
    sched_setaffinity(0, sizeof(cpus), &cpus);

    struct sched_param sparam;
    sparam.sched_priority = 99;
    sched_setscheduler(pusa_rt_tid, SCHED_FIFO, &sparam);

    /*
     * It is possible that we were running before and never stopped.  There is
     * no reset bit, but we can stop the interface, wait, and then enable it again.
     *
     * This does seem to steal the I2S interface from Linux.  This is tested with
     * the Pisound card.  Stealing the interface without disabling the driver allows
     * the Pisound MIDI interface to continue to work in Linux ALSA.  This may not
     * strictly the cleanest way to do things, but it works and avoids rewriting
     * the device driver for the Pisound MIDI interface which is currently a single
     * driver with the audio interface.  Honestly, it doesn't need to be that way
     * since the Pisound is using a UART with a SPI interface for MIDI.  Like it or
     * not, I've decided to leave things this way.
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

    /*
     * Clock setup is the most difficult parameter to understand.  The following works with
     * the Pisound Audio/MIDI card for 48K sample rate.  The FLEN parameter must be an integral
     * number in combination with the CLK_DIV parameter.  FLEN * CLK_DIV = CLK frequency / sample frequency.
     *
     * CLK frequency is 12MHz for Pisound.  12MHz / 48KHz = 250.  This gives CLK_DIV = 10 and FLEN = 25.
     * The FS signal is high 1 CLK and low 24 CLK.
     *
     * Mode is I2S.  Each frame contains a left and right 32-bit sample.  The total size of a frame in
     * bits is: FLEN * 2.  In this case: 25 * 2 = 50 CLK per frame.  One frame contains left and right
     * channel 24-bit signed samples. The Pisound card is using 24-bit samples.
     *
     * The code below sets the PCM_RXC_A and PCM_TXC_A registers to enable left and right channel
     * and sets width to 16 bits - our code will shift and sign extend to 32-bit signed int.
     */
    writel(PCM_MODE_A, PCM_MODE_FLEN(25) | PCM_MODE_FSLEN(1));
    writel(PCM_RXC_A, PCM_RXC_CH1EN | PCM_RXC_CH1WID(16) | PCM_RXC_CH2EN | PCM_RXC_CH2WID(16));
    writel(PCM_TXC_A, PCM_TXC_CH1EN | PCM_TXC_CH1WID(16) | PCM_TXC_CH2EN | PCM_TXC_CH2WID(16));

    /*
     * Initialize and start DMA for I2S audio
     */
    if (init_dma() < 0) {
        printf("Failed to initialize DMA\n");
        return NULL;
    }
    start_dma();

    /*
     * Check if TX_EMPTY is set and if not, clear it.
     */
    unsigned long status = readl(PCM_CS_A);
    if ((status & PCM_CS_TXE) == 0)
	writel(PCM_CS_A, status | PCM_CS_TXCLR);
    status = readl(PCM_CS_A);

    /*
     * Enable RX and TX simultaneously
     */
    writel(PCM_CS_A, status | PCM_CS_RXON | PCM_CS_TXON);

    while (!pusa_done)
    {
        pusa_rt_func last_func;
        if (pusa_rt_modifier_go)
        {
            pusa_rt_modifier_return = (*pusa_rt_modifier_func)(pusa_rt_modifier_parm);
            last_func = pusa_rt_modifier_func;
            pusa_rt_modifier_go = 0;
        }

        /*
         * Process audio data using DMA
         */
        
        // Check if RX DMA has completed a buffer
        if (readl(DMA_CS(DMA_RX_CHANNEL)) & DMA_CS_END) {
            // Process received audio data
            int buffer_processed = 0;
            
            for (int i = 0; i < DMA_BUFFER_SIZE; i += 2) {
                if (num_times < 100)
                    time1_times[num_times] = bcmhw_get_system_timer();

                if (pusa_audio_handler != NULL) {
                    // Process audio data through the handler function
                    pusa_audio_handler(&rx_buffer_virt[current_rx_buffer][i], 2);
                    
                    // Copy processed data to TX buffer
                    tx_buffer_virt[current_tx_buffer][i] = rx_buffer_virt[current_rx_buffer][i];
                    tx_buffer_virt[current_tx_buffer][i+1] = rx_buffer_virt[current_rx_buffer][i+1];
                }

                if (num_times < 100)
                    time2_times[num_times++] = bcmhw_get_system_timer();
                
                buffer_processed = 1;
                pusa_rx_counter++;
                pusa_tx_counter++;
            }
            
            if (buffer_processed) {
                // Switch to next buffer
                current_rx_buffer = (current_rx_buffer + 1) % NUM_DMA_BUFFERS;
                current_tx_buffer = (current_tx_buffer + 1) % NUM_DMA_BUFFERS;
                
                // Restart DMA if it's stopped
                unsigned long rx_cs = readl(DMA_CS(DMA_RX_CHANNEL));
                if (!(rx_cs & DMA_CS_ACTIVE)) {
                    writel(DMA_CONBLK_AD(DMA_RX_CHANNEL), rx_cb_phys[current_rx_buffer]);
                    writel(DMA_CS(DMA_RX_CHANNEL), DMA_CS_ACTIVE | DMA_CS_WAIT_FOR_OUTSTANDING_WRITES | 
                          (8 << DMA_CS_PRIORITY_SHIFT) | (8 << DMA_CS_PANIC_PRIORITY_SHIFT) | DMA_CS_DREQ_STOPS_DMA);
                }
                
                unsigned long tx_cs = readl(DMA_CS(DMA_TX_CHANNEL));
                if (!(tx_cs & DMA_CS_ACTIVE)) {
                    writel(DMA_CONBLK_AD(DMA_TX_CHANNEL), tx_cb_phys[current_tx_buffer]);
                    writel(DMA_CS(DMA_TX_CHANNEL), DMA_CS_ACTIVE | DMA_CS_WAIT_FOR_OUTSTANDING_WRITES | 
                          (8 << DMA_CS_PRIORITY_SHIFT) | (8 << DMA_CS_PANIC_PRIORITY_SHIFT) | DMA_CS_DREQ_STOPS_DMA);
                }
            }
        }
        
        // Check for DMA errors
        unsigned long rx_cs = readl(DMA_CS(DMA_RX_CHANNEL));
        unsigned long tx_cs = readl(DMA_CS(DMA_TX_CHANNEL));
        
        if (rx_cs & DMA_CS_ERROR) {
            pusa_rx_errors++;
            writel(DMA_CS(DMA_RX_CHANNEL), DMA_CS_RESET);
            usleep(1000);
        }
        
        if (tx_cs & DMA_CS_ERROR) {
            pusa_tx_errors++;
            writel(DMA_CS(DMA_TX_CHANNEL), DMA_CS_RESET);
            usleep(1000);
        }
        
        // Check for PCM errors
        status = readl(PCM_CS_A);
        writel(PCM_CS_A, status);
        
        if (status & PCM_CS_RXERR)
            pusa_rx_errors++;
        if (status & PCM_CS_TXERR)
            pusa_tx_errors++;
    }
    
    // Stop DMA when done
    writel(DMA_CS(DMA_RX_CHANNEL), DMA_CS_RESET);
    writel(DMA_CS(DMA_TX_CHANNEL), DMA_CS_RESET);
    
    return NULL;
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
     * Lock memory to prevent paging.
     */
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
        return EXIT_FAILURE;
    }

    /*
     * Set affinity of standard threads.
     */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    CPU_SET(1, &cpuset);
    CPU_SET(2, &cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("Failed to set main thread affinity");
        return EXIT_FAILURE;
    }

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
    printf("tx %d (%d), rx %d, tx errors %d, rx errors %d, prefill %d, max loops %d\n",
	   pusa_tx_counter, pusa_tx_counter_at_first_found, pusa_rx_counter,
	   pusa_tx_errors, pusa_rx_errors, pusa_prefill_count, pusa_max_loops);
    pusa_max_loops = 0;

    printf("Long functions:\n");
    for (int i = 0; i < long_count; i++)
	printf("   %p\n", long_funcs[i]);

#ifdef PUSA_VERBOSE_DEBUG
    printf("Times:\n");
    for (int i = 0; i < num_times; i++)
	printf("   %d %d\n", time1_times[i], time2_times[i]);
#endif

    num_times = 0;
}
