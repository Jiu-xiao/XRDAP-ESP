# XR-DAP 的 ESP32-C3 / ESP32-H2 版

`esp32c3-h2/` 是 ESP32-C3 和 ESP32-H2 共用的 ESP-IDF 工程，编译时选芯片。两种芯片的 USB 口是固定功能的 USB Serial/JTAG，电脑上显示为一个 CDC 串口。探针在这个串口上用 libxr Topic 帧提供 CMSIS-DAP（与 S3 版 UART0 上的串口链路相同），电脑上由 `xrdap-forward`（XRDAP-Host 仓库） 把串口转成 TCP，OpenOCD 用 cmsis-dap 的 TCP 后端连接。ESP32-C3 另外可以连 Wi-Fi，在 TCP 3241 端口直接提供 CMSIS-DAP，与 S3 版相同。ESP32-H2 没有 Wi-Fi，只有串口链路。

XR 厂商扩展命令（多地址读写、指针链、探针端采样）在两种芯片上都可用，行为与 S3 版一致。

ESP32-C3 已在硬件上验证（2026-10-19，原生 USB 的 C3 板，目标 STM32F401RC，杜邦线）：

- 串口链路（USB Serial/JTAG）：DAP_Info、厂商 INFO、DPIDR、读写 RAM 的 1/2/4/8 字节（写后读回一致），SWD 时钟 1、4、10、20 MHz 都没有错误。
- 探针端采样（SWD 20 MHz，经 USB Serial/JTAG 取数据）：4 个变量最短 300 µs（约 3.3 kHz）没有跳拍；1 ms、500 µs、350 µs 各 5 秒都没有跳拍、丢帧；32 个变量 2 ms 没有跳拍。周期接近单帧耗时时跳拍有计数，探针一直能响应。
- OpenOCD 经 `xrdap-forward`（Windows，带 `cmsis-dap quirk enable` 和 `min_timeout 10`）识别 Cortex-M4、读内存，导出 64 KiB SRAM 0.56 s（约 115 KiB/s，2 MHz）。
- Wi-Fi（TCP 3241，经芯片 USB 口上的命令行配网）：INFO、读写正常；4 个变量 1 ms 周期 5 秒没有跳拍，500 µs 跳 19 拍（约 0.2%），350 µs 跳得多。采样优先级排在 lwIP 之上之前，500 µs 跳 585 拍。连着 Wi-Fi 时串口采样 350 µs 也会偶尔跳拍（Wi-Fi 驱动在同一个核上）。OpenOCD 直连 C3 的 TCP（Windows，带上面两行）导出 64 KiB SRAM 0.87 s（约 74 KiB/s）。
- 芯片 USB 口上的命令行和 DAP 链路共用一个口：终端输入、DAP 读写、采样交替进行都正常，二进制数据不会让命令行输出（`tests-repo/mux_test.py`）。
- 烧录 Flash：OpenOCD `program ... verify reset` 写一个 420 字节的测试程序（与 G431 的测试程序同一份源码，换 F401 的链接脚本），经串口链路 + `xrdap-forward` 1.9 s，经 Wi-Fi 直连 2.9 s，都校验通过；复位后程序运行，变量和计数器读数正确，1 ms 采样 3 秒没有跳拍。

ESP32-H2 只验证了编译，还没有在硬件上运行过。

MSPM0G3507 LaunchPad 版见 [mspm0/README.md](mspm0/README.md)。

## 接线

两块开发板都用 GPIO5 和 GPIO4，与 S3 版相同。

| 探针 | 目标 |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

DAP 串口链路走芯片的 USB D-/D+：ESP32-C3 是 GPIO18 / GPIO19，ESP32-H2 是 GPIO26 / GPIO27。

- ESP32-H2-DevKitM-1 有两个 USB 口：一个直接接芯片的 USB（GPIO26/27），DAP 串口链路用它；另一个接板上的 USB 转串口芯片（UART0），用来看系统日志。
- ESP32-C3-DevKitM-1 板上只有一个 Micro-USB 口，接的是 USB 转串口芯片（UART0），用来烧录和配置 Wi-Fi。DAP 串口链路要另接一根 USB 线到排针：D- 接 GPIO18，D+ 接 GPIO19，再接 5V 和 GND。USB 口直接连到 GPIO18/19 的 C3 开发板不用另外接线。

引脚和其他固定参数在 `esp32c3-h2/main/config.hpp`。

## 编译和烧录

