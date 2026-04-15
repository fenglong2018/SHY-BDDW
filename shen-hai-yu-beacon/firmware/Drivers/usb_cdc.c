/**
 * @file  usb_cdc.c
 * @brief N32L403 USB Full-Speed CDC 虚拟串口驱动
 *
 * 实现最小化 USB CDC-ACM：
 *   - 设备描述符 / 配置描述符 / CDC 描述符
 *   - Bulk IN  EP1 → 发送数据到主机
 *   - Bulk OUT EP2 → 接收主机数据
 *   - Control EP0  → 枚举 + CDC 控制请求
 *
 * PA11 = D-,  PA12 = D+  (N32L403 原生 USB)
 */

#include "usb_cdc.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* USB 寄存器                                                           */
/* ------------------------------------------------------------------ */
#define USB_BASE        0x40005C00UL
#define USB_SRAM_BASE   0x40006000UL  /* Packet Buffer SRAM */

typedef struct {
    volatile uint32_t EP[8];    /* 0x00: EPnR x8 */
    volatile uint32_t RESERVED[8];
    volatile uint32_t CNTR;     /* 0x40 */
    volatile uint32_t ISTR;     /* 0x44 */
    volatile uint32_t FNR;      /* 0x48 */
    volatile uint32_t DADDR;    /* 0x4C */
    volatile uint32_t BTABLE;   /* 0x50 */
} USB_T;
#define USB ((USB_T *)USB_BASE)

/* Packet Buffer Table (每个 EP 占 4×16bit = 8 字节) */
typedef struct {
    volatile uint16_t ADDR_TX;  uint16_t _r0;
    volatile uint16_t COUNT_TX; uint16_t _r1;
    volatile uint16_t ADDR_RX;  uint16_t _r2;
    volatile uint16_t COUNT_RX; uint16_t _r3;
} PMA_EP_T;
#define PMA_TABLE   ((PMA_EP_T *)(USB_SRAM_BASE))

/* PMA 数据区起始偏移（字节，相对 USB_SRAM_BASE） */
#define PMA_EP0_TX_ADDR   0x40U
#define PMA_EP0_RX_ADDR   0x80U
#define PMA_EP1_TX_ADDR   0xC0U   /* CDC IN  (TX to host) */
#define PMA_EP2_RX_ADDR   0x100U  /* CDC OUT (RX from host) */
#define PMA_EP_SIZE       64U

/* EPnR 位操作宏（toggle-to-clear 机制） */
#define EP_STAT_TX_MASK  (3U << 4)
#define EP_STAT_RX_MASK  (3U << 12)
#define EP_STAT_TX_NAK   (2U << 4)
#define EP_STAT_TX_VALID (3U << 4)
#define EP_STAT_RX_NAK   (2U << 12)
#define EP_STAT_RX_VALID (3U << 12)
#define EP_CTR_RX        (1U << 15)
#define EP_CTR_TX        (1U << 7)
#define EP_SETUP         (1U << 11)

/* RCC / GPIO */
typedef struct {
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR;
    volatile uint32_t AHBENR, APB2ENR, APB1ENR;
} RCC_T;
#define RCC ((RCC_T *)0x40021000UL)

typedef struct { volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR; } GPIO_T;
#define GPIOA_USB ((GPIO_T *)0x40010800UL)

#define NVIC_ISER0 (*(volatile uint32_t *)0xE000E100UL)

/* ------------------------------------------------------------------ */
/* 描述符                                                               */
/* ------------------------------------------------------------------ */
static const uint8_t s_dev_desc[] = {
    0x12, 0x01,             /* bLength, bDescriptorType=DEVICE */
    0x10, 0x01,             /* bcdUSB = 1.10 */
    0x02, 0x00, 0x00,       /* bDeviceClass=CDC, SubClass, Protocol */
    0x40,                   /* bMaxPacketSize0 = 64 */
    0x83, 0x04,             /* idVendor  = 0x0483 (ST 借用) */
    0x40, 0x57,             /* idProduct = 0x5740 (CDC VCP) */
    0x00, 0x02,             /* bcdDevice */
    0x01, 0x02, 0x03,       /* iManufacturer, iProduct, iSerialNumber */
    0x01                    /* bNumConfigurations */
};

