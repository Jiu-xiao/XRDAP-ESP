# ESP32-S2 高时钟采样跳拍：方案与清理审查

日期：2026-10-26。类型：工程方案及验收记录。基线：父仓库 cef9b5f，Core 70bbdbe，LibXR 5d795f21。

## 目标、范围和当前状态

处理 USB 采样 a+b、1 ms 下 S2 比 S3-Lite 更易跳拍的问题。保留协议、引脚、目标运行状态、帧布局和真实丢拍计数。允许 S2 缓存配置、必要的高速 SWD 定位、已授权的历史冗余代码清理。测试脚本和原始证据仅放 build-tmp 或现有本地 tools，不提交 tools。已完成本地实现、构建、USB短测及r2对照；GUI仍有缺口，未达到严格零跳拍验收。用户追加授权及时git commit；仅提交本任务代码和方案，不推送、不发布。

## 证据和可证伪假设

find.md 的 r2 历史记录：a+b/1 ms/10 s，1 MHz 10002 帧、0 跳拍、max_frame 666 us；2 MHz 10002 帧、0 跳拍、530 us；4 MHz 9924 帧、78 跳拍、820 us。buffer dropped、failed values、SWD errors、resync 均为 0。因此应先解释采样耗时增长，GUI 帧序号缺口不能直接当作 USB 丢包。

当前 ELF 核对：PacedTransfer 位于 S2 IRAM，约 1070 字节；12 个 Xfer 全部位于 Flash，3452–9488 字节（N=16 为 9488 字节）。S2 sdkconfig 指令缓存 8 KB；IDF 5.5.2 的 esp32s2/Kconfig.cache 明确说明选 16 KB 会少 8 KB 堆。S3 将热路径和一个高速档放入 IRAM，且任务分布在两个核上，不能把 S3 的表现代入 S2。

假设：高速档展开代码与其他任务争用指令缓存，拉长 SWD 请求及帧耗时，触发 TickAccount 原有公平调度间隙要求。一次传输的临界区也放大中断延迟。若只增大缓存后 4 MHz 帧时长、跳拍未改善，则缓存容量并非充分解释，应回到选档标定、实际线频率和临界区时长定位，不能继续叠加定时器补偿。

Calibrate() 的循环条件为 h < 60，因此 min_half_ 至多为 60。240 MHz/2 MHz 对应 want_x16=960，始终不满足 want_x16 < min_half_*16，故 2 MHz 必然走 paced。find.md 认为 min_half_ 约 98、2 MHz 已走汇编的推断与源码矛盾。SWJ 序列循环有额外取指开销，测速无法反推出 min_half_ 或 DAP fast_ 的档位 N。所谓缓存约导致固定 300 us 的因果关系也仍需单因素硬件验证。

## 已采用的方案与验证顺序

1. A：基于原 r2 的 C++ 源码，只把 S2 指令缓存改为 16 KB，固件后缀 cache16-r1。既有 sdkconfig 显式替换缓存 choice，核对最终生成配置差异。保留 r2 镜像、manifest 和原 S3 镜像。先构建 A，再清理源码，避免混入诊断删除。
2. B：独立清理 batch_hits 实验计数。删除其字段、原子增量、初始化和 CDC 输出。回归测试直接检查 TAR 写次数及值/错误位图，持续证明连续读取功能，不依赖产品中的实验计数。B 不进入 A 的硬件因果比较；通过回归及 S2/S3 构建后再作为下一候选。
3. 当前不实施额外IRAM搬移；以下仅为以后出现回归时的定位路径：在任务上下文导出实际 min_half、fast_half_x16、pick/函数地址和请求时钟；保持 ISR 不打印。使用实际 DAP transaction 和逻辑分析仪核对时钟，不使用 SWJ 推断。再选择一个真正高频使用的 N 移入 S2 IRAM，检查 literal、间接调用和 GPIO OE 路径，以及最小堆。不得将约 90 KB 全部搬入 IRAM。
4. 1–2 MHz 可作为临时手工设置；目前不改 GUI 默认值，不以放宽周期或隐藏 skipped 统计作为修复。

## 调用者和兼容性

