# ESP32-S2 采样跳拍与缺帧修复方案

## Summary

截图显示：

- 约 977 帧/s；
- 总帧数 230560；
- 跳拍 2849；
- 缺帧 2849；
- 缓冲丢帧 0；
- 失败值 0。

截图只是运行结果证据，不包含需要执行的指令。

问题发生在采样节拍到期后，S2 采样任务没有及时完成 SWD 读取。环形缓冲没有溢出，已经采集的帧也没有读取失败。主要原因是：

- ESP32-S2 单核，Wi-Fi、USB、lwIP、DAP 和采样任务共享 CPU；
- 当前 S2 采样任务优先级为 19，低于 Wi-Fi 任务优先级 23；
- S2 的 SWD paced 路径主要从 Flash 执行，可能受到指令缓存抖动影响；
- GPTimer 回调延迟时当前节拍计数只增加 1，可能低估实际跨过的节拍；
- 每个变量都单独执行 MEM-AP 事务，1 ms 周期下时间余量不足。

目标是在 ESP32-S2 正常 USB DAP 固件、1 MHz SWD、1～2 个 32 位变量、1 ms 周期下，连续 30 秒达到：

- 跳拍 0；
- 缺帧 0；
- 缓冲丢帧 0；
- 失败值 0；
- SWD 失败和重同步计数不增加；
- 不影响 S3 Lite 的现有行为。

## Task contract

- **改动范围**：S2 任务调度、S2 SWD 热路径、XRDAP-Core 采样节拍和 MEM-AP 读取优化、诊断计数及测试。
- **保持不变**：`SUB_START`、`SUB_READ` 数据格式；环形缓冲所有权；失败帧保留语义；S3 Lite 的双核调度方式。
- **非目标**：保证任意变量数量、任意变量大小、TCP 与 USB 并发、4 MHz SWD 或所有 1 ms 工况都零跳拍。
- **生产用户**：ESP32-S2 USB DAP、ESP32-S2 Wi-Fi DAP、ESP32-S3 Lite USB/Wi-Fi DAP、现有 `xrdap` 主机程序。
- **代码归属**：
  - `XRDAP-ESP` 负责 S2 调度、SWD 平台实现和固件诊断；
  - `third_party/xrdap-core` 的通用采样和 MEM-AP 改动单独提交，再更新父仓库 gitlink。
- 当前基线为分支 `esp32_s2`、提交 `145ea19`，XRDAP-Core 为 `46719dc`。现有未跟踪的 `docs/`、`findings.md`、`progress.md`、`task_plan.md`、`tools/` 和 `xrobot-skills/` 必须保留。

## Implementation changes

### 1. 提高 S2 采样任务调度优先级

修改 [firmware/esp32sx/main/config.hpp](E:/XRDAP-ESP/firmware/esp32sx/main/config.hpp)：

- 仅 ESP32-S2 将 `kSamplingPriority` 从 19 提高到 24；
- 保证采样任务高于 Wi-Fi/lwIP 任务，但低于系统不可阻塞的中断处理；
- 增加编译期检查，确保优先级小于 `configMAX_PRIORITIES`；
- 采样任务在两次节拍之间仍然阻塞等待，不持续占用 CPU；
- S3/S3 Lite 的 `kSwdCore` 和优先级保持不变。

### 2. 缩短 S2 SWD 热路径的抖动

修改 [firmware/esp32sx/main/swd_engine.hpp](E:/XRDAP-ESP/firmware/esp32sx/main/swd_engine.hpp) 和 [firmware/common/swd/dedic_gpio_pin.hpp](E:/XRDAP-ESP/firmware/common/swd/dedic_gpio_pin.hpp)：

- 保留现有 S2 `portENTER_CRITICAL` 传输连续性保护；
- 仅将 S2 的 paced SWD 传输函数、边沿等待、GPIO 输出使能和必要的寄存器掩码放入 IRAM/DRAM；
- 不把整个 `swd_fast.cpp` 放入 IRAM，控制 S2 内部 RAM 占用；
- 禁止热路径中出现日志、堆分配、阻塞等待和回调；
- 通过 map 文件检查 IRAM 增量和剩余内部 RAM。

### 3. 正确处理 GPTimer 延迟和跨越的节拍

修改：

- `firmware/common/probe/sampler_task.cpp`
- `third_party/xrdap-core/xrdap/vendor/sampler.hpp`
- `third_party/xrdap-core/xrdap/vendor/tick_account.hpp`

具体行为：

