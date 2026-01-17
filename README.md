<p align="center">
<img src="pic/mos_logo.svg">
</p>

# MOS Core

### About 🦉
-  **[English](https://github.com/Eplankton/mos-core) | [中文](https://gitee.com/Eplankton/mos-core)**

**MOS** is a Real-Time Operating System (RTOS) project consists of a preemptive kernel and a command-line shell(both in C++) with application components(e.g., **GuiLite** and **FatFS**).

### Repository 📦
- **[GitHub(English)](https://github.com/Eplankton/mos-core) | [Gitee(中文)](https://gitee.com/Eplankton/mos-core)**

### Architecture 🏀
<img src="pic/mos_arch.svg">

```
.
├── config.h            // System configuration
├── 📁 arch             // Architecture-specific
│   └── cpu.hpp         // Init/Context switching asm
│
├── 📁 kernel           // Kernel code
│   ├── macro.hpp       // Constant macros
│   ├── type.hpp        // Basic types
│   ├── concepts.hpp    // Type constraints
│   ├── data_type.hpp   // Basic data structures
│   ├── alloc.hpp       // Memory management
│   ├── global.hpp      // Kernel global variables
│   ├── printf.h/.c     // Thread-safe printf
│   ├── task.hpp        // Task control
│   ├── sync.hpp        // Sync primitives
│   ├── async.hpp       // Async stackless coroutines
│   ├── scheduler.hpp   // Scheduler
│   ├── ipc.hpp         // Inter-Process Communication
│   └── utils.hpp       // Other utilities
│
├── kernel.hpp          // Kernel module
└── shell.hpp           // Command line
```

---

<p align="center">
<img src="pic/osh-zh-en.svg">
</p>