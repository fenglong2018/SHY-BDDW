#ifndef __RTCONFIG_H__
#define __RTCONFIG_H__

/* ---- RT-Thread Nano 配置 ---- */
/* 针对 N32L403KBQ7: Flash 128KB, RAM 32KB */

/* 内核基础 */
#define RT_NAME_MAX         8       /* 对象名最大长度 */
#define RT_ALIGN_SIZE       4
#define RT_THREAD_PRIORITY_MAX  8   /* 优先级数量 (0最高) */
#define RT_TICK_PER_SECOND  1000    /* 1ms tick */

/* 调试 */
#define RT_DEBUG
#define RT_USING_OVERFLOW_CHECK

/* 线程间通信 */
#define RT_USING_SEMAPHORE
#define RT_USING_MUTEX
#define RT_USING_EVENT
#define RT_USING_MESSAGEQUEUE

/* 内存管理 */
#define RT_USING_HEAP
#define RT_USING_SMALL_MEM          /* 小内存管理算法，适合 32KB RAM */
#define RT_HEAP_SIZE        (8 * 1024)  /* 8KB 堆 */

/* 定时器 */
#define RT_USING_TIMER_SOFT
#define RT_TIMER_THREAD_PRIO        4
#define RT_TIMER_THREAD_STACK_SIZE  256

/* 控制台 */
#define RT_USING_CONSOLE
#define RT_CONSOLEBUF_SIZE          128
#define RT_CONSOLE_DEVICE_NAME      "uart3"

/* 组件 */
#define RT_USING_COMPONENTS_INIT
#define RT_USING_USER_MAIN
#define RT_MAIN_THREAD_STACK_SIZE   512
#define RT_MAIN_THREAD_PRIORITY     6

/* FinSH 控制台 (可选，节省 RAM 时注释掉) */
/* #define RT_USING_FINSH */

/* ---- 低功耗配置 ---- */
/* Stop 模式下 RT-Thread tick 会停止，唤醒后需补偿 */
/* 使用 RTC 作为低功耗定时器 */
#define RT_USING_RTC

/* ---- 线程栈大小配置 ---- */
#define THREAD_STACK_GNSS       512
#define THREAD_STACK_RDSS       512
#define THREAD_STACK_BEACON     512
#define THREAD_STACK_LED        256
#define THREAD_STACK_ADC        256

/* ---- 线程优先级配置 ---- */
#define THREAD_PRIO_GNSS        3   /* 高优先级，实时解析 */
#define THREAD_PRIO_RDSS        3
#define THREAD_PRIO_BEACON      5   /* 业务逻辑 */
#define THREAD_PRIO_LED         7   /* 最低，显示 */
#define THREAD_PRIO_ADC         6

#endif /* __RTCONFIG_H__ */
