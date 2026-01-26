#ifndef _MOS_CONFIG_
#define _MOS_CONFIG_

// Info Configuration
#define MOS_USER_NAME      "neo"
#define MOS_VERSION        "v0.4(beta)"
#define MOS_ARCH_CORTEX_M4 "Cortex-M4"
#define MOS_ARCH           MOS_ARCH_CORTEX_M4
#define MOS_MCU            "STM32F4xx"

// MOS Settings
#define MOS_CONF_USE_HARD_FPU       true       // Whether to use hardware FPU
#define MOS_CONF_ASSERT             true       // Whether to use full assert
#define MOS_CONF_PRINTF             true       // Whether to use printf
#define MOS_CONF_LOG_TIME           true       // Whether to add timestamp on LOG
#define MOS_CONF_DEBUG_INFO         true       // Whether to use debug info
#define MOS_CONF_MAX_TASK_NUM       16         // Maximum number of tasks
#define MOS_CONF_POOL_SIZE          16         // Size of pre-allocated page pool
#define MOS_CONF_PAGE_SIZE          1024       // Default page size in BYTES
#define MOS_CONF_SYSTICK            1000       // SystemFrequency / 1000 = every 1ms
#define MOS_CONF_PRI_INV            -1         // Invalid priority
#define MOS_CONF_PRI_MAX            0          // Max priority
#define MOS_CONF_PRI_MIN            127        // Min priority
#define MOS_CONF_TIME_SLICE         50         // Time slice width
#define MOS_CONF_SCHED_POLICY       PreemptPri // Scheduler Policy: RoundRobin, PreemptPri
#define MOS_CONF_SHELL_BUF_SIZE     32         // Shell I/O Buffer Size
#define MOS_CONF_SHELL_USR_CMD_SIZE 8          // Shell User Cmds Size
#define MOS_CONF_ASYNC_TASK_MAX     256        // Max Executor Volume
#define MOS_CONF_ASYNC_TASK_SIZE    32         // Lambda Captured Object Bytes
#define MOS_CONF_ASYNC_POOL_MAX     200        // Max Parallel Corountines Size
#define MOS_CONF_ASYNC_FRAME_SIZE   64         // Coroutines Frame Size
#define MOS_CONF_ASYNC_USE_POOL     false      // Whether to use customized pool allocator
#define MOS_CONF_USER_NAME_SIZE     8          // Size of user name

#endif