static const uint8_t s_cfg_desc[] = {
    /* Configuration */
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,
    /* Interface 0: CDC Control */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    /* CDC Header */
    0x05, 0x24, 0x00, 0x10, 0x01,
    /* CDC Call Management */
    0x05, 0x24, 0x01, 0x00, 0x01,
    /* CDC ACM */
    0x04, 0x24, 0x02, 0x02,
    /* CDC Union */
    0x05, 0x24, 0x06, 0x00, 0x01,
    /* EP3 IN (Interrupt, notification) */
    0x07, 0x05, 0x83, 0x03, 0x08, 0x00, 0xFF,
    /* Interface 1: CDC Data */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    /* EP1 IN Bulk */
    0x07, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00,
    /* EP2 OUT Bulk */
    0x07, 0x05, 0x02, 0x02, 0x40, 0x00, 0x00,
};

static const uint8_t s_lang_desc[]  = { 0x04, 0x03, 0x09, 0x04 };
static const uint8_t s_mfr_desc[]   = { 0x0C, 0x03, 'S',0,'H',0,'Y',0,'B',0,'C',0 };
static const uint8_t s_prod_desc[]  = { 0x1A, 0x03,
    'B',0,'e',0,'a',0,'c',0,'o',0,'n',0,' ',0,'C',0,'D',0,'C',0,' ',0,'C',0,'O',0,'M',0 };
static const uint8_t s_sn_desc[]    = { 0x0A, 0x03, '1',0,'.',0,'0',0,'.',0,'0',0 };

/* ------------------------------------------------------------------ */
/* 内部状态                                                             */
/* ------------------------------------------------------------------ */
static uint8_t  s_addr_pending = 0;
static bool     s_configured   = false;

/* TX 环形缓冲 */
#define TX_BUF_SIZE 512
static uint8_t  s_tx_buf[TX_BUF_SIZE];
static uint16_t s_tx_head = 0, s_tx_tail = 0, s_tx_count = 0;
static bool     s_tx_busy = false;

/* RX 环形缓冲 */
#define RX_BUF_SIZE 256
static uint8_t  s_rx_buf[RX_BUF_SIZE];
static uint16_t s_rx_head = 0, s_rx_tail = 0, s_rx_count = 0;

/* ------------------------------------------------------------------ */
/* PMA 读写（USB SRAM 按 16bit 对齐访问）                               */
/* ------------------------------------------------------------------ */
static void _pma_write(uint32_t pma_offset, const uint8_t *src, uint16_t len)
{
    volatile uint16_t *dst = (volatile uint16_t *)(USB_SRAM_BASE + pma_offset * 2);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = src[i];
        if (i + 1 < len) w |= (uint16_t)(src[i + 1] << 8);
        *dst++ = w;
        dst++;  /* 跳过空字 */
    }
}

static void _pma_read(uint32_t pma_offset, uint8_t *dst, uint16_t len)
{
    volatile uint16_t *src = (volatile uint16_t *)(USB_SRAM_BASE + pma_offset * 2);
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t w = *src++;
        src++;
        dst[i] = (uint8_t)(w & 0xFF);
        if (i + 1 < len) dst[i + 1] = (uint8_t)(w >> 8);
    }
}

/* ------------------------------------------------------------------ */
/* EP 状态设置                                                          */
/* ------------------------------------------------------------------ */
static void _ep_set_tx_valid(uint8_t ep)
{
    uint32_t v = USB->EP[ep];
    v &= ~(EP_CTR_RX | EP_CTR_TX | EP_STAT_TX_MASK);
    v ^= EP_STAT_TX_VALID;
    USB->EP[ep] = v;
}

