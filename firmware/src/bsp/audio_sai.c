/* audio_sai.c — SAI1 TDM (8 slots x 32-bit, 24-bit data) + DMA2 double-buffer.
 *
 * Confirmed (bench 1): SAI1, Block A = Master RX (codec ADC in), Block B = Slave
 * TX (the 8 DAC channels), 8 slots x 32-bit slot, 24-bit data, 256-bit frame.
 *
 * [BENCH] to confirm on the logic analyzer / against the stock codec init:
 *   - SAI pin assignment (AF6 set below is the common port-E group)
 *   - the MCLK/bit-clock divider chain (MCKDIV + PLLSAI in clock.c) to hit 96 kHz
 *   - FS framing (FSALL/FSPOL/FSOFF/FSDEF) the CS42888 expects for TDM
 *   - which DMA stream/channel the board's silicon rev maps SAI1_A/_B to
 *
 * Timing source = the RX DMA (Stream1) half/complete IRQ: on each half we hand one
 * AUDIO_BLOCK_FRAMES block of RX frames to bsp_audio_isr() and it fills the matching
 * half of the TX buffer (transmitted ~one block later).  Buffers live in SRAM1 —
 * DMA2 cannot reach CCM.
 */
#include "stm32f429xx.h"
#include "board.h"
#include "bsp.h"

#define FR   AUDIO_BLOCK_FRAMES
#define SL   TDM_SLOTS
#define HALF (FR * SL)              /* int32 words per DMA half */

/* DMA-accessible (SRAM1). Two halves each: [0..HALF) and [HALF..2*HALF). */
static int32_t rxbuf[2 * HALF];
static int32_t txbuf[2 * HALF];

/* weak default: silence, until main.c overrides it with the engine bridge. */
__attribute__((weak)) void bsp_audio_isr(const int32_t *in, int32_t *out, unsigned frames)
{
    (void)in;
    for (unsigned i = 0; i < frames * SL; ++i) out[i] = 0;
}

static void sai_gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOEEN;
    (void)RCC->AHB1ENR;
    /* PE2 MCLK_A, PE3 SD_B, PE4 FS_A, PE5 SCK_A, PE6 SD_A — all AF6. [BENCH] */
    static const uint8_t pins[] = {2, 3, 4, 5, 6};
    for (unsigned i = 0; i < sizeof(pins); ++i) {
        uint32_t n = pins[i];
        GPIOE->MODER   = (GPIOE->MODER   & ~(3u << (n*2))) | (2u << (n*2));
        GPIOE->OTYPER &= ~(1u << n);
        GPIOE->OSPEEDR |= (3u << (n*2));
        GPIOE->PUPDR   = (GPIOE->PUPDR   & ~(3u << (n*2)));
        GPIOE->AFR[0]  = (GPIOE->AFR[0]  & ~(0xFu << (n*4))) | (6u << (n*4)); /* AF6 */
    }
}

/* Build an SAI block CR1. mode: 01=master RX, 10=slave TX. syncen: 00 async, 01 sync. */
static uint32_t sai_cr1(uint32_t mode, uint32_t syncen, uint32_t mckdiv)
{
    return (mode << 0)
         | (0u   << 2)          /* PRTCFG = free protocol */
         | (6u   << 5)          /* DS = 24-bit            */
         | (syncen << 10)
         | (1u   << 13)         /* OUTDRIV */
         | (1u   << 17)         /* DMAEN   */
         | (mckdiv << 20);      /* MCKDIV ([BENCH]) NODIV=0 -> MCLK generated */
}

