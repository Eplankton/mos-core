<p align="center">
<img src="pic/mos_logo.svg" width="25%">
</p>

### Introduction 🦉
```
 A_A       _    MOS Real-Time Operating System
o'' )_____//    Simple RTOS on Cortex-M
 `_/  MOS  )    Developed using C/C++
 (_(_/--(_/     [Apache License Version 2.0]
```

### Repository 📦
- **[GitHub(English)](https://github.com/Eplankton/mos-core) | [Gitee(中文)](https://gitee.com/Eplankton/mos-core)**

### Architecture 🏀
<img src="pic/mos_arch.svg">

```
.
├── 📁 config.h         // System configuration
├── 📁 arch             // Architecture-specific
│   └── cpu.hpp         // Init/Context switching asm
│
├── 📁 kernel           // Kernel code
│   ├── macro.hpp       // Constant macros
│   ├── type.hpp        // Basic types
│   ├── concepts.hpp    // Type constraints (optional)
│   ├── data_type.hpp   // Basic data structures
│   ├── alloc.hpp       // Memory management
│   ├── global.hpp      // Kernel global variables
│   ├── printf.h/.c     // Thread-safe printf(*)
│   ├── task.hpp        // Task control
│   ├── sync.hpp        // Sync primitives
│   ├── scheduler.hpp   // Scheduler
│   ├── ipc.hpp         // Inter-Process Communication
│   └── utils.hpp       // Other utilities
│
├── kernel.hpp          // Kernel module
└── shell.hpp           // Command line
```