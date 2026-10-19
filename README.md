# XRDAP-ESP32-S3

ESP32-S3 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)。可以通过 USB、串口或 Wi-Fi（TCP）连接，用来烧录和调试 SWD 目标芯片（已测 STM32G431KB）。

## 接线

| 探针 | 目标 |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

引脚驱动强度设为 1（ESP-IDF 默认是 2），减小边沿过冲和振铃。实测 0 档（最弱）在 10 MHz 以上出错更多。

## 编译和烧录

### 直接烧录 release

在 [Releases](../../releases) 下载 `xrdap-esp32-s3.bin`，这是合并好的完整镜像，从地址 0 烧录。同一个 release 里还有 ESP32-C3 / ESP32-H2 版的镜像（`xrdap-esp32-c3.bin`、`xrdap-esp32-h2.bin`，见 [ports/README.md](ports/README.md)）和 Windows、Linux 版的串口转发工具 `xrdap-forward`、命令行工具 `xrdap`（见 [XR 厂商扩展](#xr-厂商扩展)）。

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
```

### 自己编译

需要 ESP-IDF v5.5。libxr 以 git 子模块形式放在 `third_party/libxr`，跟踪 libxr 的 `feat/xrdap-serial-transport` 分支。克隆时带上 `--recurse-submodules`（已经克隆的执行 `git submodule update --init`）。

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP32-S3.git
cd XRDAP-ESP32-S3/firmware
idf.py build
idf.py -p COM3 flash
```

引脚、核分配、TCP 端口、串口波特率等固定参数在 `firmware/main/config.hpp`。每次推送和 PR 都会由 GitHub Actions 编译，打 `v*` 标签会自动发布 release。

## 上位机怎么连

探针提供 USB、串口、Wi-Fi（TCP）三种连接，三者共用同一个 DAP 实例，同一时间只用其中一种。有 TCP 客户端连接时 USB 会自动关闭；USB 和串口之间没有这样的切换，不要同时使用。

### USB

插上 ESP32-S3 的 USB 口（板上标 USB 的那个，不是串口桥那个）。设备名 `XR-DAP-S3`，VID:PID 为 `1D50:6199`（XRUSB 登记的标识），序列号是 `XRobot-XRDAP-` 加芯片唯一 ID。CMSIS-DAP v2（Bulk），Windows 自带 WinUSB，免驱。同时会多出一个 CDC 虚拟串口。

OpenOCD（Espressif v0.12.0-esp32-20260831 实测）：

```tcl
adapter driver cmsis-dap
adapter usb vid_pid 0x1d50 0x6199
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

这一版 OpenOCD 不接受 `cmsis-dap vid_pid`，要用 `adapter usb vid_pid`。同一台电脑上有多个使用这组 VID:PID 的设备时，加一行 `adapter serial <序列号>` 指定。

Keil：调试器选 CMSIS-DAP Debugger，端口选 SW，设备选目标芯片。pyOCD、probe-rs 按平常的 CMSIS-DAP 方式使用，不需要额外配置。

### 串口

探针在 UART0 上也提供 CMSIS-DAP。UART0 接板上的 USB 转串口芯片（GPIO43 TX，GPIO44 RX），也就是串口桥那个 USB 口，波特率 4 Mbaud。板上的 CH343 在 5 Mbaud 下不能正常工作，所以固定为 4 Mbaud。UART0 用于 DAP 之后，系统日志控制台已关闭。

OpenOCD 不能直接使用这个串口，电脑端用 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) 里的 `xrdap-forward` 把它转成 OpenOCD 的 CMSIS-DAP TCP 后端（在那个仓库的 Releases 下载，或用 `cargo build --release` 编译）。运行时指定探针的串口，其他参数见那个仓库的说明：

```
xrdap-forward --serial COM35
```

打开串口时，USB 转串口芯片的 DTR/RTS 会让 ESP32 复位一次。xrdap-forward 反复查询探针，等它启动完成（最长 10 秒），打印探针报告的 DAP 包长后开始监听。OpenOCD 使用 TCP 后端连接 127.0.0.1：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

OpenOCD 一侧用的是 TCP 后端，Windows 上的已知问题见下面的 Wi-Fi（TCP）一节。串口帧格式见 [docs/protocol.md](docs/protocol.md)。

### Wi-Fi 配网

探针要先知道热点的名称和密码。用 USB 连接后，电脑上会多出一个虚拟串口（设备管理器里的 `USB 串行设备`），用任意串口终端打开，波特率随意，输入：

```
wifi set 热点名 密码
```

热点名或密码里有空格时用双引号括起来；开放网络不填密码。保存后设备会重启并连接热点。其他命令：

| 命令 | 作用 |
|---|---|
| `wifi status` | 查看热点、是否连接、IP、信号强度 |
| `wifi clear` | 清除已保存的热点参数 |
| `help` | 列出命令 |

热点参数存在设备的 NVS 里（命名空间 `rmlink`，键 `wifi`），格式和 RMLink 固件一致，用 RMLink 配过网的板子不用重配。没有热点参数时 Wi-Fi 不启动，只能用 USB 或串口。

### Wi-Fi（TCP）

探针连上 Wi-Fi 后，在 TCP 3241 端口提供 CMSIS-DAP。设备的 IP 用上面的 `wifi status` 查看，也可以在路由器里查。

OpenOCD 要用带 TCP 后端的版本（Espressif 的 v0.12.0-esp32-20260831 及以后）：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 192.168.1.50   ;# 换成探针的 IP
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

注意：

- TCP 只有 OpenOCD 支持。Keil、pyOCD、probe-rs 都不支持 CMSIS-DAP over TCP。
- 有 TCP 客户端连接时，USB 会关闭（虚拟串口也会消失）；客户端断开后 USB 自动恢复。
- 同一时间只服务一个 TCP 客户端。客户端异常消失（没有正常断开）时，约 11 秒后连接会被释放。
- Windows 上要在配置里多加两行：

  ```tcl
  cmsis-dap quirk enable
  cmsis-dap tcp min_timeout 10
  ```

  这一版 OpenOCD 的 TCP 后端在 Windows 上把"暂时没有数据"当成错误（它看 `errno`，Winsock 不写 `errno`）：启动要等约 10 秒、刷屏 `error reading header`，多个请求同时在途时响应会错位，报 `command mismatch`。`quirk enable` 让它一次只发一个请求，`min_timeout 10` 把启动等待缩到约 1 秒，启动时的报错仍会打印，可以忽略。OpenOCD 上游已修复（提交 e89e73839d53），Espressif 还没有带这个修复的发布版。Linux 上不需要这两行。

## XR 厂商扩展

除标准 CMSIS-DAP 命令外，探针还实现了一组厂商命令（0x80 到 0x85）：一次读写多个地址，沿指针链读取变量，以及在探针上按固定周期采样变量、由上位机批量取回。采样由探针定时，频率不受链路往返延迟限制。USB、串口、TCP 下行为相同。

采样期间探针只接受 `DAP_Info`、`DAP_HostStatus` 和这组命令，其他命令回 `0xFF`，所以采样不能和 OpenOCD 调试同时进行。命令格式和采样规则见 [docs/protocol.md](docs/protocol.md)。

### 命令行工具 xrdap

[XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) 里的命令行工具 `xrdap` 用这组命令按变量名读写目标内存，以及在探针上采样；变量的地址和类型取自目标程序 ELF 里的 DWARF 调试信息。用法、例子和 CSV 格式见那个仓库的说明。

## 实测

目标 STM32G431KB，杜邦线连接，OpenOCD / 自写客户端测得。4 MHz 的 OpenOCD 各项和采样一项为 2026-10-18 用本仓库固件测得。

| 项目 | 结果 |
|---|---|
| USB 块读（12 MHz，4 个请求同时在途） | 约 970 KiB/s，已到全速 USB 的上限 |
| USB 块写 | 约 780 KiB/s |
| USB 下 OpenOCD 读 128 KiB（12 MHz） | 约 690 KiB/s |
| USB 下 OpenOCD 读 128 KiB（4 MHz） | 0.62 s |
| USB 下 OpenOCD 烧录 16 KiB 并校验（4 MHz） | 1.09 s |
| USB 下 OpenOCD 连接、断开 20 次（4 MHz） | 20 次全部成功，平均每次 0.09 s |
| Wi-Fi 块读 / 块写（12 MHz） | 约 380 / 450 KiB/s |
| Wi-Fi 最小命令往返 | 约 1.3 ms |
| Wi-Fi 读 4 到 8 个变量，每秒刷新次数 | 约 500 到 620 次 |
| 探针端采样 8 个条目（1、2、4、8 字节变量和一条三级指针链，SWD 设为 24 MHz） | 周期 600 µs、450 µs 没有跳拍；400 µs 及以下被 SUB_SET 拒绝 |
| 探针端采样 8 个条目（含分频），周期 2 ms，连续 5 分钟（USB） | 149999 帧，没有丢帧、跳拍，时间戳与 k × 2 ms 的偏差在 -27 到 +12 µs |
| Windows 上 OpenOCD 经串口转发读 128 KiB / 烧录 16 KiB 并校验（4 MHz，加上面两行） | 2.1 s / 2.3 s（含约 1 s 连接等待） |
| Windows 上 OpenOCD 经 Wi-Fi 读 128 KiB / 烧录 16 KiB 并校验（4 MHz，加上面两行） | 2.5 s / 3.0 s（含约 1 s 连接等待） |

### 各连接方式的极限（2026-10-19，S3 固件，SWD 请求 25 MHz，实际约 23.7 MHz）

目标 STM32G431KB，杜邦线，主机是 Windows 上的 Python 脚本，每项取 1.5 秒内的平均，一次一个请求（同步）。Wi-Fi 的往返时间平均约 2 ms、最大 6 ms，Wi-Fi 的数会随环境波动。

| 项目 | USB | 串口 4 Mbaud | Wi-Fi TCP |
|---|---|---|---|
| 读一个字（DAP 命令），次/秒 | 7,200 | 1,390 | 260 到 360 |
| 写一个字，次/秒 | 7,100 | 1,400 | 270 到 350 |
| 厂商 READ 轮询 1 个变量，次/秒 | 6,100 | 1,290 | 250 到 380 |
| 厂商 READ 轮询 8 个变量，次/秒 | 2,400 | 770 | 230 到 260 |
| 厂商 READ 轮询 32 个变量，次/秒 | 820 | 290 | 200 到 235 |
| SRAM 大块读（每包 1000 字节），KiB/s | 534 | 180 | 200 到 210 |
| SRAM 大块写，KiB/s | 429 | 185 | 190 到 220 |
| 同一个 TCP 连接一次发 4 个包：读 / 写，KiB/s | - | - | 559 / 529 |

探针端采样（SUB_SET，每个变量每拍都读，4 字节直接读）能稳定跑的最短周期：

| 变量数 | USB | 串口 | Wi-Fi |
|---|---|---|---|
| 1 | 73 µs（13.7 kHz） | 137 µs（7.3 kHz） | 236 µs（4.2 kHz） |
| 8 | 192 µs（5.2 kHz） | 286 µs（3.5 kHz） | 325 µs（3.1 kHz） |
| 32 | 603 µs（1.66 kHz） | 772 µs（1.30 kHz） | 680 µs（1.47 kHz） |

32 个变量时总吞吐 USB 约 5.3 万次/秒，串口约 4.1 万，Wi-Fi 约 4.7 万。SWD 为 4 MHz 时 32 个变量最短周期要 2 ms 以上。每多一级指针多一次读。读一个变量是 3 次 SWD 传输（写地址、读数据、再读一次 RDBUFF），25 MHz 下每个变量约 11.4 µs，其中线上时间约 6 µs。

Flash（OpenOCD，页 2 KiB，STM32G431KB，毫秒）：擦除约 16 ms 一页，不随连接方式变化；编程约 75 到 84 KiB/s，由芯片 Flash 决定。

| 项目（128 KiB） | USB | 串口转发（Windows） | Wi-Fi（Windows） |
|---|---|---|---|
| 擦除 | 2.0 s | 2.0 s | 2.0 s |
| 编程 | 1.5 s | 1.6 s | 1.7 s |
| 校验 | 0.94 s | 0.95 s | 0.98 s |
| 读出 | 0.21 s | 0.69 s | 0.68 s |

## 已知问题

- SWD 时钟：SWCLK 的高低电平等长，实际频率和请求值基本一致（S3 上 4 MHz 时实测 4.03 MHz；请求 25 MHz 及以上时用最快的一档，约 23.7 MHz，这个数是按 CPU 周期数算出来的，没有用示波器量过）。能跑多快取决于外部条件：线的长度和阻抗、目标板的上拉和走线、杜邦线质量等，先用 4 MHz。测试板用杜邦线连接，4、8、10、12、13.3、15、17.1、20 MHz 下各做 3000 轮（每轮写 1016 字节再读回比较，共约 2400 万字）没有出现应答错误和数据不一致。换了线、换了目标板都要重新测；高时钟下出问题时，先用示波器看 SWCLK 和 SWDIO 的波形，调整外围电路（缩短线、串联电阻、上拉等）。
- 旧版（libxr 自带的 SWD 实现）在 10 MHz 以上写应答常出 FAULT，并有偶发的数据错误：切换到输出的 GPIO_ENABLE 寄存器写在外设总线上，生效比预期晚，前两位数据被上拉成 1。新版在切换后读回该寄存器，等写完成再发数据。
- Windows 上 OpenOCD 的 TCP 后端问题见 Wi-Fi（TCP）一节。

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