static void sai_block_config(SAI_Block_TypeDef *b, uint32_t mode, uint32_t syncen)
{
    b->CR1  = sai_cr1(mode, syncen, SAI_MCKDIV);
    b->CR2  = (1u << 0);                    /* FTH = 1/4 FIFO */
    b->FRCR = (255u << 0)                   /* FRL = 256-bit frame */
            | (15u  << 8)                   /* FSALL = 16-bit FS pulse [BENCH] */
            | (0u   << 16)                  /* FSDEF = start-of-frame */
            | (1u   << 17)                  /* FSPOL = active high [BENCH] */
            | (1u   << 18);                 /* FSOFF = asserted before slot0 [BENCH] */
    b->SLOTR = (0u << 0)                    /* FBOFF = 0 */
             | (2u << 6)                    /* SLOTSZ = 32-bit */
             | ((SL - 1u) << 8)             /* NBSLOT = 8 */
             | (0xFFu << 16);               /* SLOTEN = all 8 slots */
}

static void sai_dma_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    /* RX: DMA2 Stream1 Ch0, peripheral-to-memory, circular, HT+TC IRQ. [BENCH stream] */
    DMA2_Stream1->CR = 0;
    while (DMA2_Stream1->CR & DMA_SxCR_EN) { }
    DMA2_Stream1->PAR  = (uint32_t)&SAI1_Block_A->DR;
    DMA2_Stream1->M0AR = (uint32_t)rxbuf;
    DMA2_Stream1->NDTR = 2u * HALF;
    DMA2_Stream1->CR =
          (0u << 25)                        /* channel 0 */
        | (0u << 6)                         /* DIR = periph->mem */
        | DMA_SxCR_MINC
        | (2u << 11) | (2u << 13)           /* PSIZE/MSIZE = 32-bit */
        | DMA_SxCR_CIRC
        | DMA_SxCR_HTIE | DMA_SxCR_TCIE
        | (3u << 16);                       /* priority very high */

    /* TX: DMA2 Stream5 Ch0, memory-to-peripheral, circular. [BENCH stream] */
    DMA2_Stream5->CR = 0;
    while (DMA2_Stream5->CR & DMA_SxCR_EN) { }
    DMA2_Stream5->PAR  = (uint32_t)&SAI1_Block_B->DR;
    DMA2_Stream5->M0AR = (uint32_t)txbuf;
    DMA2_Stream5->NDTR = 2u * HALF;
    DMA2_Stream5->CR =
          (0u << 25)                        /* channel 0 */
        | (1u << 6)                         /* DIR = mem->periph */
        | DMA_SxCR_MINC
        | (2u << 11) | (2u << 13)
        | DMA_SxCR_CIRC
        | (3u << 16);

    NVIC_SetPriority(DMA2_Stream1_IRQn, 1);
    NVIC_EnableIRQ(DMA2_Stream1_IRQn);
}

void bsp_audio_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SAI1EN;
    (void)RCC->APB2ENR;
    sai_gpio_init();
    sai_block_config(SAI1_Block_A, 1u, 0u);   /* master RX, async  */
    sai_block_config(SAI1_Block_B, 2u, 1u);   /* slave TX, sync    */
    sai_dma_init();
}

void bsp_audio_start(void)
{
    /* DMA first, then SAI (slave block B before master block A). */
    DMA2_Stream1->CR |= DMA_SxCR_EN;
    DMA2_Stream5->CR |= DMA_SxCR_EN;
    SAI1_Block_B->CR1 |= (1u << 16);          /* SAIEN B (slave)  */
    SAI1_Block_A->CR1 |= (1u << 16);          /* SAIEN A (master) -> clocks run */
}

void DMA2_Stream1_IRQHandler(void)
{
    if (DMA2->LISR & DMA_LISR_HTIF1) {        /* first half filled */
        DMA2->LIFCR = DMA_LIFCR_CHTIF1;
        bsp_audio_isr(&rxbuf[0], &txbuf[0], FR);
    }
    if (DMA2->LISR & DMA_LISR_TCIF1) {        /* second half filled */
        DMA2->LIFCR = DMA_LIFCR_CTCIF1;
        bsp_audio_isr(&rxbuf[HALF], &txbuf[HALF], FR);
    }
}
