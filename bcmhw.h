/*
 * Header file with defines for Raspberry Pi hardware registers.
 */

#ifndef __bcmhw_h__
#define __bcmhw_h__

// Clock manager
#define CM_GPCTL(x)		((unsigned long)(clks_base) + (8 * (x)))
#define CM_GPDIV(x)		((unsigned long)(clks_base) + (8 * (x)) + 4)

#define CM_GPCTL_PASSWD		(0x5a << 24)
#define CM_GPCTL_MASH_INTDIV	(0 << 9)
#define CM_GPCTL_MASH_1STAGE	(1 << 9)
#define CM_GPCTL_MASH_2STAGE	(2 << 9)
#define CM_GPCTL_MASH_3STAGE	(3 << 9)
#define CM_GPCTL_FLIP		(1 << 8)
#define CM_GPCTL_BUSY		(1 << 7)
#define CM_GPCTL_KILL		(1 << 5)
#define CM_GPCTL_ENABLE		(1 << 4)
#define CM_GPCTL_SRC_GND	(0)
#define CM_GPCTL_SRC_OSC	(1)
#define CM_GPCTL_SRC_TESTDEBUG0	(2)
#define CM_GPCTL_SRC_TESTDEBUG1	(3)
#define CM_GPCTL_SRC_PLLA	(4)
#define CM_GPCTL_SRC_PLLC	(5)
#define CM_GPCTL_SRC_PLLD	(6)
#define CM_GPCTL_SRC_HDMI	(7)
#define CM_GPDIV_PASSWD		(0x5a << 24)
#define CM_GPDIV_DIVI(x)	(((x) & 0xfff) << 12)
#define CM_GPDIV_DIVF(x)	((x) & 0xfff)

#define CM_I2S_CLOCK		19

#define GPFSEL0			((unsigned long) gpio_base + 0x00)
#define GPSET0			((unsigned long) gpio_base + 0x1C)
#define GPCLR0			((unsigned long) gpio_base + 0x28)
#define GPPUD			((unsigned long) gpio_base + 0x94)
#define GPPUDCLK0		((unsigned long) gpio_base + 0x98)

#define GPIO_PU_ENABLEUP	2
#define GPIO_PU_ENABLEDOWN	1
#define GPIO_PU_DISABLE		0

#define GPIO_FUNC_INPUT		0x0
#define GPIO_FUNC_OUTPUT	0x1
#define GPIO_FUNC_ALT0		0x4
#define GPIO_FUNC_ALT1		0x5
#define GPIO_FUNC_ALT2		0x6
#define GPIO_FUNC_ALT3		0x7
#define GPIO_FUNC_ALT4		0x3
#define GPIO_FUNC_ALT5		0x2

// I2S
#define PCM_CS_A		((unsigned long) pcm_base + 0x00)
#define PCM_FIFO_A		((unsigned long) pcm_base + 0x04)
#define PCM_MODE_A		((unsigned long) pcm_base + 0x08)
#define PCM_RXC_A		((unsigned long) pcm_base + 0x0C)
#define PCM_TXC_A		((unsigned long) pcm_base + 0x10)
#define PCM_DREQ_A		((unsigned long) pcm_base + 0x14)
#define PCM_INTEN_A		((unsigned long) pcm_base + 0x18)
#define PCM_INTSTC_A		((unsigned long) pcm_base + 0x1C)
#define PCM_GRAY_A		((unsigned long) pcm_base + 0x20)

#define PCM_CS_STBY		(1 << 25)
#define PCM_CS_SYNC		(1 << 24)
#define PCM_CS_RXSEX		(1 << 23)
#define PCM_CS_RXF		(1 << 22)
#define PCM_CS_TXE		(1 << 21)
#define PCM_CS_RXD		(1 << 20)
#define PCM_CS_TXD		(1 << 19)
#define PCM_CS_RXR		(1 << 18)
#define PCM_CS_TXW		(1 << 17)
#define PCM_CS_RXERR		(1 << 16)
#define PCM_CS_TXERR		(1 << 15)
#define PCM_CS_RXSYNC		(1 << 14)
#define PCM_CS_TXSYNC		(1 << 13)
#define PCM_CS_DMAEN		(1 << 9)
#define PCM_CS_RXTHR_NOTEMPTY	(0 << 7)
#define PCM_CS_RXTHR_LVL1	(1 << 7)
#define PCM_CS_RXTHR_LVL2	(2 << 7)
#define PCM_CS_RXTHR_FULL	(3 << 7)
#define PCM_CS_TXTHR_EMPTY	(0 << 5)
#define PCM_CS_TXTHR_LVL1	(1 << 5)
#define PCM_CS_TXTHR_LVL2	(2 << 5)
#define PCM_CS_TXTHR_ALMOSTFULL	(3 << 5)
#define PCM_CS_RXCLR		(1 << 4)
#define PCM_CS_TXCLR		(1 << 3)
#define PCM_CS_TXON		(1 << 2)
#define PCM_CS_RXON		(1 << 1)
#define PCM_CS_EN		(1 << 0)

#define PCM_MODE_CLK_DIS	(1 << 28)
#define PCM_MODE_PDMN		(1 << 27)
#define PCM_MODE_PDME		(1 << 26)
#define PCM_MODE_FRXP		(1 << 25)
#define PCM_MODE_FTXP		(1 << 24)
#define PCM_MODE_CLKM		(1 << 23)
#define PCM_MODE_CLKI		(1 << 22)
#define PCM_MODE_FSM		(1 << 21)
#define PCM_MODE_FSI		(1 << 20)
#define PCM_MODE_FLEN(x)	((x) << 10)
#define PCM_MODE_FSLEN(x)	((x) << 0)