static void _ep_set_rx_valid(uint8_t ep)
{
    uint32_t v = USB->EP[ep];
    v &= ~(EP_CTR_RX | EP_CTR_TX | EP_STAT_RX_MASK);
    v ^= EP_STAT_RX_VALID;
    USB->EP[ep] = v;
}

static void _ep_set_tx_nak(uint8_t ep)
{
    uint32_t v = USB->EP[ep];
    v &= ~(EP_CTR_RX | EP_CTR_TX | EP_STAT_TX_MASK);
    v ^= EP_STAT_TX_NAK;
    USB->EP[ep] = v;
}

/* ------------------------------------------------------------------ */
/* EP0 控制传输                                                         */
/* ------------------------------------------------------------------ */
static const uint8_t *s_ep0_tx_ptr = NULL;
static uint16_t       s_ep0_tx_rem = 0;

static void _ep0_send(const uint8_t *data, uint16_t len, uint16_t req_len)
{
    if (len > req_len) len = req_len;
    s_ep0_tx_ptr = data;
    s_ep0_tx_rem = len;

    uint16_t chunk = (len > 64) ? 64 : len;
    _pma_write(PMA_EP0_TX_ADDR, data, chunk);
    PMA_TABLE[0].COUNT_TX = chunk;
    s_ep0_tx_ptr += chunk;
    s_ep0_tx_rem -= chunk;
    _ep_set_tx_valid(0);
}

static void _ep0_handle_setup(void)
{
    uint8_t setup[8];
    _pma_read(PMA_EP0_RX_ADDR, setup, 8);

    uint8_t  bmReq  = setup[0];
    uint8_t  bReq   = setup[1];
    uint16_t wValue = (uint16_t)(setup[2] | (setup[3] << 8));
    uint16_t wLen   = (uint16_t)(setup[6] | (setup[7] << 8));

    if ((bmReq & 0x60) == 0x00) {  /* Standard request */
        switch (bReq) {
        case 0x05:  /* SET_ADDRESS */
            s_addr_pending = (uint8_t)(wValue & 0x7F);
            PMA_TABLE[0].COUNT_TX = 0;
            _ep_set_tx_valid(0);
            break;
        case 0x06:  /* GET_DESCRIPTOR */
            switch (wValue >> 8) {
            case 0x01: _ep0_send(s_dev_desc, sizeof(s_dev_desc), wLen); break;
            case 0x02: _ep0_send(s_cfg_desc, sizeof(s_cfg_desc), wLen); break;
            case 0x03:
                switch (wValue & 0xFF) {
                case 0: _ep0_send(s_lang_desc, sizeof(s_lang_desc), wLen); break;
                case 1: _ep0_send(s_mfr_desc,  sizeof(s_mfr_desc),  wLen); break;
                case 2: _ep0_send(s_prod_desc, sizeof(s_prod_desc), wLen); break;
                case 3: _ep0_send(s_sn_desc,   sizeof(s_sn_desc),   wLen); break;
                default: USB->EP[0] |= (3U << 4); break;
                }
                break;
            default: USB->EP[0] |= (3U << 4); break;
            }
            break;
        case 0x09:  /* SET_CONFIGURATION */
            s_configured = true;
            /* 使能 EP1 TX 和 EP2 RX */
            USB->EP[1] = 0x0221;  /* EP1, Bulk, addr=1 */
            PMA_TABLE[1].ADDR_TX  = PMA_EP1_TX_ADDR;
            PMA_TABLE[1].COUNT_TX = 0;
            _ep_set_tx_nak(1);

            USB->EP[2] = 0x3002;  /* EP2, Bulk, addr=2 */
            PMA_TABLE[2].ADDR_RX  = PMA_EP2_RX_ADDR;
            PMA_TABLE[2].COUNT_RX = (1U << 15) | (1U << 10); /* 64 bytes BL_SIZE=1, NUM_BLOCK=1 */
            _ep_set_rx_valid(2);

            PMA_TABLE[0].COUNT_TX = 0;
            _ep_set_tx_valid(0);
            break;
        default:
            PMA_TABLE[0].COUNT_TX = 0;
            _ep_set_tx_valid(0);
            break;
        }
    } else {
        /* CDC class request: SET_LINE_CODING etc. — 直接 ACK */
        PMA_TABLE[0].COUNT_TX = 0;
        _ep_set_tx_valid(0);
    }
}

