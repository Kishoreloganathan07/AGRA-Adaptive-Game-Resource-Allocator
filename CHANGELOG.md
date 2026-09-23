# Changelog

All notable changes to the AGRA project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-09-18

### Phase 0: Repository and Architecture Skeleton
- Initial repository setup, CMake build system targeting C++20 with Ninja on Windows 10/11 x64.
- Core data types (`types.hpp`), strong process/thread primitives, policy modes, bottleneck definitions.
- Robust Win32-integrated error handling subsystem (`error.hpp`, `error.cpp`).
- Thread-safe structured logging with ANSI color formatting and persistent file sink (`logger.hpp`, `logger.cpp`).
- JSON-based application configuration manager with validation and safe fallback defaults (`config.hpp`, `config.cpp`).
- Decoupled synchronous event bus architecture (`event_bus.hpp`, `event_bus.cpp`).
- Zero-dependency modern C++20 unit test runner and initial test suite (`test_framework.hpp`).
- Comprehensive documentation skeleton (`docs/*.md`).

### Phase 1: Windows System Discovery
- CPU hardware topology via `GetLogicalProcessorInformationEx` — P/E-core hybrid detection, NUMA, per-core affinity masks.
- Process enumeration via `EnumProcesses` / `OpenProcess` with graceful access-denied fallback.
- System memory, Windows version, power state, and capability detection (CPU Sets, EcoQoS).

### Phase 2: Game Detection & Profiles
- Heuristic game detector (foreground/fullscreen window check + game database matching).
- Game database with common executable names.
- Game profile system with JSON serialisation/deserialisation (save/load/list).
- Manual attachment by PID or executable name with safety guard (blocked on protected processes).

### Phase 3: Monitoring Engine
- Background monitoring thread with condition-variable-controlled sleep (no busy-looping).
- System CPU sampler via Windows PDH (Performance Data Helper).
- Per-process CPU, memory, I/O, thread-count sampler via `PROCESS_TIMES` / `PROCESS_MEMORY_COUNTERS_EX`.
- AGRA self-overhead tracker (sampling duration in µs, CPU and memory of `agra.exe` itself).
- Contention index calculation: background CPU / available CPU headroom.
- Event-driven dispatch: `WorkloadSampleEvent` and `GameExitedEvent` published on each tick.

### Phase 4: Workload Analysis
- `RollingWindow<N>` with mean, variance, min, max, and linear-regression slope (trend detection).
- Exponentially-weighted moving average (`Ewma`) with configurable alpha.
- `WorkloadAnalyzer`: short/medium EWMA, demand estimation, spike vs. sustained detection, stability score.

## [1.1.0] - 2026-09-18

### Phase 5: Bottleneck Classifier
- Conservative rule-based `BottleneckClassifier` converting `WorkloadAnalysis` + `WorkloadMetrics` into an explainable `BottleneckResult`.
- Eight prioritised classification rules: Idle → GPU-bound → CPU-bound → Background Contention → Memory Pressure → I/O Related → Mixed → Unknown.
- Per-type confidence ceilings that reflect the epistemic limits of user-mode-only metrics (max 0.92 for CPU-bound; max 0.65 for GPU-bound heuristic).
- `requires_action` flag set only when confidence ≥ `min_confidence_threshold` AND type is CPU-actionable; GPU-bound and Memory-pressure are always informational.
- Every verdict includes a human-readable `explanation` string for UI, logs, and data export.
- 13 new unit tests covering all rules, boundary conditions, confidence ceilings, and the safety invariant that non-actionable types never produce `requires_action = true`.
- Total test suite: **44/44 pass** — 0 warnings, 0 errors.