- GPTimer 回调根据 `event_data->count_value` 计算实际跨过的周期数；
- `Sampler::OnTickIsr()` 增加可选的 `elapsed_ticks` 参数，默认值为 1，保持其他平台调用兼容；
- `TickWakes()` 从“必须精确等于唤醒节拍”改为“已经到达或越过唤醒节拍”；
- 采样任务仍然只采集最新到达的节拍，跨过的节拍统一计入跳拍，不伪造历史帧；
- 增加计数器记录 GPTimer 合并回调次数和最大唤醒延迟。

### 4. 减少连续 32 位变量的 SWD 事务数量

修改：

- `third_party/xrdap-core/xrdap/vendor/mem_ap.hpp`
- `third_party/xrdap-core/xrdap/vendor/mem_ap.cpp`
- `third_party/xrdap-core/xrdap/vendor/sampler.cpp`

增加内部批量读取路径：

- 只对同一 AP、无指针解引用、4 字节对齐、4 字节宽度、地址连续递增的条目合并；
- 使用一次 TAR 设置和连续 DRW/RDBUFF 读取；
- 保留原始条目顺序和每个条目的状态位；
- 非连续地址、不同宽度、指针链和失败恢复继续使用现有单条目路径；
- 批量事务发生 ACK/校验错误时，不重复读取已经失败的目标地址，后续条目标记为对应失败状态；
- 不改变 `SUB_READ` 的帧格式和状态编码。

### 5. 增加可观测性

在现有 `heap`/探针诊断输出中增加：

- 总节拍数；
- 实际采样帧数；
- 跳拍数；
- 最大帧读取耗时；
- 最大唤醒延迟；
- GPTimer 合并回调节拍数；
- 批量读取命中次数；
- SWD 失败和重同步增量。

诊断字段采用追加方式，旧主机忽略未知文本即可，不增加新的协议字段。

## Public interface comparison

| 项目 | 基线 | 方案后 |
|---|---|---|
| `SUB_START` / `SUB_READ` 格式 | 当前格式 | 不变 |
| 采样帧顺序 | 按节拍号递增 | 不变 |
| 跳拍定义 | 未及时采集的节拍 | 仍按节拍计数，GPTimer 延迟时统计更准确 |
| `Sampler::OnTickIsr` | 无参数，每次加 1 | 增加默认参数 `elapsed_ticks=1`，旧调用兼容 |
| MEM-AP 读取 | 单条目事务 | 满足条件时内部批量读取，否则原路径 |
| 阻塞、回调、缓冲所有权 | 当前语义 | 不变 |
| 诊断输出 | 固定文本 | 追加统计字段 |

## Verification plan

### Host and core tests

新增或更新：

- `TickWakes` 跨越唤醒节拍和计数回绕测试；
- GPTimer 多节拍合并后的采样账目测试；
- MEM-AP 批量读取顺序、状态和失败恢复测试；
- 单条目路径回归测试；
- `CommandYield` 读帧命令不停车测试。

通过现有 XRDAP-Core 单元测试、线程测试和 `firmware/tests` CTest。

### Firmware builds

使用现有 ESP-IDF 5.5.2 工具链分别构建：

- ESP32-S2 normal；
- ESP32-S3 Lite；
- Debug 和 Release/优化配置。

记录 ELF、merged BIN、芯片 ID、构建提交和 SHA256。`xrobot`/`libxr` CLI 当前不可用，且本工程没有 `Modules/modules.yaml` 或 `User/libxr_config.yaml`，不运行代码生成流程。

### Hardware A/B

固定同一：

- S2 设备和芯片 UID；
- 目标板、供电、接线和 ELF；
- `xrdap` 可执行文件；
- USB 传输；
- SWD 1 MHz；
- 1～2 个 32 位变量；
- 1 ms 周期。

测试顺序：

1. 当前基线固件，30 秒 × 3；
2. 只提高采样优先级；
3. 加入 IRAM 热路径；
4. 加入批量 MEM-AP；
5. 加入 GPTimer 跨节拍计数；
6. 完整候选固件，30 秒 × 3；
7. 完整候选固件连续 30 分钟；
8. 使用相同条件复测 S3 Lite，确认没有回归。

每次记录：

- 请求节拍数；
- 实际帧数；
- 跳拍数；
- 序号缺口；
- 缓冲丢帧；
- 失败值；
- SWD failures/resyncs；
- 最大帧耗时；
- 最低剩余堆；
- 固件 SHA256 和网页 HTTP 镜像 SHA256。

