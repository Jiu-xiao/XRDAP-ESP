# XRDAP-ESP

[English](README.en.md)

用 ESP32-S3、ESP32-C3 或传统 ESP32 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)。插 USB 线或走 Wi-Fi 都能用，Keil、pyOCD、probe-rs、OpenOCD 把它当普通的 CMSIS-DAP 探针；另外能在探针上按固定周期采样目标的变量，配合电脑端工具画曲线。已在 STM32G431KB、STM32F401RC 上测试。

电脑端工具在 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host)：一个 `xrdap` 可执行文件，不带参数是图形界面，带命令是命令行。

## 选哪块板

| | ESP32-S3 | ESP32-C3 | 传统 ESP32 |
|---|---|---|---|
| 插 USB 线给 Keil、pyOCD、probe-rs、OpenOCD 用 | 直接用（原生 USB 探针） | 电脑上运行 `xrdap share usbip` 后用 | 同 C3（经板上 USB 转串口芯片） |
| 走 Wi-Fi 给 Keil、pyOCD、probe-rs、OpenOCD 用 | 用 USB/IP 挂载 | 用 USB/IP 挂载 | 用 USB/IP 挂载 |
| 走 Wi-Fi 给 OpenOCD 用 | OpenOCD 的 TCP 后端直连 | 同左 | 同左 |
| SWD 时钟 | 最高约 23.7 MHz | 最高约 20 MHz | 实际约 3 MHz（GPIO 寄存器位带） |
| 探针端采样的最短周期（插线） | 4 个变量 118 µs | 4 个变量 300 µs | 8 个变量 1 ms，1 个变量 200 µs |
| OpenOCD 读 128 KiB（插线） | 0.62 s | 1.8 s | 3.4 s |

要最快、最省事选 S3；手边只有 C3 或传统 ESP32 也能用，多一步 `xrdap share`。传统 ESP32 没有原生 USB 和 CPU 专用 GPIO，SWD 和串口都慢一档，调试、烧录没问题。

## 1. 准备

### 接线

| 目标 | ESP32-S3、ESP32-C3 | 传统 ESP32 |
|---|---|---|
| SWCLK | GPIO5 | GPIO18 |
| SWDIO | GPIO4 | GPIO19 |
| GND | GND | GND |

一个目标只接一个探针。传统 ESP32 的 GPIO5 是启动配置引脚，所以换成 GPIO18、GPIO19；它的串口链路在 UART0（GPIO1、GPIO3），也就是板上 USB 转串口芯片那个口，插 USB 线即可。C3 要用芯片自带的 USB 口（D- GPIO18、D+ GPIO19）：USB 座直接连到这两个脚的开发板插上就行；USB 座接的是 USB 转串口芯片的板子（例如 ESP32-C3-DevKitM-1），另接一根 USB 线到排针的 GPIO18、GPIO19、5V、GND。

### 烧固件

