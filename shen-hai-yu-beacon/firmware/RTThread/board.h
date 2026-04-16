#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>

void rt_hw_board_init(void);

/* 全局 IPC 对象，在 main.c 中创建，各线程共享 */
extern rt_mq_t  mq_gnss;       /* GNSS 数据消息队列 */
extern rt_mq_t  mq_rdss_tx;    /* 待发送短报文队列 */
extern rt_sem_t sem_sos;        /* SOS 触发信号量 */
extern rt_sem_t sem_fall;       /* 落水检测信号量 */
extern rt_mutex_t mtx_status;  /* 设备状态互斥锁 */

#endif /* __BOARD_H__ */
