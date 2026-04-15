#include <rtthread.h>
#include "msg_def.h"
#include "../RTThread/board.h"
#include "../Drivers/gpio.h"
#include "../Drivers/pwr.h"
#include "../Drivers/flash_cfg.h"
#include "../Drivers/boot_confirm.h"
#include "../RTThread/rtconfig.h"

/* 固件版本标记，固定在 0x08004200，供 Bootloader 解析 */
FW_VERSION_MARKER();

/* ---- 全局 IPC 对象 ---- */
rt_mq_t    mq_gnss        = RT_NULL;
rt_mq_t    mq_rdss_tx     = RT_NULL;
rt_sem_t   sem_sos         = RT_NULL;
rt_sem_t   sem_bat_display = RT_NULL;
rt_sem_t   sem_work_done   = RT_NULL;
rt_mutex_t mtx_status      = RT_NULL;

/* ---- 全局设备状态 ---- */
DeviceStatus_t g_status = {
    .phase      = PHASE_IDLE,
    .sos_active = RT_FALSE,
    .gnss_valid = RT_FALSE,
    .bat_mv     = 0,
    .bat_pct    = 0,
};

/* ---- EXTI 配置：PB7 (KEY_FALL/SOS) 下降沿唤醒 ---- */
static void _exti_sos_init(void)
{
    /* AFIO 时钟 */
    typedef struct {
        volatile rt_uint32_t CR; volatile rt_uint32_t CFGR; volatile rt_uint32_t CIR;
        volatile rt_uint32_t APB2RSTR; volatile rt_uint32_t APB1RSTR;
        volatile rt_uint32_t AHBENR; volatile rt_uint32_t APB2ENR;
    } RCC_T;
    #define RCC_L ((RCC_T *)0x40021000UL)
    RCC_L->APB2ENR |= (1U << 0);  /* AFIOEN */

    /* AFIO EXTICR2: EXTI7 → PB */
    volatile rt_uint32_t *EXTICR2 = (volatile rt_uint32_t *)0x4001000CUL;
    *EXTICR2 = (*EXTICR2 & ~(0xFU << 12)) | (0x1U << 12);  /* PB7 */

    /* EXTI Line7: 下降沿触发，使能中断 */
    typedef struct {
        volatile rt_uint32_t IMR, EMR, RTSR, FTSR, SWIER, PR;
    } EXTI_T;
    #define EXTI_L ((EXTI_T *)0x40010400UL)
    EXTI_L->FTSR |= (1U << 7);   /* 下降沿 */
    EXTI_L->IMR  |= (1U << 7);   /* 使能中断 */

    /* NVIC: EXTI9_5 (IRQ 23) */
    (*(volatile rt_uint32_t *)0xE000E100UL) |= (1U << 23);
}

/* ---- EXTI9_5 中断处理（PB7 在此范围内） ---- */
void EXTI9_5_IRQHandler(void)
{
    rt_interrupt_enter();
    volatile rt_uint32_t *EXTI_PR = (volatile rt_uint32_t *)0x40010414UL;
    if (*EXTI_PR & (1U << 7)) {
        *EXTI_PR = (1U << 7);  /* 清 pending */
        /* 唤醒后由 beacon 线程检测按键类型，此处仅清标志 */
    }
    rt_interrupt_leave();
}

/* 固件确认线程：启动 5s 后通知 Bootloader 新固件有效 */
static void _confirm_thread(void *param)
{
    (void)param;
    rt_thread_mdelay(5000);
    BootConfirm_OK();
    rt_kprintf("[MAIN] firmware confirmed OK\n");
}

int main(void)
{
    rt_kprintf("\n=== ShenHaiYu Beacon V1.0 (RT-Thread Nano) ===\n");
    rt_kprintf("    短按 SOS: 电量显示\n");
    rt_kprintf("    长按 SOS (3s): 启动救援模式\n\n");

    /* 创建 IPC 对象 */
    mq_gnss        = rt_mq_create("mq_gnss",  sizeof(MsgGnss_t),   4, RT_IPC_FLAG_FIFO);
    mq_rdss_tx     = rt_mq_create("mq_rdss",  sizeof(MsgRdssTx_t), 8, RT_IPC_FLAG_FIFO);
    sem_sos        = rt_sem_create("sem_sos",  0, RT_IPC_FLAG_FIFO);
    sem_bat_display= rt_sem_create("sem_bat",  0, RT_IPC_FLAG_FIFO);
    sem_work_done  = rt_sem_create("sem_wdone",0, RT_IPC_FLAG_FIFO);
    mtx_status     = rt_mutex_create("mtx_st", RT_IPC_FLAG_PRIO);

    RT_ASSERT(mq_gnss && mq_rdss_tx && sem_sos &&
              sem_bat_display && sem_work_done && mtx_status);

    /* 加载 Flash 配置（SN / 硬件版本） */
    if (!FlashCfg_Load(&g_cfg)) {
        rt_kprintf("[MAIN] flash cfg invalid, using default\n");
        FlashCfg_Default(&g_cfg);
    }
    rt_kprintf("[MAIN] SN=%s HW=%d.%d.%d\n",
               g_cfg.sn, g_cfg.hw_ver[0], g_cfg.hw_ver[1], g_cfg.hw_ver[2]);

    /* 硬件初始化 */
    GPIO_Init();
    PWR_Init();
    _exti_sos_init();

    rt_kprintf("[MAIN] init done, threads starting...\n");

    /* 延迟 5s 后确认新固件有效（防变砖回滚机制） */
    rt_thread_t confirm_tid = rt_thread_create(
        "confirm", _confirm_thread, RT_NULL, 256, 7, 10);
    if (confirm_tid) rt_thread_startup(confirm_tid);

    return 0;
}