在 [Releases](../../releases) 下载 `xrdap-esp32-s3.bin`、`xrdap-esp32-c3.bin` 或 `xrdap-esp32.bin`（校验和在同一页的 `SHA256SUMS`），从地址 0 烧录：

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
python -m esptool --chip esp32 write_flash 0x0 xrdap-esp32.bin
```

升级用同样的命令，Wi-Fi 设置会保留。esptool 报 `Wrong boot mode` 进不了下载模式时（有的板子自动下载电路只能复位、拉不低 GPIO0），按住 BOOT，按一下 RESET（EN），再松开 BOOT，然后在命令里加 `--before no_reset`。

### 装电脑端工具

从 [XRDAP-Host 的 Releases](https://github.com/Jiu-xiao/XRDAP-Host/releases) 下载对应平台的 `xrdap`。插上探针后运行：

```
xrdap list
```

能列出探针就说明固件和连接都正常。

## 2. 按场景使用

下面的 OpenOCD 例子以 STM32F401 为目标，换芯片时替换 `target/stm32f4x.cfg`。

### 2.1 S3 插 USB，用 Keil、pyOCD、probe-rs、OpenOCD

插 S3 的原生 USB 口（板上标 USB 的那个，不是串口桥）。它就是一个 CMSIS-DAP v2 探针，Windows 免驱。

```
openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg
pyocd list
probe-rs list
```

Keil：Debug 选 CMSIS-DAP Debugger，Settings 里端口选 SW，SW Device 栏出现 IDCODE 即连上，Max Clock 先设 4 MHz。OpenOCD 和 probe-rs 实测可用，pyOCD 没有实测。

### 2.2 C3 或传统 ESP32 插 USB，用 Keil、pyOCD、probe-rs、OpenOCD

C3 的 USB 口是固定功能的 USB Serial/JTAG，传统 ESP32 的 USB 口接的是 USB 转串口芯片，电脑上都是一个串口。用 `xrdap share usbip` 把它变成 USB 探针（`COM7` 换成实际的口，Linux 上是 `/dev/ttyACM0` 或 `/dev/ttyUSB0`）：

```
xrdap -p COM7 share usbip
```

再在另一个终端挂载：

```
usbip attach -r 127.0.0.1 -b 1-1
```

之后系统里出现 `XR-DAP-C3 CMSIS-DAP`（传统 ESP32 是 `XR-DAP-ESP32 CMSIS-DAP`），用法同 2.1。挂载要 USB/IP 客户端：Linux 内核自带（`sudo modprobe vhci-hcd`，命令前加 `sudo`），Windows 装 [usbip-win2](https://github.com/vadimgrn/usbip-win2)。不想开终端时，在 `xrdap` 图形界面里连上探针后勾选“共享为 USB DAP”，效果相同。

### 2.3 走 Wi-Fi，用 Keil、pyOCD、probe-rs、OpenOCD

先按 2.6 配好 Wi-Fi。探针在 3240 端口提供 USB/IP，直接挂载，不需要 `xrdap`：

```
usbip list -r xrdap-721384.local
usbip attach -r xrdap-721384.local -b 1-1
```

`xrdap-721384.local` 是探针的 mDNS 主机名（`xrdap-` 加 MAC 后 6 位），也可以写 IP；`xrdap list` 会列出来。挂载后用法同 2.1，用完 `usbip detach -p 0`。

### 2.4 走 Wi-Fi，OpenOCD 直连

OpenOCD 也可以不经 USB/IP，用它的 CMSIS-DAP TCP 后端直连探针的 3241 端口：

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-721384.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

TCP 后端还不在 OpenOCD 的正式版里，见第 3 节“OpenOCD 版本”。

### 2.5 看变量曲线、改参数

见 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host)：打开 `xrdap`，选 ELF，按变量名添加，开始采样。探针按固定周期采样，周期不受 USB 或 Wi-Fi 往返延迟限制。

### 2.6 配 Wi-Fi、换热点

探针连在电脑上（USB 或串口都行）时，在 `xrdap` 图形界面里连上探针，展开“Wi-Fi”填热点名和密码，点“保存并重启”；或者用命令行：

```
xrdap wifi set 热点名 密码
xrdap wifi status
```

保存后探针约 0.5 秒后重启并连接热点。换热点再 `set` 一次；`xrdap wifi clear` 清除后不再连 Wi-Fi。热点断了探针会一直重连。

不装 `xrdap` 时也可以用串口终端：S3 插 USB 后多出的虚拟串口，或 C3 的 USB 口（终端不要动 DTR/RTS），输入 `wifi set 热点名 密码`、`wifi status`、`wifi clear`、`help`。

```
python -m serial.tools.miniterm COM7 115200 --dtr 0 --rts 0
```

## 3. 参考

### 连接方式一览

| 连接 | S3 | C3 | 传统 ESP32 | 给谁用 |
|---|---|---|---|---|
| 原生 USB（CMSIS-DAP v2） | 有 | 无 | 无 | 所有工具 |
| 芯片 USB 口上的串口链路 | 无 | 有 | 无 | `xrdap`；`xrdap share` 转给其他工具 |
| 板上 USB 转串口（UART0） | 有，4 Mbaud | 无 | 有，921600 | 同上 |
| Wi-Fi，USB/IP，端口 3240 | 有 | 有 | 有 | 挂载后所有工具 |
| Wi-Fi，OpenOCD TCP 后端，端口 3241 | 有 | 有 | 有 | OpenOCD、`xrdap` |

`xrdap` 按串口芯片自动选波特率：CH343 等先试 4 Mbaud，CP2102、CH340 用 921600，不用手写 `--baud`。

几种连接共用一个 DAP，同一时间只服务一个客户端，3240 和 3241 也互斥（一个连着时另一个排队）。网络客户端连着时 S3 的原生 USB 暂时关闭，断开后恢复。客户端没有正常断开时约 11 秒后释放。

### 找到探针

- 序列号：`XRobot-XRDAP-` 加出厂 MAC 的 12 位大写十六进制，例如 `XRobot-XRDAP-E8069093EF1C`。USB 描述符、DAP_Info、mDNS 里都是这个值，多块探针时用它区分（OpenOCD 用 `adapter serial`，pyOCD 用 `-u`，probe-rs 用 `--probe`）。
- mDNS：主机名 `xrdap-` 加 MAC 后 3 字节小写（上例为 `xrdap-93ef1c.local`），服务 `_xrdap._tcp`，TXT 记录 `serial`、`chip`、`fw`。
- `xrdap list` 列出 USB、串口、局域网上的探针，同一块探针的几种连接放在一起。

### USB/IP 客户端

| 系统 | 安装 | 挂载 | 卸载 |
|---|---|---|---|
| Linux | 内核自带：`sudo modprobe vhci-hcd`；Ubuntu 的 `usbip` 命令在 `linux-tools-generic` 包里 | `sudo usbip attach -r <地址> -b 1-1` | `sudo usbip detach -p 0` |
| Windows | [usbip-win2](https://github.com/vadimgrn/usbip-win2) | `usbip attach -r <地址> -b 1-1` | `usbip detach -p 0` |

Linux 上普通用户要能打开设备，加 udev 规则：

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6199", MODE="0666"
```