需要 ESP-IDF v5.5，并且已经拉取 libxr 子模块（见仓库根目录的 README）。在 `ports/esp32c3-h2` 目录下执行，每种芯片用各自的编译目录：

```
idf.py -B build-esp32c3 -D IDF_TARGET=esp32c3 -D SDKCONFIG=build-esp32c3/sdkconfig build
idf.py -B build-esp32c3 -D SDKCONFIG=build-esp32c3/sdkconfig -p COM5 flash
```

```
idf.py -B build-esp32h2 -D IDF_TARGET=esp32h2 -D SDKCONFIG=build-esp32h2/sdkconfig build
idf.py -B build-esp32h2 -D SDKCONFIG=build-esp32h2/sdkconfig -p COM6 flash
```

`COM5`、`COM6` 换成实际的串口。烧录可以用板上 USB 转串口芯片的串口，也可以用芯片 USB Serial/JTAG 的串口。两种芯片共用的配置在 `sdkconfig.defaults`，各自的在 `sdkconfig.defaults.esp32c3` 和 `sdkconfig.defaults.esp32h2`。

## 上位机连接

### 串口（C3 和 H2）

运行 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) 里的 `xrdap-forward`，用 `--serial` 指定 USB Serial/JTAG 的串口：

```
xrdap-forward --serial COM7 --keep-lines
```

`xrdap-forward` 默认打开串口后设置 DTR 和 RTS，这是给 USB 转串口芯片加自动下载电路的板子用的。USB Serial/JTAG 自己根据 DTR、RTS 复位芯片（esptool 就是这样复位它的），所以这里加 `--keep-lines`，不改这两根线。`--baud` 对 USB Serial/JTAG 没有作用，保持默认即可。

`xrdap-forward` 默认在 `127.0.0.1:3241` 监听。OpenOCD 要用带 TCP 后端的版本（Espressif 的 v0.12.0-esp32-20260831 及以后）：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

### Wi-Fi（只有 C3）

C3 的 Wi-Fi 命令行在两个地方都能用：

- 芯片的 USB 口（USB Serial/JTAG，也就是 DAP 串口链路那个口）。用任意串口终端打开它直接输入命令，不需要切换模式：探针自动区分 DAP 帧和人打的命令（`firmware/main/serial_mux.hpp`）。命令行只在收到一行命令并回车后才回答，一行里有控制字符时整行丢弃；最近 2 秒有 DAP 帧时不理会文字输入，所以调试工具不会收到命令行的输出。终端不要改 DTR/RTS（例如 `python -m serial.tools.miniterm COM7 115200 --dtr 0 --rts 0`），否则 USB Serial/JTAG 可能复位芯片。
- UART0（ESP32-C3-DevKitM-1 的 Micro-USB 口），115200 波特率。

命令和 S3 版相同：

```
wifi set 热点名 密码
```

`wifi status` 查看连接状态和 IP，`wifi clear` 清除热点参数，`help` 列出命令。热点参数存在 NVS 的 `rmlink` 命名空间，格式与 S3 版一致。UART0 给了命令行，所以 C3 版没有系统日志输出。

连上热点后，OpenOCD 的配置与上面相同，只把 `cmsis-dap tcp host` 换成探针的 IP，不需要 `xrdap-forward`。

## 与 S3 版的区别

- 没有 USB CMSIS-DAP 接口，Keil、pyOCD、probe-rs 不能直接使用，只能通过 OpenOCD 的 TCP 后端。
- 单核芯片，SWD、DAP 命令、采样、Wi-Fi 都在同一个核上。任务优先级见 `config.hpp`：采样任务由节拍定时器唤醒、节拍之间阻塞，排在串口、TCP 的 DAP 任务和 lwIP 之上，esp_timer 和 Wi-Fi 驱动之下；每个周期至少留 1/8 周期给别的任务。早先采样任务优先级最低时，串口取数据会挤掉节拍（4 个变量 1 ms 周期 5 秒跳 24 拍），调高后为 0。
- CPU 频率较低（C3 160 MHz，H2 96 MHz），SWCLK 能达到的最高频率比 S3 低。SWCLK 频率的校正值沿用 S3 的实测值，还没有在这两种芯片上量过。
- 与 S3 版共用 `firmware/main` 下的源文件（厂商扩展、TCP 服务、串口适配、专用 GPIO、Wi-Fi）。ESP32-H2 的 USB Serial/JTAG 驱动是从 libxr 复制的 `esp32c3-h2/main/esp_cdc_jtag_h2.*`，因为 libxr 里的那一份目前只在 C3 和 C6 上编译。
