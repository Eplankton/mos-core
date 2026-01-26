#ifndef _MOS_ARCH_CORTEX_M4_
#define _MOS_ARCH_CORTEX_M4_

#include "stm32f4xx.h"
#include "core_cm4.h"
#include "../config.h"

#define MOS_REBOOT()              NVIC_SystemReset()
#define MOS_TRIGGER_SVC_INTR()    asm volatile("svc 0")
#define MOS_TRIGGER_PENDSV_INTR() SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk
#define MOS_SVC_HANDLER           SVC_Handler
#define MOS_PENDSV_HANDLER        PendSV_Handler
#define MOS_SYSTICK_HANDLER       SysTick_Handler
#define MOS_TEST_IRQ()            __get_PRIMASK() == 0
#define MOS_DISABLE_IRQ()         __disable_irq()
#define MOS_ENABLE_IRQ()          __enable_irq()
#define MOS_NOP()                 asm volatile("nop")
#define MOS_DSB()                 __DSB()
#define MOS_ISB()                 __ISB()
#define MOS_WFI()                 __WFI()

#if (MOS_CONF_USE_HARD_FPU == true)
// --------------------------------------------------------------------------
// Start the first task (SVC Handler)
// Features:
// - Restore LR from stack to determine FPU usage.
// - Conditionally restore FPU context (S16-S31) based on LR.
// --------------------------------------------------------------------------
#define ARCH_INIT_ASM                                                                \
	"cpsid   i\n" /* Disable interrupts */                                           \
	"ldr     r3, =cur_tcb\n"                                                         \
	"ldr     r1, [r3]\n"                                                             \
	"ldr     r0, [r1,#8]\n"       /* r0 = cur_tcb.sp */                              \
	"ldmia   r0!, {r4-r11, lr}\n" /* Pop core registers R4-R11 and EXC_RETURN(LR) */ \
	"tst     lr, #0x10\n"         /* Check Bit 4 of LR (0=FPU used, 1=Not used) */   \
	"it      eq\n"                /* If Bit 4 == 0 */                                \
	"vldmiaeq r0!, {s16-s31}\n"   /* Restore FPU registers S16-S31 */                \
	"msr     psp, r0\n"           /* Update PSP */                                   \
	"mov     r0, #0\n"                                                               \
	"cpsie   i\n"  /* Enable interrupts */                                           \
	"bx      lr\n" /* Jump to task (LR contains mode info) */

// --------------------------------------------------------------------------
// Context Switch (PendSV Handler)
// Features:
// - Check if current task uses FPU; if so, save S16-S31.
// - Save LR (EXC_RETURN) to stack to preserve FPU state.
// - Restore new task's LR and conditionally restore FPU context.
// --------------------------------------------------------------------------
#define ARCH_CONTEXT_SWITCH_ASM                                             \
	"cpsid   i\n" /* Disable interrupts */                                  \
	"mrs     r0, psp\n"                                                     \
	"tst     lr, #0x10\n" /* Test Bit 4 of LR */                            \
	"it      eq\n"                                                          \
	"vstmdbeq r0!, {s16-s31}\n" /* If FPU used, save high vfp registers */  \
	"ldr     r3, =cur_tcb\n"                                                \
	"ldr     r2, [r3]\n"                                                    \
	"stmdb   r0!, {r4-r11, lr}\n" /* Save core registers R4-R11 and LR */   \
	"str     r0, [r2,#8]\n"       /* Update cur_tcb.sp */                   \
	"stmdb   sp!, {r3,lr}\n"                                                \
	"bl      next_tcb\n" /* Select next TCB */                              \
	"ldmia   sp!, {r3,lr}\n"                                                \
	"ldr     r1, [r3]\n"                                                    \
	"ldr     r0, [r1,#8]\n"       /* Get cur_tcb.sp (new) */                \
	"ldmia   r0!, {r4-r11, lr}\n" /* Pop core registers R4-R11 and LR */    \
	"tst     lr, #0x10\n"         /* Test restored LR Bit 4 */              \
	"it      eq\n"                                                          \
	"vldmiaeq r0!, {s16-s31}\n" /* If new task uses FPU, restore S16-S31 */ \
	"msr     psp, r0\n"                                                     \
	"cpsie   i\n" /* Enable interrupts */                                   \
	"bx      lr\n"
#else

// From FreeRTOS -> https://www.freertos.org
#define ARCH_INIT_ASM                                                        \
	"cpsid   i\n" /* Disable interrupts */                                   \
	"ldr     r3, =cur_tcb\n"                                                 \
	"ldr     r1, [r3]\n"                                                     \
	"ldr     r0, [r1,#8]\n"   /* r0 = cur_tcb.sp */                          \
	"ldmia   r0!, {r4-r11}\n" /* Pop registers R4-R11(user saved context) */ \
	"msr     psp, r0\n"       /* PSP = cur_tcb.sp(new) */                    \
	"mov     r0, #0\n"                                                       \
	"orr     lr, #0xD\n" /* Enter Thread Mode: 0xFFFF'FFFD */                \
	"cpsie   i\n"        /* Enable interrupts */                             \
	"bx      lr\n"

// From FreeRTOS -> https://www.freertos.org
#define ARCH_CONTEXT_SWITCH_ASM                          \
	"cpsid   i\n" /* Disable interrupts */               \
	"mrs     r0, psp\n"                                  \
	"ldr     r3, =cur_tcb\n"                             \
	"ldr     r2, [r3]\n"                                 \
	"stmdb   r0!, {r4-r11}\n" /* Save core registers. */ \
	"str     r0, [r2,#8]\n"   /* Get cur_tcb.sp */       \
	"stmdb   sp!, {r3,lr}\n"                             \
	"bl      next_tcb\n"                                 \
	"ldmia   sp!, {r3,lr}\n"                             \
	"ldr     r1, [r3]\n"                                 \
	"ldr     r0, [r1,#8]\n"   /* Get cur_tcb.sp(new) */  \
	"ldmia   r0!, {r4-r11}\n" /* Pop core registers. */  \
	"msr     psp, r0\n"                                  \
	"cpsie   i\n" /* Enable interrupts */                \
	"bx      lr\n"

#endif

#define ARCH_JUMP_TO_CONTEXT_SWITCH "b    context_switch"

#endif