虚拟设备的 VID:PID 是 1D50:6199，bcdDevice 0x0200（S3 原生 USB 是 0x0100，两者不同是因为 Windows 按这三项缓存描述符）。

### OpenOCD 版本

- 走 USB（2.1）和 USB/IP（2.2、2.3）用的是 OpenOCD 的 USB 后端，正式版 0.12.0 就行。
- 2.4 的 CMSIS-DAP TCP 后端不在任何正式版里（最新正式版 0.12.0 发布于 2023 年），要从 master 自己编译。Windows 上 master 在 2026-08-15 的提交 e89e73839d53 之前有套接字处理的 bug（把“暂时没有数据”当错误，启动等十几秒并刷屏 `error reading header`），要用包含这个提交的版本。

### Windows 上的驱动

Windows 偶尔会重装 S3 接口的驱动并丢掉设备接口 GUID，表现为所有工具都打不开探针。用管理员 PowerShell 删掉设备后重新插上：

```
pnputil /remove-device "USB\VID_1D50&PID_6199\<序列号>" /subtree
```

### 性能

S3，目标 STM32G431KB，杜邦线：

| 项目 | 结果 |
|---|---|
| USB 块读 / 块写（12 MHz） | 约 970 / 780 KiB/s |
| OpenOCD 经 USB 读 128 KiB（4 MHz） | 0.62 s |
| OpenOCD 经 USB 烧录 16 KiB 并校验（4 MHz） | 1.09 s |
| Wi-Fi 最小命令往返 | 约 1.3 ms |
| 探针端采样 8 个变量，2 ms 周期，连续 5 分钟 | 149999 帧，不丢帧、不跳拍，时间戳偏差 -27 到 +12 µs |

探针端采样（4 字节变量，2 秒内不跳拍不丢帧）的最短周期，SWD 约 23.7 MHz：

| 变量数 | USB | 串口 | Wi-Fi |
|---|---|---|---|
| 1 | 86 µs | 86 µs | 118 µs |
| 8 | 167 µs | 220 µs | 251 µs |
| 32 | 604 µs | 681 µs | 608 µs |