| 维度 | 基线与候选 |
|---|---|
| 生产用户 | USB/TCP CMSIS-DAP、SUB_READ、CDC heap；S2/S3 普通固件与诊断配置 |
| 协议/返回值/容量 | DAP、vendor v2、帧格式、采样缓冲保持（本轮实际 S2 通告 4096 字节） |
| 数据可见性/顺序 | 不补做过期帧，失败值仍标记；批量读取保持 1 KiB TAR 边界 |
| 部分完成/失败 | 原始失败帧保留；后续限频恢复；不重放失败目标读取 |
| 阻塞/超时/完成 | 会话、锁、socket timeout、读只命令让路策略保持 |
| 所有权/生命周期 | SWD 核和任务、ring、MEM-AP 锁及 USB/TCP 会话保持 |
| 取消/调用上下文 | Stop/Start/ISR 的职责保持；不增加 ISR 日志 |
| ABI/布局 | A 仅配置；B 删除本地源码统计字段 batch_hits，无线协议不含它，Core 头文件消费者须重编 |
| 硬件副作用 | A 仅探针缓存启动配置；目标不复位/暂停/运行/写内存；调试端口配置仍按正常只读采样使用 |
| Flash/RAM/时序 | A 堆预算减少 8192 字节，性能待测；B 每批少一次 atomic 增量及 4 字节成员 |
| 诊断兼容性 | B 的 CDC sample 行移除 batch_hits；本地解析器采用通用 key=value 提取，无强制该字段，历史原始日志保留 |

## 从 d6d812d（含）起的消融审查

| 改动 | 决定 | 删除后破坏的具体需求/证据 |
|---|---|---|
| MEM-AP AP 选择、TxnOk ack 引用 | 保留 | 多 AP 选择/错误判定正确性；recovery host regression |
| EnterSwd、清 sticky、重读 CSW、恢复节流 | 保留 | 链路失步后永久错误值恢复；不能删除为清理 |
| socket 发送超时、EINTR 处理、会话结束 | 保留 | 不读回复的对端可永久阻塞；独立 socket regression |
| 会话/错误诊断、console 栈余量 | 保留 | 区分占用、SWD 故障与性能不足；本轮板测需要 |
| S2 priority 24、paced IRAM、连续字读取、read-only CommandYield | 保留 | 旧 A/B 有改善证据；批读有 TAR 边界/失败测试 |
| 自由计数 GPTimer、绝对 deadline、elapsed_ticks | 保留 | 长时间中断延迟时不低报实际漏拍；merged=0 只表示旧实验未触发 |
| one-shot wake deadline | 保留 | 避免长帧期间信号量重复累计、损害同核命令公平性 |
| sampler_timing 的延迟与 busy 分离 | 保留 | ISR 迟到不应被重复算为采样占用；现有 timing regression |
| batch_hits 统计及依赖它的测试断言 | 删除/改为流量断言 | 仅服务旧批读 A/B，非产品需求；每帧原子开销可去除 |

## 验证预算和停止条件

先完成 A 的 ESP-IDF S2 Release 构建和有效 sdkconfig 差异、image chip ID/offset/hash/ELF SHA、HTTP 实际字节校验。核对 S3 镜像 hash 未变。网页只刷探针固件，禁止勾选擦除全部（保留 Wi-Fi/NVS）。用户完成网页刷写后核对 DAP 后缀及 USB/CDC 枚举，确认 GUI 已关闭再独占 CLI。优先复用现有 runner，冻结 ELF c270a19b 和 host 3e204229；变量 a=0x20009844/b=0x20009848，目标运行状态、Wi-Fi 负载和接线保持一致。

先 1 MHz、4 MHz 各 30 s × 3（a+b/1 ms），保存 CSV、帧序缺口、skipped、dropped、failed values、max_frame/max_wake、SWD/resync、最低堆和任务栈余量。严格 A/B 需要同一当前环境下 r2 同样的 6 轮；旧 10 s 表只用于定位，不能替代新 30 s 基线。存在 NO_ACK、失败值、掉线、WDT、资源不足则停，不把它们算作性能样本，也不复位目标重试。

只有 B 对应 host 回归和 S2/S3 Release/Debug 构建通过后才交付清理候选。若短测全通过，再做 4 MHz/1 ms/30 min 和真实 GUI 同配置复核；USB 与 TCP 分别验收。零跳拍要求每轮 skipped=0、序号缺口=0、buffer dropped=0、failed values=0、SWD errors/resync=0。仅有明显改善仍报告未完全解决，不以某个最大耗时作为单独成功依据。

证据级别分别报告：静态检查 L1、host tests L2、ESP-IDF 构建 L6、硬件 L7、网页发布路径 L8。当前最高证据不能替代待测层级。

## A 候选交付证据