/* ------------------------------------------------------------------ */
/* 启动下一次 EP1 TX                                                    */
/* ------------------------------------------------------------------ */
static void _ep1_start_tx(void)
{
    if (!s_configured || s_tx_count == 0) {
        s_tx_busy = false;
        return;
    }
    uint16_t len = (s_tx_count > PMA_EP_SIZE) ? PMA_EP_SIZE : s_tx_count;
    uint8_t  tmp[PMA_EP_SIZE];
    for (uint16_t i = 0; i < len; i++) {
        tmp[i] = s_tx_buf[s_tx_head];
        s_tx_head = (s_tx_head + 1) % TX_BUF_SIZE;
        s_tx_count--;
    }
    _pma_write(PMA_EP1_TX_ADDR, tmp, len);
    PMA_TABLE[1].COUNT_TX = len;
    s_tx_busy = true;
    _ep_set_tx_valid(1);
}

/* ------------------------------------------------------------------ */
/* USB 中断处理                                                         */
/* ------------------------------------------------------------------ */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    uint32_t istr = USB->ISTR;

    /* 复位 */
    if (istr & (1U << 10)) {
        USB->CNTR  &= ~(1U << 10);
        USB->ISTR   = 0;
        USB->BTABLE = 0;

        /* EP0 配置 */
        PMA_TABLE[0].ADDR_TX  = PMA_EP0_TX_ADDR;
        PMA_TABLE[0].COUNT_TX = 0;
        PMA_TABLE[0].ADDR_RX  = PMA_EP0_RX_ADDR;
        PMA_TABLE[0].COUNT_RX = (1U << 15) | (1U << 10);

        USB->EP[0] = 0x3200;  /* EP0, Control */
        _ep_set_rx_valid(0);

        USB->DADDR = 0x80;  /* EF=1, ADD=0 */
        s_configured   = false;
        s_tx_busy      = false;
        s_tx_count     = 0;
        s_rx_count     = 0;
        return;
    }

    /* 正确传输 */
    while (USB->ISTR & (1U << 15)) {
        uint8_t  ep  = (uint8_t)(USB->ISTR & 0xF);
        uint32_t epr = USB->EP[ep];

        if (ep == 0) {
            if (epr & EP_SETUP) {
                _ep0_handle_setup();
            } else if (epr & EP_CTR_TX) {
                /* EP0 TX 完成 */
                USB->EP[0] &= ~EP_CTR_TX;
                if (s_addr_pending) {
                    USB->DADDR = 0x80 | s_addr_pending;
                    s_addr_pending = 0;
                }
                /* 继续发送剩余数据 */
                if (s_ep0_tx_rem > 0) {
                    uint16_t chunk = (s_ep0_tx_rem > 64) ? 64 : s_ep0_tx_rem;
                    _pma_write(PMA_EP0_TX_ADDR, s_ep0_tx_ptr, chunk);
                    PMA_TABLE[0].COUNT_TX = chunk;
                    s_ep0_tx_ptr += chunk;
                    s_ep0_tx_rem -= chunk;
                    _ep_set_tx_valid(0);
                } else {
                    _ep_set_rx_valid(0);
                }
            } else if (epr & EP_CTR_RX) {
                USB->EP[0] &= ~EP_CTR_RX;
                _ep_set_rx_valid(0);
            }
        } else if (ep == 1 && (epr & EP_CTR_TX)) {
            /* EP1 TX 完成，继续发送缓冲区剩余 */
            USB->EP[1] &= ~EP_CTR_TX;
            _ep1_start_tx();
        } else if (ep == 2 && (epr & EP_CTR_RX)) {
            /* EP2 RX 收到主机数据 */
            USB->EP[2] &= ~EP_CTR_RX;
            uint16_t len = PMA_TABLE[2].COUNT_RX & 0x3FF;
            uint8_t  tmp[PMA_EP_SIZE];
            _pma_read(PMA_EP2_RX_ADDR, tmp, len);
            for (uint16_t i = 0; i < len; i++) {
                if (s_rx_count < RX_BUF_SIZE) {
                    s_rx_buf[s_rx_tail] = tmp[i];
                    s_rx_tail = (s_rx_tail + 1) % RX_BUF_SIZE;
                    s_rx_count++;
                }
            }
            _ep_set_rx_valid(2);
        } else {
            USB->EP[ep] &= ~(EP_CTR_RX | EP_CTR_TX);
        }
        USB->ISTR = ~(1U << 15);
    }
}

