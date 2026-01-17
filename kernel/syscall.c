/*
 * syscalls.c
 * Implementation of newlib system call stubs for embedded ARM (e.g., STM32/Renode)
 */

#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#undef errno
extern int errno;

/* 
 * Environment variable pointer.
 * This is a global variable required by newlib.
 */
char *__env[1] = { 0 };
char **environ = __env;

/* 
 * 1. _exit: Terminate program.
 * In embedded systems, typically implement an infinite loop or reset.
 */
void _exit(int status) {
    (void)status; // Suppress unused parameter warning
    while (1) {
        // Add reset code here if needed, e.g., NVIC_SystemReset();
    }
}

/* 
 * 2. _close: Close a file.
 * In embedded systems without a file system, simply return -1.
 */
int _close(int file) {
    (void)file;
    return -1;
}

/* 
 * 3. _fstat: Get file status.
 * Declare output streams as character devices (to make printf unbuffered).
 */
int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

/* 
 * 4. _getpid: Get process ID.
 * In a single-process environment, return 1.
 */
int _getpid(void) {
    return 1;
}

/* 
 * 5. _isatty: Check if a file descriptor refers to a terminal.
 * Return 1 to indicate it's an interactive terminal.
 */
int _isatty(int file) {
    (void)file;
    return 1;
}

/* 
 * 6. _kill: Send a signal to a process.
 * Not supported in this environment; return an error.
 */
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

/* 
 * 7. _lseek: Reposition file offset.
 * Not supported; return 0.
 */
int _lseek(int file, int ptr, int dir) {
    (void)file;
    (void)ptr;
    (void)dir;
    return 0;
}

/* 
 * 8. _read: Read from a file.
 * If you have UART reception, implement it here.
 */
int _read(int file, char *ptr, int len) {
    (void)file;
    (void)ptr;
    (void)len;
    return 0; // EOF
}

/* 
 * 9. _write: Write to a file (CRITICAL!).
 * This is the underlying implementation for printf.
 * Modify the logic here according to your hardware (e.g., UART).
 */
int _write(int file, char *ptr, int len) {
    (void)file;
    int i;
    for (i = 0; i < len; i++) {
        // If you have a UART send function, uncomment below and call it
        // __io_putchar(ptr[i]); 
        // or: UART_SendChar(ptr[i]);
    }
    return len;
}

/* 
 * 10. _sbrk: Heap memory allocation (required for malloc).
 * This function must be correctly implemented if new/malloc are used.
 */
extern char _end; // Heap start address defined in linker script (typically after .bss)
static char *heap_end = 0;

caddr_t _sbrk(int incr) {
    char *prev_heap_end;
    
    // Initialize heap_end
    if (heap_end == 0) {
        heap_end = &_end;
    }

    prev_heap_end = heap_end;
    
    // Check if heap would collide with stack (simple increment; stack collision detection is recommended)
    // Simple detection example:
    // char * stack_ptr;
    // __asm volatile ("mov %0, sp" : "=r" (stack_ptr));
    // if (heap_end + incr > stack_ptr) { ... error ... }

    heap_end += incr;
    return (caddr_t) prev_heap_end;
}