- 构建：ESP-IDF 5.5.2 / Xtensa GCC 14.2.0，S2 normal/Release，构建成功（L6）。
- 版本：`0.5.5-preview-s2-cache16-r1`，868640 字节；SHA256 `ef8bff035d6e88d14dbda940c7e826579d6822739190fbf5d3027e84384c2b0f`。
- ELF SHA256：`813379dc983822284f523839430de31019c84c7e05059a697c35f222b3e29921`，与镜像内嵌哈希相符。
- 有效配置与 r2 仅缓存 choice、size 和版本后缀不同；bootloader/app 芯片 ID=2，merged offset=0。
- 网页 `http://localhost:8001/` 的 S2 manifest/version/实际响应 BIN hash 已验证。S3 镜像仍为 `dc06d7b0...a641b4`。原网页 S2/metadata 保存在 `build-tmp/s2-cache-20261026/rollback-site/`；原 r2 完整镜像在 `build-tmp/s2-opt-20261026/full/s2-release/`。
- 浏览器已核对安全上下文、Web Serial、安装组件和 S2 版本；控制台 0 errors/0 warnings。尚未操作安装按钮，也未声称物理刷写或采样通过。
- 源码清理 B 在 A 构建归档后才开始。网页仍提供 A，不受后续编译产物影响。

## B 清理验证与板测前置

Core `batch_hits` 字段、初始化和每批原子增量已删除；CDC 输出同步删除；8 类批读回归改为直接断言 TAR 写次数，并保留值、失败位图和无重读检查。Core 头文件为子模块内本地修改，父仓库 gitlink 尚未提交或更新，交付需同时保留父仓库和 Core diff。

Core 测试初次在同时运行 ESP 构建时，通用线程执行器的延迟为25.53 ms，超过25 ms门限；该失败记录在 core-test.log。隔离编译负载后完整4/4通过（core-test-isolated.log），未修改门限；ESP主机测试2/2通过。clang-format21.1.8检查通过。S2/S3 Release/Debug四配置 ESP-IDF 构建通过，镜像内 ELF 哈希一致。证据位于 build-tmp/s2-cache-20261026/cleanup-build-matrix.json。清理仅达到 L2/L6；尚未对含清理的镜像做硬件验收。

用户已网页刷写 A；DAP 精确版本、USB/CDC 枚举通过。Wi-Fi status 起初显示无保存参数，尚无证据确定其原因。实际 merged 镜像分区条目证实 NVS 在0x210000，镜像长度868640，不相交；撤销曾提出的“整合镜像覆盖NVS”解释，未更改网页刷写布局。随后用用户提供参数配置2.4GHz热点，仅重启探针；目标仍在运行（DHCSR01010000）。联网后堆空闲100140、最低95300字节。冻结 host/ELF hash与原测试一致。开始6轮30秒的USB采样。

## A 联网 USB 短测（已完成，L7）

a+b / 1 ms / Wi-Fi已连接，固定host/ELF，目标持续运行。每轮CSV都是30000行、sample_tick=0..29999；逐行独立审计missing_ticks/gap_events/order_errors/time_errors/malformed_rows/failed_values全部为0。SUB_READ最终跳拍、缓冲丢帧和帧序缺口均为0，与原始CSV一致。

| 请求SWD时钟 | 每轮时长×轮数 | 帧数（各轮） | 跳拍（各轮） | 缺帧（各轮） | max_frame_us（各轮） | max_wake_us（各轮） |
|---|---|---|---|---|---|---|
| 1MHz | 30s×3 | 30000/30000/30000 | 0/0/0 | 0/0/0 | 645/644/654 | 335/328/339 |
| 4MHz | 30s×3 | 30000/30000/30000 | 0/0/0 | 0/0/0 | 678/680/690 | 320/304/304 |

所有轮次failed values、buffer dropped、SWD errors、resync、merged_ticks均为0。CSV审计证据csv-audit.json，原始文件wifi-cache16/；统计short-summary.json。仅建立该设备/接线/热点/USB CLI的6轮短测结果，不替代真实GUI、TCP、长期或含清理候选的验收。

已额外准备网页r2对照选项，精确使用bfde8d58...ffad46原始镜像，实际HTTP hash已验证；不覆盖缓存候选和S3镜像。等待用户刷回后，在同一环境复测，再恢复候选，避免只用旧历史数据建立因果结论。

## 当前物理操作与最终候选

首次请求r2对照后，实际读回仍为cache16-r1；该次未计入对照。浏览器缓存旧boards.json导致页面只显示原两项。修正本地页面board/manifest fetch为no-store，新增只包含一个r2选项的control-r2.html，验证DOM和实际HTTP镜像后重新刷写。第二次读回r2版本正确，恢复相同热点后才开始统计。镜像分区并不覆盖NVS，热点参数丢失原因仍未定位，不归因于采样修复。

