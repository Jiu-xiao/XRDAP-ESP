# XR-DAP 的 ESP32-C3 / ESP32-H2 版

`esp32c3-h2/` 是 ESP32-C3 和 ESP32-H2 共用的 ESP-IDF 工程，编译时选芯片。两种芯片的 USB 口是固定功能的 USB Serial/JTAG，电脑上显示为一个 CDC 串口。探针在这个串口上用 libxr Topic 帧提供 CMSIS-DAP（与 S3 版 UART0 上的串口链路相同），电脑上由 `host/xrdap-forward` 把串口转成 TCP，OpenOCD 用 cmsis-dap 的 TCP 后端连接。ESP32-C3 另外可以连 Wi-Fi，在 TCP 3241 端口直接提供 CMSIS-DAP，与 S3 版相同。ESP32-H2 没有 Wi-Fi，只有串口链路。

XR 厂商扩展命令（多地址读写、指针链、探针端采样）在两种芯片上都可用，行为与 S3 版一致。

目前两个版本只验证了编译，还没有在硬件上运行过。

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

在仓库根目录下编译并运行 `xrdap-forward`（需要 Rust），用 `--serial` 指定 USB Serial/JTAG 的串口：

```
cd host/xrdap-forward
cargo run --release -- --serial COM7 --keep-lines
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

C3 的 Wi-Fi 命令行在 UART0 上，也就是 ESP32-C3-DevKitM-1 的 Micro-USB 口，串口终端用 115200 波特率。命令和 S3 版相同：

```
wifi set 热点名 密码
```

`wifi status` 查看连接状态和 IP，`wifi clear` 清除热点参数，`help` 列出命令。热点参数存在 NVS 的 `rmlink` 命名空间，格式与 S3 版一致。UART0 给了命令行，所以 C3 版没有系统日志输出。

连上热点后，OpenOCD 的配置与上面相同，只把 `cmsis-dap tcp host` 换成探针的 IP，不需要 `xrdap-forward`。

## 与 S3 版的区别

- 没有 USB CMSIS-DAP 接口，Keil、pyOCD、probe-rs 不能直接使用，只能通过 OpenOCD 的 TCP 后端。
- 单核芯片，SWD、DAP 命令、采样、Wi-Fi 都在同一个核上。任务优先级见 `config.hpp`：采样任务最低（短周期时会忙等），串口和 TCP 的 DAP 任务高于采样、低于 lwIP 和 Wi-Fi。采样期间空闲任务得不到运行，所以关掉了空闲任务的看门狗检查。
- CPU 频率较低（C3 160 MHz，H2 96 MHz），SWCLK 能达到的最高频率比 S3 低。SWCLK 频率的校正值沿用 S3 的实测值，还没有在这两种芯片上量过。
- 与 S3 版共用 `firmware/main` 下的源文件（厂商扩展、TCP 服务、串口适配、专用 GPIO、Wi-Fi）。ESP32-H2 的 USB Serial/JTAG 驱动是从 libxr 复制的 `esp32c3-h2/main/esp_cdc_jtag_h2.*`，因为 libxr 里的那一份目前只在 C3 和 C6 上编译。
