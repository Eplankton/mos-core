/*
 * syscalls.c
 * Implementation of newlib system call stubs for embedded ARM (e.g. STM32/Renode)
 */

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#undef errno
extern int errno;

/* * 环境变量指针 
 * 这是 newlib 需要的一个全局变量
 */
char *__env[1] = { 0 };
char **environ = __env;

/* * 1. _exit: 退出程序
 * 在嵌入式中通常死循环或复位
 */
void _exit(int status) {
    (void)status; // 防止未使用参数警告
    while (1) {
        // 这里可以添加复位代码，例如 NVIC_SystemReset();
    }
}

/* * 2. _close: 关闭文件
 * 嵌入式通常没有文件系统，直接返回 -1
 */
int _close(int file) {
    (void)file;
    return -1;
}

/* * 3. _fstat: 获取文件状态
 * 声明输出流为字符设备（让 printf 不带缓冲）
 */
int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/* * 4. _getpid: 获取进程 ID
 * 单进程环境，返回 1
 */
int _getpid(void) {
    return 1;
}

/* * 5. _isatty: 判断是否为终端
 * 返回 1 表示是交互式终端
 */
int _isatty(int file) {
    (void)file;
    return 1;
}

/* * 6. _kill: 发送信号
 * 不支持信号，返回错误
 */
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

/* * 7. _lseek: 文件定位
 * 不支持，返回 0
 */
int _lseek(int file, int ptr, int dir) {
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/* * 8. _read: 读输入
 * 如果你有串口接收，可以在这里实现
 */
int _read(int file, char *ptr, int len) {
    (void)file;
    (void)ptr;
    (void)len;
    return 0; // EOF
}

/* * 9. _write: 写输出 (关键!)
 * 这是 printf 的底层实现。
 * 你需要根据你的硬件（如 UART）修改这里的逻辑。
 */
int _write(int file, char *ptr, int len) {
    (void)file;
    int i;
    for (i = 0; i < len; i++) {
        // 如果你有 UART 发送函数，取消下面的注释并调用它
        // __io_putchar(ptr[i]); 
        // 或者: UART_SendChar(ptr[i]);
    }
    return len;
}

/* * 10. _sbrk: 堆内存分配 (malloc 需要)
 * 如果你用到了 new/malloc，这个函数必须正确实现
 */
extern char _end; // Linker script 中定义的堆起始地址 (通常是 .bss 之后)
static char *heap_end = 0;

caddr_t _sbrk(int incr) {
    char *prev_heap_end;
    
    // 初始化 heap_end
    if (heap_end == 0) {
        heap_end = &_end;
    }

    prev_heap_end = heap_end;
    
    // 检查堆是否会通过栈 (这里只是简单增加，建议加上堆栈碰撞检测)
    // 简单的检测：
    // char * stack_ptr;
    // __asm volatile ("mov %0, sp" : "=r" (stack_ptr));
    // if (heap_end + incr > stack_ptr) { ... error ... }

    heap_end += incr;
    return (caddr_t) prev_heap_end;
}