最终含清理候选在独立fixed-s2.html页，仅一个选项，版本0.5.5-preview-s2-cache16-clean-r1，868576字节，SHA256af0c8b87f8f143ca93b2f3728fc910e488436dfc6067b8e8ebf7c649c8a6f132。已完成L6构建及HTTP/浏览器校验，尚待用户刷写后L7短测。该镜像不是前述6轮cache-only硬件证据的对象。

## 同环境 r2 对照（已完成，L7）

独立对照页刷入的实际固件版本为0.5.5-preview-sampling-r2，联网与目标运行状态核对后复测。相同host/ELF/变量/周期，1MHz三轮跳拍与CSV缺口为0/1/0；4MHz三轮为240/227/220。4MHz帧数29760/29773/29780，max_frame_us993/832/834，max_wake_us304/299/310。buffer dropped、failed values、SWD errors和resync全部0。原始CSV逐行复核与探针跳拍和host缺口完全一致，无乱序或时间戳错误。

| 4MHz / a+b / 1ms / 每轮30s | 跳拍 | CSV缺帧 | max_frame_us |
|---|---|---|---|
| r2，8KB缓存 | 240 / 227 / 220 | 240 / 227 / 220 | 993 / 832 / 834 |
| cache-only，16KB缓存 | 0 / 0 / 0 | 0 / 0 / 0 | 678 / 680 / 690 |

该结果支持将S2指令缓存从8KB增为16KB作为最小修复；不再追加IRAM搬移或改变定时器策略。保持原始跳拍计数，因此改善不是隐藏缺口。仍限于本轮设备与测试负载，不能推出无限时长/其他高频档位绝无丢拍。

修复候选先测、原r2后测，随后刷最终含清理版复测，构成一次受控回退与恢复。RSSI有正常波动（候选-46dBm/对照-40dBm），boot时间/Flash布局元数据也不同；不将该实验当作精确的缓存miss计数或实际SWCLK频率证明。

## 最终网页入口和身份核对

最终版首次请求后实际读回仍为cache16-r1，且服务器日志显示下载的是主页面的旧候选；该次不计入最终镜像验证。已把主页面的ESP32-S2选项和独立fixed-s2.html统一指向最终clean镜像，从主页面移除r2对照选项，原cache-only镜像改名保留。原地址http://localhost:8001/现在显示cache16-clean-r1；实际HTTP镜像hash与最终归档一致。随后读回0.5.5-preview-s2-cache16-clean-r1正确，恢复热点后才开始最终短测。

网页测试过程表明必须同时核对页面版本、实际HTTP镜像和设备读回版本；仅报告用户完成刷写不足以确认实际镜像。无效尝试均单独留档，没有计入A/B结果。

## 最终含清理候选：验收未通过

实际版本cache16-clean-r1，Wi-Fi已连接，S2/STM32身份核对通过。6轮30秒USB短测的跳拍/CSV缺口：1MHz为0/1/0，4MHz为0/1/0。每项都是实际漏拍，未计为严格零跳拍通过。4MHz最大帧耗时685/687/688us；1MHz650/660/653us。所有buffer dropped、failed values、SWD errors、resync为0，最低堆95292字节。独立CSV审计见final-csv-audit.json。cache-only先前6轮全零不能替代此最终镜像的证据；也不能仅凭各一次孤立事件断言统计清理导致性能回归。

用户实际GUI截图：25258帧、23跳拍、23缺口、缓冲丢帧0、失败值0。截图未提供精确时长。GUI产品路径不通过，故当前结论是“显著减少高时钟跳拍，尚未完全解决”，不是零缺帧修复完成。长稳测试应等待剩余抖动根因定位；不以延长已失败场景的运行代替定位。

下一步优先在GUI运行时从独立CDC采集max_frame/max_wake与会话诊断，不争用其DAP接口；确认实际GUI进程/时钟/命令路径后，分离GUI高频轮询带来的锁等待/Flash取指开销。当前mem_ap与sampler仍在S2 Flash；如同输入诊断支持，下一单因素候选仅把这两个较小热路径对象置IRAM，先算链接和堆预算，再做同配置A/B。避免改TickAccount公平间隙或隐藏skipped来使数字归零。移除批读实验统计的代码不改变实际帧协议或跳拍账目。

## 提交状态

用户于本轮追加“请及时的进行git commit”。Core先单独提交实验计数清理，父仓库再提交缓存设置、CDC输出适配、Core指针和本方案。记录已知GUI失败，不推送、不发布；仍需继续定位并单独验收后续改动。