OpenOCD 读 STM32F401 的 128 KiB（Linux，4 MHz 请求）：

| 路径 | S3 | C3 |
|---|---|---|
| Wi-Fi，TCP 后端 | 1.6 s | 1.9 s |
| Wi-Fi，USB/IP | 2.1 s | 2.7 s |
| C3 插 USB，`xrdap share usbip` | - | 1.8 s |

C3 是单核，Wi-Fi 收发和采样在同一个核上：经 Wi-Fi 采样 4 个变量 1 ms 不跳拍，500 µs 约跳 0.2% 的拍。

传统 ESP32（CP2102，921600 baud，目标 STM32G431，杜邦线）：SWD 1 到 40 MHz 请求下读写全部正确，实际约 3 MHz（GPIO 寄存器在外设总线上，每次访问约 31 µs）；探针端采样 8 个变量 1 ms、1 个变量 200 µs 不跳拍不丢帧，再短时探针仍不跳拍，但 921600 的串口取不完数据，缓冲开始丢帧；经 `xrdap share usbip`，OpenOCD 读 128 KiB 3.4 s。经 Wi-Fi：4 个变量 500 µs、8 个变量 600 µs、1 个变量 150 µs 不丢帧（150 µs 时 5 秒内跳 1 拍）；Linux 上 USB/IP 挂载后 OpenOCD 读 128 KiB 3.2 s，TCP 后端 2.3 s。

### SWD 时钟和接线

SWCLK 高低电平等长，频率按 CPU 周期计算（S3 上 4 MHz 实测 4.03 MHz；请求 25 MHz 及以上时用最快的一档，约 23.7 MHz，未用示波器量过）。C3 的频率校正值还没有用逻辑分析仪校准。能跑多快取决于线长、目标板上拉和走线，先用 4 MHz；高时钟出错时先看 SWCLK、SWDIO 波形，缩短线或加串联电阻。引脚驱动强度设为 1（IDF 默认 2），减小过冲和振铃。

### 自己编译

需要 ESP-IDF v5.5。libxr 是 `third_party/libxr` 子模块。

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32s3      # C3 是 firmware/esp32c3，传统 ESP32 是 firmware/esp32
idf.py build
idf.py -p COM3 flash
```

`idf.py build merge-bin` 生成和 release 相同的合并镜像 `build/merged-binary.bin`。

| 目录 | 内容 |
|---|---|
| `firmware/esp32s3`、`firmware/esp32c3`、`firmware/esp32` | 各芯片的工程：`main.cpp`、`config.hpp`（引脚、核、优先级），S3 的 SWD 引擎 |
| `firmware/common/vendor` | XR 厂商扩展：协议、MEM-AP 访问、采样调度、帧缓冲、0x86 Wi-Fi 命令 |
| `firmware/common/net` | Wi-Fi、OpenOCD TCP 服务、USB/IP 虚拟设备、mDNS |
| `firmware/common/serial`、`console` | 串口 DAP 链路、C3 的串口分流、Wi-Fi 命令行 |
| `firmware/common/probe` | 三种芯片共用的参数、探针身份、启动步骤 |
| `firmware/common/swd` | C3 的专用 GPIO 引脚、传统 ESP32 的 GPIO 寄存器引脚、SWCLK 频率控制 |
| `docs/protocol.md` | 串口帧、TCP 帧、厂商命令、探针身份、mDNS、USB/IP 设备 |

每次推送都由 GitHub Actions 编译两个固件并检查格式（clang-format 21.1.8），打 `v*` 标签自动发布。

### 厂商扩展

除标准 CMSIS-DAP 命令外，探针实现了 0x80 到 0x86：一次读写多个地址、沿指针链读变量、在探针上按固定周期采样并由电脑批量取回、经 DAP 配 Wi-Fi。采样期间探针只接受 DAP_Info、DAP_HostStatus 和这组命令，所以采样和 OpenOCD 调试不能同时进行。格式见 [docs/protocol.md](docs/protocol.md)。

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
