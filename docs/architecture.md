# 实际落地架构（权威）

本文是当前代码架构的**唯一权威说明**。播放内核以 `derive(Facts)` + 幂等 `reconcile()` 为核心，没有状态机。

`docs/` 下的文档随仓库跟踪；`.review/` 与 `.plan/` 均被 `.gitignore` 忽略（`:60`、`:93`），只是本地工作记录。因此架构结论必须落在本文，而不是那两个目录。

## 1. 重构计划的收口（2026-11-18）

`.review/framework_redesign_2026-10-29.md` 与 `.review/framework_redesign_plan_2026-10-30.md` 描述的那轮框架重构，**P3–P10 阶段序列整体终止**，不再按原文的阶段顺序与「完成标准」验收；已完成部分（P0 / P1 / P1b / P2a 及后续独立提交）的记录仍保留在计划 §5，仅作历史。

同时正式作废以下**目标架构概念**，代码中不存在、也不再作为实施目标：

| 概念 | 出处 |
|---|---|
| `PlatformServices`（四种平台接入机制统一为一个注入点） | 方案 §4.7、计划 P3a |
| `DecoderRegistry` / `DemuxerRegistry` / `Codec` 枚举 | 方案 §5.2、计划 P3b/P3c |
| `PlaylistLoader` / `VariantSelector`（HLS ABR 扩展缝） | 方案 §6、计划 P3d |
| `SessionCounters` / `StatsCollector` / `PlayerStats` / `ProcessUsage` | 方案 §4.10、计划 P4/P5 |
| `core/source|pipeline|sync` 目录重排方案 | 方案 §3.2、计划 P6 |

连带作废：§5.2 的 14 项改名清单（`shareInstance`→`instance`、`CTime`→`RationalTime`、`HttpMp4Source`→`HttpSource`、`Manager`→`Registry`、`Wake` 系列统一等）、§3.2 目录迁移清单、计划 §P6 的「改名规模 P6 前重新统计」。**没有结论前不做 149+ 处机械改名**；现有命名是有效命名，不是待迁移状态。

个别能力若仍有价值（指标链路、直播追赶、格式与 scheme 错误码），按独立工作项重新出方案实施，**不沿用 P4/P8 的目标架构、命名与完成标准**。`P10` 变速仍是明确的未来功能，落地前不对外暴露。

## 2. 控制面：Facts → derive → reconcile

全部权威状态只在 scheduler 线程上（`Engine.h:24`），其它线程投递命令、读快照。

| 环节 | 位置 | 语义 |
|---|---|---|
| 命令 | `Command.h:20-58` | `Prepare` / `Play` / `Pause` / `Seek` / `SetSpeed` / `SetRepeat` / `SetVolume` / `SetMute` / `Stop` / `Release`，`std::variant` |
| Facts | `EngineState.h:19-32` | `wantPlay`、`seekTarget`、`hasSession`、`info`、`failure` 等事实；**不存 Status** |
| 派生 | `EngineState.h:41-62` | `constexpr deriveStatus(state, pipelines)` → `Status`（`Status.h` 的 7 值枚举） |
| 收敛 | `Engine.cpp:476-510` | `reconcile()`：`adoptSession` / `startSeek` / `completeSeek` / `retryStuckSeek` / `repeatIfEnded` 多轮 settle 至不变，再派生 status、发通知、`applyClock()`、`publish()`、`scheduleWakeup()` |
| 快照 | `Command.h:74-89` | `Snapshot` 是对外只读视图，由 `Engine::snapshot()` 加锁返回 |
| 通知 | `Notifier.h:20-49`、`Notifier.cpp:29-45,91-93` | `Snapshot` → `PlayerState`（`toPlayerState`）；`Notification` 只发 `SeekDone` / `ErrorEvent` / `PlayEnd` / `FirstFrameRendered`；回调不在锁内执行 |

`Engine.h:86` 的 `status_` 是 `deriveStatus()` **结果的缓存**，用于 `previousStatus` 比较与快照，不是第二份权威状态；只由 `reconcile()` 写入。

`PlayerState`（`include/PlayerTypes.h:19-31`，11 值）是跨平台公开 ABI，由 `scripts/gen_player_state.py` + `PlayerStateNames.h` 的 `static_assert` + iOS `SlarkPlayer.h` 固化；内核只产出其中 7 个值。三端枚举与内核 `Status` 是不同层次，不是同一状态的两份存储。

## 3. 数据面

| 层 | 位置 | 说明 |
|---|---|---|
| Source | `src/core/engine/source/` | `FileSource`、`HttpMp4Source`、`HLSSource`、`StreamSource`（基类）、`SourceFactory`；loader 线程是 demuxer 唯一运行处，每轨一条 `Channel`，帧带 `SeekEpoch` |
| Pipeline | `src/core/engine/pipeline/` | `AudioPipeline` / `VideoPipeline` 各一条独立线程；`DecoderBuilder`、`PipelineFactory`；sink 为 `AudioRenderSink` / `VideoRenderSink` |
| 同步与策略 | `MediaClock`、`AVSync`、`BufferPolicy`、`WatermarkGate`、`AccessGate` | 纯函数策略：`deriveStatus`、`WatermarkGate`、`AVSync::decide`、`BufferPolicy` |
| 容器与传输 | `src/core/demuxer/`、`src/core/stream/`、`src/http/` | `DemuxerManager` + `Reflection` 注册表（**不是** `DemuxerRegistry`）；`M3U8Parser`、`HLSDemuxer`；`HttpProcessor` 与 `Http1Session` 两条请求流水线 |
| 平台 | `src/core/render/`、`src/platform/`、`ComponentFactories.h` | 现有工厂注入（**不是** `PlatformServices`）；`TSDemuxer` 的平台宏是文档化例外 |
| 公开入口 | `src/core/engine/Player.cpp`、`include/Player.h` | `Player` 持有 `Engine` 与 `Notifier`，对外投递命令 |

`Scheduler` 可注入，各端口有 fake，是引擎测试的主要手段。

## 4. perf 子系统现状

`src/core/perf/`、`include/IPerfReporter.h`、`PlayerTypes.h:56` 的 `perf::PerfConfig perfConfig` 是**离线会话报告模型**：采集件齐备，但 `perfConfig` 无读取点、`IPerfReporter` / `PerfMonitor` 无生产调用方、`PlayerEvent` 无指标事件，双端也不消费。这是当前缺口，不是「已迁移到 `PlayerStats`」；P4 声明的删除与新模型均未发生。补齐时按独立工作项处理，不因「零生产引用」删除基础件。

## 5. 历史文档索引

| 文档 | 状态 |
|---|---|
| `.review/framework_redesign_2026-10-29.md` | 历史方案。§0 的「保留项」与本文一致；§3–§7 的目标架构与改名/迁移清单已作废 |
| `.review/framework_redesign_plan_2026-10-30.md` | 历史计划。§5 的已完成阶段记录有效；P3–P10 章节已终止 |
| `docs/core_review_report.md` | 历史代码审查，描述已删除的 `PlayerImpl` / `PlayerStateMachine` / owner-thread 状态机架构 |
| `docs/coroutine.md` | 协程框架的权威契约与闭环总表（§8.17） |
| `.plan/project_logic_review_2026-11-18.md` | 本地静态审查记录，含当前缺口清单；非架构权威 |