现场CDC补充（GUI仍在运行）：117806ticks、117695frames、112skipped，max_frame_us822、max_wake_us1038，merged_ticks0，SWD failures/resync0。最低堆94816字节。GUI进程PID38588，路径Downloads/xrdap-esp32s2-usbfix/xrdap.exe。此诊断与CLI明显不同，下一步重点检查GUI命令与等待路径；不能用CLI零轮次代表GUI。

Core清理已提交为be2c067；父仓库将记录该子模块指针。

GUI新截图进一步确认USB ...2162、SWD4M、Cortex-M4r0p1运行中。实际GUI源代码app.rs在采样期间每500ms排队PollCore，worker.rs调用cortex::status，通过vendorREAD读DHCSR；CLI纯watch没有该操作。当前用单USB客户端、相同20ms取帧频率，比较有/无该500ms查询，隔离这个输入差异；尚不据静态源代码直接宣称最终根因。

## 下一单因素候选：S2 MEM-AP 放入 IRAM

500msDHCSR查询的单客户端对照首轮为8缺口，纯采样两轮1/0缺口，均没有每轮SWD/值/缓冲错误。剩余查询确实增加漏拍。候选基于fe2833a/be2c067，只把common/linker.lf中的mem_ap对象在S2也标为noflash；S3原本已经如此，不改变其策略。采样器、定时器、公平间隙、主机查询频率保持。该路径由采样和在线READ共用，减少与9.3KB高速SWD展开代码争用Flash缓存。先构建检查IRAM增量、外部Flash调用与最小堆预算，再用相同查询负载/GUI验收；未经板测不认为已解决。


500ms轮询对照已完成：纯采样1/0缺口，有DHCSR查询8/13缺口（每轮30s/59次查询），每轮SWD/缓冲/值错误0。旧预检脚本因缺少完整ADIv5初始化造成的累计SWD错误2次不属于采样轮次，各轮诊断单独为0。21个查询负载缺口中18个在查询后约22–24ms的下一次取帧中观测到，其余3个更晚；不把全部缺口都归为状态查询。

MEM-AP IRAM候选S2 Release构建通过：版本0.5.5-preview-s2-memap-iram-r1；868608字节，SHA256259c607912850e7d5bff71d4c2e7017c65f4ffab39c8f4367ef2fa519d90bf68。IRAM text从53268增至59664（+6396）；DRAM data额外12字节。ReadCsw/ReadWords实际地址0x400315a8/0x40031e0c，已进入IRAM。实际HTTP字节/版本和浏览器页面已校验，等待刷写后与同样的轮询输入比较。常驻堆余量需要实测，不以静态链接代替。

本候选S2/S3 Release/Debug四配置均构建通过（memap-build-matrix.json）。这是链接位置变更，没有新增C++运行逻辑；先前Core4/4和固件主机2/2覆盖原逻辑，不以主机测试代替IRAM/硬件时序验证。按用户及时提交要求先提交可回退的单因素候选，硬件验收仍待完成。

## 高速 SWD 档位 IRAM 候选

MEM-AP候选的GUI验证：用户日志20.70开始、98.92停止，约78.22秒，78194帧，17跳拍/17缺帧，缓冲丢帧0、失败值0。仅减少了剩余查询争用，仍未通过严格零缺帧验收。

下一单因素性能改动是将S2的Xfer<16>特化放IRAM，其余11档保留原位置，S3原N=10策略保持。SetClock选档时记录一个32位原子诊断值，CDC heap额外输出last_fast_level；它不会每帧或每次SWD事务写入。板测必须先确认4MHz确实选择index11(N16)，否则拒绝本假设。低速paced不会更新这个“最近高速档位”字段，不能据它判断当前低速路径。

S2 IRAM text从59664增至69156(+9492字节)，Xfer<16>实址0x400277e0，大小9488。只常驻一档，保留其余Flash档以控制RAM成本。S2/S3 Release/Debug首轮编译通过（包括Debug literal检查）；S3诊断保持原函数指针，S2新增的观察代码不改变S3路径。最后的小范围源代码调整后重新构建并重新校验实际镜像，再进行物理验证。

TCP用户截图为1MHz/1ms、30286帧，各项错误0；与此前USB4MHz不同，不能直接归因接口。突发buffer dropped可能来自Wi-Fi/主机取帧停顿：当前帧19字节、环形长度头2字节、4096字节缓冲可放195帧，约195ms。若仅skipped上升则须继续查采样/命令/CPU延迟，网络丢包不是已证实原因。
