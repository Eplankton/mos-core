<p align="center">
<img src="pic/mos_logo.svg">
</p>

### 介绍 🦉
```
 A_A       _    MOS Real-Time Operating System
o'' )_____//    运行在 Cortex-M 上的简单实时操作系统
 `_/  MOS  )    使用 C/C++ 开发
 (_(_/--(_/     [Apache License Version 2.0]
```

### 仓库 📦
- **[Gitee(中文)](https://gitee.com/Eplankton/mos-core) | [GitHub(English)](https://github.com/Eplankton/mos-core)**

### 架构 🏀
<img src="pic/mos_arch.svg">

```
.
├── config.h             // 系统配置
├── 📁 arch              // 架构相关
│   └── cpu.hpp          // 初始化/上下文切换
│
├── 📁 kernel            // 内核代码
│   ├── macro.hpp        // 常量宏
│   ├── type.hpp         // 基础类型
│   ├── concepts.hpp     // 类型约束(可选)
│   ├── data_type.hpp    // 基本数据结构
│   ├── alloc.hpp        // 内存管理
│   ├── global.hpp       // 内核全局变量
│   ├── printf.h/.c      // 线程安全的 printf(*)
│   ├── task.hpp         // 任务控制
│   ├── sync.hpp         // 同步原语
│   ├── scheduler.hpp    // 调度器
│   ├── ipc.hpp          // 进程间通信
│   └── utils.hpp        // 其他工具
│
├── kernel.hpp           // 内核模块
└── shell.hpp            // 命令行
```