# AGRA — Adaptive Game Resource Allocator

[![Platform: Windows 10/11 64-bit](https://img.shields.io/badge/Platform-Windows%2010%20%7C%2011%20(x64)-blue.svg)](#)
[![Language: C++20](https://img.shields.io/badge/C%2B%2B-20-brightgreen.svg)](#)
[![Build: CMake + Ninja](https://img.shields.io/badge/Build-CMake%20%2B%20Ninja-orange.svg)](#)

> **Research Question:**
> *"Can an adaptive workload-aware CPU resource allocation system improve real-time gaming performance compared with conventional OS scheduling and static optimization techniques while maintaining system fairness, stability, and recoverability?"*

---

## 1. What AGRA Is (and Is Not)

**AGRA is:**
- An adaptive, workload-aware Windows desktop system service that dynamically optimizes scheduling preferences for running PC games.
- A legitimate user-mode systems engineering application built strictly on documented Microsoft Windows Win32 APIs.
- A feedback-driven controller executing the closed loop:
  $$\text{Detect} \longrightarrow \text{Analyze} \longrightarrow \text{Classify} \longrightarrow \text{Decide} \longrightarrow \text{Allocate} \longrightarrow \text{Observe} \longrightarrow \text{Adjust} \longrightarrow \text{Rollback}$$

**AGRA is NOT:**
- A fake "FPS booster" or placebo registry cleaner.
- A kernel scheduler replacement (the Windows NT kernel remains 100% authoritative for actual CPU scheduling).
- A cheat engine, DLL injector, driver exploiter, or anti-cheat bypass.
- A magic utility that produces frame rate out of thin air when games are GPU-bound.

---

## 2. Core Architecture

```
                ┌──────────────────────────────────┐
                │      AGRA GUI / CLI              │
                └─────────────────┬────────────────┘
                                  │
                ┌─────────────────▼────────────────┐
                │        Session Manager           │
                └─────────────────┬────────────────┘
                                  │
         ┌────────────────────────┼────────────────────────┐
         │                        │                        │
  ┌──────▼──────┐          ┌──────▼──────┐          ┌──────▼──────┐
  │ Game        │          │ Workload    │          │ System      │
  │ Detection   │          │ Analyzer    │          │ Analyzer    │
  └──────┬──────┘          └──────┬──────┘          └──────┬──────┘
         │                        │                        │
         └────────────────────────┼────────────────────────┘
                                  │
                       ┌──────────▼───────────┐
                       │ Bottleneck Classifier│
                       └──────────┬───────────┘
                                  │
                       ┌──────────▼───────────┐
                       │  Allocation Decision │
                       │        Engine        │
                       └──────────┬───────────┘
                                  │
                       ┌──────────▼───────────┐
                       │ Resource Policy      │
                       │ Engine & Hysteresis  │
                       └──────────┬───────────┘
                                  │
                       ┌──────────▼───────────┐
                       │ Safety & Rollback    │
                       │ Manager (Snapshots)  │
                       └──────────┬───────────┘
                                  │
                       ┌──────────▼───────────┐
                       │ Windows Control Layer│
                       └──────────┬───────────┘
                                  │
                       ┌──────────▼───────────┐
                       │ Windows NT Kernel    │
                       └──────────────────────┘
```

---

## 3. Legitimate Windows Mechanisms Used

| Mechanism | Win32 API | Safe Range / Scope |
|---|---|---|
| **Process Priority** | `SetPriorityClass` | `NORMAL_PRIORITY_CLASS`, `ABOVE_NORMAL_PRIORITY_CLASS`, `HIGH_PRIORITY_CLASS` (Never Realtime) |
| **Process CPU Affinity** | `SetProcessAffinityMask` | Restricts background interference to designated efficiency/secondary cores |
| **CPU Sets** | `SetProcessDefaultCpuSets` | Soft partitioning supported on Windows 10/11 without hard affinity starvation |
| **QoS / Efficiency** | `SetProcessInformation` (`ProcessPowerThrottling`) | EcoQoS energy-efficiency throttling for background contention |
| **Recovery Snapshot** | `GetPriorityClass`, `GetProcessAffinityMask` | Full pre-intervention state snapshot for automatic and manual rollback |

---

## 4. Documentation Index

- [Architecture & Subsystem Design](docs/architecture.md)
- [Design Decisions & Mathematical Model](docs/design.md)
- [Safety, Security & Non-Interference Policy](docs/safety.md)
- [Windows API Specifications](docs/windows-api.md)
- [Benchmarking Methodology](docs/benchmarking.md)
- [Limitations & Honest Claims](docs/limitations.md)
- [Troubleshooting & Diagnostics](docs/troubleshooting.md)

---

## 5. Building and Running

### Prerequisites
- Windows 10 or 11 (64-bit)
- Modern C++20 compiler (GCC 13+, Clang 16+, or MSVC 2022)
- CMake 3.25+
- Ninja build tool

### Compilation
```powershell
# Configure build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Compile all targets
ninja -C build

# Run unit and integration tests
ninja -C build test
# or
./build/bin/agra_tests.exe
```

### CLI Usage
```powershell
# Display help and options
./build/bin/agra.exe --help

# Detect running games
./build/bin/agra.exe --detect

# Start adaptive optimization session
./build/bin/agra.exe --start

# Immediately restore all original process states
./build/bin/agra.exe --restore
```

---

## 6. License
MIT License. See [LICENSE](LICENSE) for details.
