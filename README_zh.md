<h3 align="center">
<img src="pic/mos_logo.svg">

<a href="https://github.com/Eplankton/mos-stm32/stargazers"><img src="https://img.shields.io/github/stars/Eplankton/mos-stm32?colorA=363a4f&colorB=b7bdf8&style=for-the-badge"></a>
<a href="https://github.com/Eplankton/mos-stm32/network/members"><img src="https://img.shields.io/github/forks/Eplankton/mos-stm32?colorA=363a4f&colorB=c6a0f6&style=for-the-badge"></a>
<a href="https://github.com/Eplankton/mos-stm32/contributors"><img src="https://img.shields.io/github/contributors/Eplankton/mos-stm32?colorA=363a4f&colorB=a6da95&style=for-the-badge"></a>
<a href="https://github.com/Eplankton/mos-renode/commits"><img src="https://img.shields.io/github/last-commit/Eplankton/mos-renode?colorA=363a4f&colorB=f5a97f&style=for-the-badge"></a>

**[中文](https://gitee.com/Eplankton/mos-renode) | [English](https://github.com/Eplankton/mos-renode)**

</h3>

## 介绍 🦉

- **MOS** 是一个用 C++ 编写的实时操作系统 (Real-Time Operating System, RTOS) 项目，包含一个抢占式内核和简易命令行, 并移植了一些应用层组件 (例如：**GuiLite** 和 **FatFS** 等)。

## 仓库 📦
- **[Gitee(中文)](https://gitee.com/Eplankton/mos-core) | [GitHub(English)](https://github.com/Eplankton/mos-core)**

## 架构 🏀
<img src="pic/mos_arch.svg">

```
.
├── config.h             // 系统配置
├── 📁 arch              // 架构相关
│   └── cpu.hpp          // 初始化/上下文切换
│
├── 📁 external          // 外部库
├── 📁 kernel            // 内核代码
│   ├── macro.hpp        // 常量宏
│   ├── type.hpp         // 基础类型
│   ├── concepts.hpp     // 类型约束
│   ├── data_type.hpp    // 基本数据结构
│   ├── alloc.hpp        // 内存管理
│   ├── global.hpp       // 内核全局变量
│   ├── printf.h/.c      // 线程安全的 printf
│   ├── task.hpp         // 任务控制
│   ├── sync.hpp         // 同步原语
│   ├── async.hpp        // 异步协程
│   ├── scheduler.hpp    // 调度器
│   ├── ipc.hpp          // 进程间通信
│   └── utils.hpp        // 其他工具
│
├── kernel.hpp           // 内核模块
└── shell.hpp            // 命令行
```

---

<p align="center">
<img src="pic/osh-zh-en.svg">
</p>