/* ------------------------------------------------------------------ */
/* 公开 API                                                             */
/* ------------------------------------------------------------------ */
void USB_CDC_Init(void)
{
    /* 使能 USB 和 GPIOA 时钟 */
    RCC->APB1ENR |= (1U << 23);  /* USBEN */
    RCC->APB2ENR |= (1U << 2);   /* IOPAEN */

    /* PA11(D-) PA12(D+): 浮空输入（USB 硬件接管） */
    GPIOA_USB->CRH &= ~(0xFFU << 12);  /* PA11, PA12 清零 = 模拟输入 */

    /* USB 上电，清复位 */
    USB->CNTR = (1U << 0);   /* FRES=1 */
    USB->CNTR = 0;           /* 清 FRES */
    USB->ISTR = 0;
    USB->CNTR = (1U << 10) | (1U << 15);  /* RESETM + CTRM */

    /* 使能 USB LP 中断 (IRQ 20) */
    NVIC_ISER0 |= (1U << 20);
}

void USB_CDC_SendByte(uint8_t byte)
{
    if (!s_configured) return;
    /* 有限次等待（最多 10ms），避免在 RT-Thread 中无限阻塞调度 */
    uint32_t t = 0;
    while (s_tx_count >= TX_BUF_SIZE && t++ < 1000) {
        /* 短暂让出 CPU，让 USB 中断有机会发送 */
        __asm volatile ("nop");
    }
    if (s_tx_count >= TX_BUF_SIZE) return;  /* 超时丢弃 */

    __asm volatile ("cpsid i");
    s_tx_buf[s_tx_tail] = byte;
    s_tx_tail = (s_tx_tail + 1) % TX_BUF_SIZE;
    s_tx_count++;
    bool need_start = !s_tx_busy;
    __asm volatile ("cpsie i");

    if (need_start) _ep1_start_tx();
}

void USB_CDC_SendStr(const char *str)
{
    while (*str) {
        if (*str == '\n') USB_CDC_SendByte('\r');
        USB_CDC_SendByte((uint8_t)*str++);
    }
}

void USB_CDC_SendBuf(const uint8_t *buf, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) USB_CDC_SendByte(buf[i]);
}

bool USB_CDC_ReadByte(uint8_t *byte)
{
    if (s_rx_count == 0) return false;
    __asm volatile ("cpsid i");
    *byte = s_rx_buf[s_rx_head];
    s_rx_head = (s_rx_head + 1) % RX_BUF_SIZE;
    s_rx_count--;
    __asm volatile ("cpsie i");
    return true;
}

uint16_t USB_CDC_Available(void)
{
    return s_rx_count;
}

bool USB_CDC_IsConnected(void)
{
    return s_configured;
}
