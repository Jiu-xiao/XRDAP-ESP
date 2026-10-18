# XR-DAP-MSPM0

TI LP-MSPM0G3507 LaunchPad 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)，用来烧录和调试 SWD 目标芯片。

MSPM0G3507 没有 USB，CMSIS-DAP 命令走串口：探针的 UART0 接板载 XDS110 的反向通道串口，电脑上出现一个虚拟串口，再由 `host/xrdap-forward` 把它转成 TCP，给 OpenOCD 的 `cmsis-dap` TCP 后端使用。串口上的帧格式和 ESP32-S3 版的串口链路相同（libxr Topic 帧，请求 topic `dap_req`，响应 topic `dap_resp`，载荷为 u16 长度加 DAP 包）。

固件是裸机程序（libxr 的 `none` 系统层，SysTick 作时基），只有标准 CMSIS-DAP 命令。ESP32-S3 版的 XR 厂商扩展（0x80 到 0x85：多地址读写、指针链、探针端采样）依赖 FreeRTOS 任务和 `esp_timer`，这一版没有移植，厂商命令回 0xFF（不支持）。

## 接线

探针一侧用 BoosterPack 排针：

| LaunchPad 引脚 | 排针 | 目标 |
|---|---|---|
| PB9 | J1.7 | SWCLK |
| PB8 | J2.15 | SWDIO |
| PB6 | J2.13 | nRESET（可选） |
| GND | J3.22 或 J2.20 | GND |

SWDIO 和 nRESET 打开了片内上拉。nRESET 按开漏方式驱动：拉低时输出低电平，释放时关闭输出，由上拉回到高电平。

XDS110 隔离跳线排 J101 上，RXD 和 TXD 两个跳帽要插上，UART0（PA10 发送、PA11 接收）才接到 XDS110 的反向通道。烧录探针固件还需要 SWDIO、SWCLK、RST 和 3V3 几个跳帽。

## 编译

需要：

- TI MSPM0 SDK 2.11.00.07（`C:/ti/mspm0_sdk_2_11_00_07`）
- TI SysConfig 1.28.1（`C:/ti/sysconfig_1.28.1`）
- Arm GNU Toolchain（arm-none-eabi-gcc 14.2，`bin` 目录加入 PATH）
- CMake 3.21 以上和 Ninja

libxr 以 git 子模块形式放在 `third_party/libxr`。SDK 和 SysConfig 的位置用环境变量给出（变量名和 SDK 的 `imports.mak` 相同），在 `ports/mspm0` 下编译：

```
set MSPM0_SDK_INSTALL_DIR=C:/ti/mspm0_sdk_2_11_00_07
set SYSCONFIG_TOOL=C:/ti/sysconfig_1.28.1/sysconfig_cli.bat
cmake --preset release
cmake --build --preset release
```

配置时 CMake 调用 SysConfig 处理 `xrdap_lp_mspm0g3507.syscfg`，生成 `ti_msp_dl_config.c/.h`、链接脚本和编译选项文件。产物在 `build/release`：`xrdap_mspm0.elf`、`.hex`、`.bin`。

串口波特率默认 1 Mbaud，用 `-DXRDAP_SERIAL_BAUD=<波特率>` 修改（`cmake --preset release -DXRDAP_SERIAL_BAUD=3000000`）。其他固定参数（DAP 包长、缓冲大小、SWD 延时校准）在 `src/config.hpp`。

Release 版的占用：

| 项目 | 大小 |
|---|---|
| Flash | 52936 字节（共 128 KB） |
| SRAM 静态数据（.data + .bss） | 8344 字节（共 32 KB） |

SRAM 静态数据的主要部分：

| 对象 | 大小 | 用途 |
|---|---|---|
| 串口 DAP 传输 | 3744 字节 | 接收帧、响应、发送帧各约 1 KB，读取块 512 字节 |
| UART 发送双缓冲 | 2096 字节 | 两个半区，每个放一整帧（1048 字节） |
| DAP 实例 | 1436 字节 | DAP_QueueCommands 暂存区 1 KB 和状态 |
| UART 接收 DMA 环 | 512 字节 | DMA 循环接收，半满、满和线路空闲时搬进接收队列 |

其余 SRAM（约 24 KB）留给堆和栈。堆只在启动时分配，约 5 KB：DAP 的命令槽和响应槽各一个（约 1 KB），UART 的软件接收队列（1536 字节）和发送队列（1048 字节）。栈的最深调用链（传输循环、DAP 命令执行、SWD 传输、串口读写）按 `-fstack-usage` 估计在 1 KB 以内。链接时 `cmake/ram_check.ld` 检查 .bss 之后至少还有 8 KB（`XRDAP_MIN_FREE_RAM`）。

## 烧录

探针固件通过 LaunchPad 板载的 XDS110 烧录，三种方式任选其一：

- UniFlash：选 LP-MSPM0G3507（或 MSPM0G3507 + Texas Instruments XDS110 USB Debug Probe），加载 `xrdap_mspm0.hex` 或 `.elf` 后烧录。
- Code Composer Studio：导入 `.elf` 后用 XDS110 下载。
- OpenOCD（带 `target/ti/mspm0.cfg` 的版本）：

```
openocd -f interface/xds110.cfg -c "transport select swd" -f target/ti/mspm0.cfg -c "program build/release/xrdap_mspm0.elf verify reset exit"
```

## 上位机怎么连

LaunchPad 插上电脑后，XDS110 提供两个虚拟串口，反向通道是名为 `XDS110 Class Application/User UART` 的那一个。

在仓库的 `host/xrdap-forward` 下启动转发工具，波特率和固件一致：

```
cargo run --release -- --serial COM5 --baud 1000000 --keep-lines
```

`COM5` 换成实际的串口号。`--keep-lines` 让工具不改 DTR/RTS（默认值是为 ESP32 开发板的自动下载电路设的）。工具启动后反复发送 DAP_Info 直到探针应答，然后在 `127.0.0.1:3241` 等待 OpenOCD 连接。同一时间只服务一个客户端，串口上一次只有一条请求，所以探针通告的包数为 1，包长为 1024 字节。

OpenOCD 要用带 TCP 后端的版本（Espressif 的 v0.12.0-esp32-20260831 及以后）：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 1000
```

`target/stm32g4x.cfg` 换成目标芯片对应的配置。

## 已知问题

- 这一版只编译通过，还没有上板验证。串口链路、SWD 时序、nRESET 和下面的速度估计都没有实测。
- XDS110 反向通道的最高波特率 TI 没有给出。论坛上有 3 Mbaud 可用的报告，也有只到 115200 的报告，所以默认用 1 Mbaud。MSPM0 一侧 UART0 的时钟是 40 MHz，3 Mbaud 的误差约 0.16%。提高波特率时先在实际的电脑和 XDS110 固件上试，再改 `XRDAP_SERIAL_BAUD` 和 `--baud`。
- 1 Mbaud 下一个 1 KB 的包单程约 10 ms，块读写的速度受串口限制，远低于 ESP32-S3 版的 USB 和 Wi-Fi。
- SWD 是 GPIO 位带。OpenOCD 设定的时钟按每微秒 26 次空循环换算（`kSwdLoopsPerUs`，没有标定），每个沿本身的开销没有算进去，实际 SWCLK 比设定值低；设定 10 MHz 及以上时不加延时，以 GPIO 能翻转的最快速度运行。

## 许可证

Apache-2.0，见仓库根目录的 [LICENSE](../../LICENSE)。