#define PCM_RXC_CH1WEX		(1 << 31)
#define PCM_RXC_CH1EN		(1 << 30)
#define PCM_RXC_CH1POS(x)	((x) << 20)
#define PCM_RXC_CH1WID(x)	((x) << 16)
#define PCM_RXC_CH2WEX		(1 << 15)
#define PCM_RXC_CH2EN		(1 << 14)
#define PCM_RXC_CH2POS(x)	((x) << 4)
#define PCM_RXC_CH2WID(x)	((x) << 0)

#define PCM_TXC_CH1WEX		(1 << 31)
#define PCM_TXC_CH1EN		(1 << 30)
#define PCM_TXC_CH1POS(x)	((x) << 20)
#define PCM_TXC_CH1WID(x)	((x) << 16)
#define PCM_TXC_CH2WEX		(1 << 15)
#define PCM_TXC_CH2EN		(1 << 14)
#define PCM_TXC_CH2POS(x)	((x) << 4)
#define PCM_TXC_CH2WID(x)	((x) << 0)

#define PCM_INT_RXERR		(1 << 3)
#define PCM_INT_TXERR		(1 << 2)
#define PCM_INT_RXR		(1 << 1)
#define PCM_INT_TXW		(1 << 0)

// DMA Controller
#define DMA_CS(n)        ((unsigned long) dma_base + 0x100 * (n) + 0x00)
#define DMA_CONBLK_AD(n) ((unsigned long) dma_base + 0x100 * (n) + 0x04)
#define DMA_TI(n)        ((unsigned long) dma_base + 0x100 * (n) + 0x08)
#define DMA_SOURCE_AD(n) ((unsigned long) dma_base + 0x100 * (n) + 0x0C)
#define DMA_DEST_AD(n)   ((unsigned long) dma_base + 0x100 * (n) + 0x10)
#define DMA_TXFR_LEN(n)  ((unsigned long) dma_base + 0x100 * (n) + 0x14)
#define DMA_STRIDE(n)    ((unsigned long) dma_base + 0x100 * (n) + 0x18)
#define DMA_NEXTCONBK(n) ((unsigned long) dma_base + 0x100 * (n) + 0x1C)
#define DMA_DEBUG(n)     ((unsigned long) dma_base + 0x100 * (n) + 0x20)

// DMA CS Register bits
#define DMA_CS_RESET         (1 << 31)
#define DMA_CS_ABORT         (1 << 30)
#define DMA_CS_DISDEBUG      (1 << 29)
#define DMA_CS_WAIT_FOR_OUTSTANDING_WRITES (1 << 28)
#define DMA_CS_PANIC_PRIORITY_SHIFT 20
#define DMA_CS_PRIORITY_SHIFT 16
#define DMA_CS_ERROR         (1 << 8)
#define DMA_CS_WAITING_FOR_OUTSTANDING_WRITES (1 << 6)
#define DMA_CS_DREQ_STOPS_DMA (1 << 5)
#define DMA_CS_PAUSED        (1 << 4)
#define DMA_CS_DREQ          (1 << 3)
#define DMA_CS_INT           (1 << 2)
#define DMA_CS_END           (1 << 1)
#define DMA_CS_ACTIVE        (1 << 0)

// DMA TI Register bits
#define DMA_TI_NO_WIDE_BURSTS (1 << 26)
#define DMA_TI_WAITS_SHIFT   21
#define DMA_TI_PERMAP_SHIFT  16
#define DMA_TI_BURST_LENGTH_SHIFT 12
#define DMA_TI_SRC_IGNORE    (1 << 11)
#define DMA_TI_SRC_DREQ      (1 << 10)
#define DMA_TI_SRC_WIDTH     (1 << 9)
#define DMA_TI_SRC_INC       (1 << 8)
#define DMA_TI_DEST_IGNORE   (1 << 7)
#define DMA_TI_DEST_DREQ     (1 << 6)
#define DMA_TI_DEST_WIDTH    (1 << 5)
#define DMA_TI_DEST_INC      (1 << 4)
#define DMA_TI_WAIT_RESP     (1 << 3)
#define DMA_TI_TDMODE        (1 << 1)
#define DMA_TI_INTEN         (1 << 0)

// I2S/PCM DREQ ID
#define DMA_DREQ_PCM_TX      2
#define DMA_DREQ_PCM_RX      3

// DMA Control Block structure (must be 32-byte aligned)
typedef struct {
    unsigned int ti;            // Transfer Information
    unsigned int source_ad;     // Source address
    unsigned int dest_ad;       // Destination address
    unsigned int txfr_len;      // Transfer length
    unsigned int stride;        // 2D stride
    unsigned int nextconbk;     // Next control block
    unsigned int reserved1;     // Reserved
    unsigned int reserved2;     // Reserved
} dma_cb_t;

extern void *base_address;
extern void *clks_base;
extern void *gpio_base;
extern void *pcm_base;
extern void *dma_base;

static inline void writel(unsigned long lp, unsigned long l)
{
    *(volatile unsigned long *)lp = l;
}

static inline unsigned long readl(unsigned long lp)
{
    return *(volatile unsigned long *)lp;
}

int bcmhw_init(void);
void bcmhw_gpio_select(int gpio, int function);
void bcmhw_gpio_print(int gpio);
int bcmhw_set_i2s_clk(int samplerate);
void bcmhw_gpio_set(int gpio, int on);
unsigned long bcmhw_get_system_timer(void);

#endif /* __bcmhw_h__ */