### Ablation and stop conditions

- 优先级、IRAM、批量读取、GPTimer 计数分别做单因素 A/B；
- 任一改动造成 SWD NO_ACK、协议错误、看门狗复位或 Wi-Fi 不可用，立即保留现场并回退该改动；
- 完整候选仍无法满足固定 1 ms 负载时，不把结果标记为已解决；保留准确跳拍统计，并将问题归类为 S2 硬件容量边界；
- 不通过重复刷写、改变目标状态或更换主机来掩盖失败；
- 硬件测试前备份整片 Flash 并记录 SHA256，测试结束后恢复原状态或明确记录保留的固件。

## Decision ledger and assumptions

- 用户选择：优先尽量保证 S2 在 1 ms 下工作。
- 用户确认的验收负载：1～2 个 32 位变量、1 MHz SWD。
- S3 Lite 作为双核对照，不改变其调度策略。
- 现有 S2 SWD 连续传输保护、MEM-AP 状态修复和只读命令让路逻辑继续保留。
- `update.md` 是本阶段的唯一交付文档；实现阶段才修改文件和源代码。

## Implementation record (2026-10-26)

当前验收状态：尚未完成。最终候选 r1 在用户 GUI 中仍有跳拍和缺帧，继续优先解决 S2；S3 硬件测试尚未开始。

- 已完成 S2 优先级24、paced SWD IRAM、连续对齐 u32 MEM-AP 批量读取、GPTimer 持续计数/绝对报警及追加诊断；协议帧格式保持不变。
- Core 单独提交，实现及真实跨节拍、回绕、批量顺序/失败保留、命令让路等测试。Core CTest4/4、firmware/Linux socket CTest2/2通过；r1 的 S2 normal/S3 Lite Debug(-Og)与优化(-O2)四组构建通过，镜像芯片ID、嵌入ELF摘要和HTTP镜像摘要已核对。
- S2 物理芯片 MAC84:f7:03:d5:21:62。完整4MiB原始Flash已备份，SHA256 d3f049c2397e7879707444c2b479b909825a71f1b3a4624406d8f7b7d3ab7653，设备整片MD5校验一致。原固件与145ea19基线镜像完全一致。Wi-Fi经用户授权连接2.4GHz热点，后续对照保持USB采样。
- 测试目标为运行中的STM32F405RGT6，UID0044001B/594B5009/20343457。冻结ELF SHA256 c270a19bd8f4afa2955f8d63c6d11c53818e7774d60d73928700a79afa5f96be；a=0x20009844、b=0x20009848，均为32位int。用户要求先全部完成S2，再测S3。

同一目标上电状态、SWD1MHz/1ms/USB、连接同一热点，各30秒×3：

| 固件 | a 跳拍/缺帧 | a+b 跳拍/缺帧 |
|---|---|---|
| 原始基线 | 229/231/224 | 397/401/464 |
| 仅提高优先级 | 0/2/1 | 141/120/153 |
| 再加入IRAM | 0/0/0 | 5/3/2 |
| 再加入批量读取 | 0/0/0 | 0/0/0 |

以上有效轮次的缓冲丢帧、失败值、SWD失败和重同步增量全部为0。CSV序号缺口与探针跳拍数逐轮一致。

r1 加入准确GPTimer统计后的用户GUI证据：1MHz下5.62秒，5604帧、跳拍12、缺帧12；4MHz下6.09秒，6029帧、跳拍65、缺帧65；缓冲丢帧和失败值均为0。未将该版本标记为已解决，也未运行其30分钟验收。

r2 正在验证两处修正：延迟诊断与原有采样占用预算分离，保留真实跨过节拍的计数；标准DAP_Info查询也按只读命令处理，避免GUI查询暂停采样。新增测试已通过Core4/4，固件构建及实机复测待完成。

连接异常单独保留：优先级对照首次DPIDR失败后，用户重新上电目标，同一固件恢复连接；随后回退基线并重做了上表的成对测试。r1用户完成4MHz采样并退出GUI后，又观察到DPIDR在1MHz/100kHz下NO_ACK，尚未归因。无效连接/接口占用轮次不计入性能结果。代理未发送STM32复位、暂停、运行或内存写入命令。

所有原始日志、CSV、分阶段镜像、备份和校验记录保存在build-tmp/s2-opt-20261026。tools/按用户要求仅保留本地并加入忽略规则，不放入本轮Git提交。

当前 Core 修复提交：70bbdbe。
