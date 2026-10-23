# XRDAP-ESP

[English](README.en.md)

用 ESP32-S3、ESP32-S2、ESP32-C3 或传统 ESP32 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)。插 USB 线或走 Wi-Fi 都能用，Keil、pyOCD、probe-rs、OpenOCD 把它当普通的 CMSIS-DAP 探针；另外能在探针上按固定周期采样目标的变量，配合电脑端工具画曲线。

电脑端工具在 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host)：一个 `xrdap` 可执行文件，不带参数是图形界面，带命令是命令行。

## 选哪块板

| | ESP32-S3 | ESP32-C3 | 传统 ESP32 |
|---|---|---|---|
| 插 USB 线给 Keil、pyOCD、probe-rs、OpenOCD 用 | 直接用（原生 USB 探针） | 电脑上运行 `xrdap share usbip` 后用 | 同 C3（经板上 USB 转串口芯片） |
| 走 Wi-Fi 给 Keil、pyOCD、probe-rs、OpenOCD 用 | 用 USB/IP 挂载 | 用 USB/IP 挂载 | 用 USB/IP 挂载 |
| 走 Wi-Fi 给 OpenOCD 用 | OpenOCD 的 TCP 后端直连 | 同左 | 同左 |
| SWD 时钟 | 最高约 23.7 MHz | 最高约 20 MHz | 最高约 5 MHz（GPIO 寄存器位带，受外设总线限制） |
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

“高速无线 DAP 调试器 Lite”板（ESP32-S3 模组，2×5 排针）用单独的固件 `xrdap-esp32-s3-lite.bin`：SWCLK 是 GPIO37；SWDIO 经板上的 SGM3157 模拟开关接到排针，固件把开关的选择脚 GPIO36 固定为低，用 GPIO47 收发 SWDIO。板上没有 USB 转串口芯片，只用原生 USB 和 Wi-Fi。模组要用不带 Octal PSRAM 的（WROOM-1-N4、N8，MINI-1-N4、N8），带 Octal PSRAM 的型号占用 GPIO35 到 37。

ESP32-S2 板（ESP32-S2FH4，芯片内 4 MB Flash，没有 PSRAM，2×5 排针）用固件 `xrdap-esp32-s2.bin`：SWCLK 是 GPIO37，SWDIO 是 GPIO35（经 0 Ω 电阻，板上的 SGM3157 不焊），目标 nRESET 是 GPIO40（开漏，上电时不复位目标）。排针上的 TDI、TDO 留给以后的 JTAG，目标串口 TXD、RXD 暂不使用。板上没有 USB 转串口芯片，只用原生 USB 和 Wi-Fi，用法同 S3（2.1、2.3、2.4）。S2 是单核，内部 RAM 只有 320 KB，Wi-Fi 缓冲和任务栈都比 S3 小，采样路径不放进 IRAM。这个固件还没有在实物上测过。

S3、S2 的状态灯（S3 开发板：GPIO48 的 RGB 灯；Lite 板：板上的 WS2812；S2 板：GPIO17 的 WS2812）。Lite 板的 SW2 键、S2 板的 KEY 键按住 5 秒清除热点参数并重启。开发板的 BOOT 键只用于进下载模式：它接在 USB 转串口芯片的自动下载电路上，串口线的 DTR/RTS 能把它拉低，所以固件运行时不读它；开发板清除 Wi-Fi 用 `xrdap wifi clear` 或命令行 `wifi clear`（2.6）。

| 灯 | 含义 |
|---|---|
| 彩虹扫过一圈 | 上电 |
| 红 | 有线：USB 已连上电脑，或 5 秒内有经 USB、串口来的命令 |
| 蓝 | 无线：有网络调试客户端（OpenOCD TCP 或 USB/IP），或只连着 Wi-Fi |
| 紫 | USB 和 Wi-Fi 都没连上；正在连 Wi-Fi 时呼吸 |
| 主色与紫色来回，2 秒一次 | 上位机经 DAP_HostStatus 报告已连接 |
| 红蓝来回，1 秒一次 | 上位机报告目标运行中 |
| 闪亮 | 每条 DAP 命令（USB、串口、Wi-Fi 都算）把灯提亮再暗下去，命令不断时约 15 Hz 闪烁 |
| 紫色快闪 | Lite 板 SW2 或 S2 板 KEY 按住中；按满 5 秒变紫色常亮，清除热点参数并重启 |
| 紫色常亮 | 刚改了热点参数（按键、命令行或 0x86 命令），马上重启 |

USB 和 Wi-Fi 都连着时以有线为准，显示红色。上位机异常退出、没有发 DAP_Disconnect 时，5 秒内没有命令也回到主色。板上的 WS2812B 用 5V 供电，数据高电平门限是 0.65 × 5V = 3.25V，3.3V 驱动刚好够，个别灯珠可能不亮或颜色不对，不影响调试功能。

### 烧固件

用 Chrome 或 Edge 桌面版打开[网页烧录](https://jiu-xiao.github.io/XRDAP-ESP/)，选板子后点“烧录固件”。也可以用命令行：在 [Releases](../../releases) 下载 `xrdap-esp32-s3.bin`、`xrdap-esp32-s3-lite.bin`（Lite 板）、`xrdap-esp32-s2.bin`（S2 板）、`xrdap-esp32-c3.bin` 或 `xrdap-esp32.bin`（校验和在同一页的 `SHA256SUMS`），从地址 0 烧录：

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
python -m esptool --chip esp32s2 write_flash 0x0 xrdap-esp32-s2.bin
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
python -m esptool --chip esp32 write_flash 0x0 xrdap-esp32.bin
```

升级用同样的命令，Wi-Fi 设置会保留。开发板的 BOOT 键只在烧录时用到。v0.5.1 及更早的 S3 镜像头写的是 8MB，烧到 4MB Flash 的板子（N4、部分 Lite 板）上会反复重启；用这些旧镜像时在 `write_flash` 后加 `--flash_size 4MB`。esptool 报 `Wrong boot mode` 进不了下载模式时（有的板子自动下载电路只能复位、拉不低 GPIO0），按住 BOOT，按一下 RESET（EN），再松开 BOOT，然后在命令里加 `--before no_reset`。

Lite 板没有复位键和 USB 转串口，烧录前要手动进下载模式：拔下 USB，按住 SW1（接 GPIO0 的按键）再插上，然后松开。芯片进入下载模式后，电脑上出现原生 USB 的串口（`303A:1001`），再运行上面的命令；烧完后 esptool 复位芯片，新固件随即启动。

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

Keil：Debug 选 CMSIS-DAP Debugger，Settings 里端口选 SW，SW Device 栏出现 IDCODE 即连上，Max Clock 先设 4 MHz。

### 2.2 C3 或传统 ESP32 插 USB，用 Keil、pyOCD、probe-rs、OpenOCD

C3 的 USB 口是固定功能的 USB Serial/JTAG，传统 ESP32 的 USB 口接的是 USB 转串口芯片，电脑上都是一个串口。用 `xrdap share usbip` 把它变成 USB 探针（`COMx` 换成实际的口，Linux 上是 `/dev/ttyACM0` 或 `/dev/ttyUSB0`）：

```
xrdap -p COMx share usbip
```

再在另一个终端挂载：

```
usbip attach -r 127.0.0.1 -b 1-1
```

之后系统里出现 `XR-DAP-C3 CMSIS-DAP`（传统 ESP32 是 `XR-DAP-ESP32 CMSIS-DAP`），用法同 2.1。挂载要 USB/IP 客户端：Linux 内核自带（`sudo modprobe vhci-hcd`，命令前加 `sudo`），Windows 装 [usbip-win2](https://github.com/vadimgrn/usbip-win2)。不想开终端时，在 `xrdap` 图形界面里连上探针后勾选“共享为 USB DAP”，效果相同。

### 2.3 走 Wi-Fi，用 Keil、pyOCD、probe-rs、OpenOCD

先按 2.6 配好 Wi-Fi。探针在 3240 端口提供 USB/IP，直接挂载，不需要 `xrdap`：

```
usbip list -r xrdap-xxxxxx.local
usbip attach -r xrdap-xxxxxx.local -b 1-1
```

`xrdap-xxxxxx.local` 是探针的 mDNS 主机名（`xxxxxx` 是 MAC 后 6 位十六进制），也可以写 IP；`xrdap list` 会列出来。挂载后用法同 2.1，用完 `usbip detach -p 0`。

### 2.4 走 Wi-Fi，OpenOCD 直连

OpenOCD 也可以不经 USB/IP，用它的 CMSIS-DAP TCP 后端直连探针的 3241 端口：

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-xxxxxx.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

TCP 后端还不在 OpenOCD 的正式版里，见第 3 节“OpenOCD 版本”。

### 2.5 看变量曲线、改参数

见 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host)：打开 `xrdap`，选 ELF，按变量名添加，开始采样。探针按固定周期采样，周期不受 USB 或 Wi-Fi 往返延迟限制。

### 2.6 配 Wi-Fi、换热点

探针连在电脑上（USB 或串口都行）时，在 `xrdap` 图形界面里连上探针，展开“Wi-Fi”填热点名和密码，点“保存并重启”；或者用命令行：

```
xrdap wifi set lab 密码
xrdap wifi status
```

`lab` 换成热点名。保存后探针约 0.5 秒后重启并连接热点。换热点再 `set` 一次；`xrdap wifi clear` 清除后不再连 Wi-Fi。热点断了探针会一直重连。

热点参数存在 NVS 里，删除它的有 `wifi clear`、0x86 清除命令、Lite 板的 SW2 长按，以及 NVS 分区损坏（写满或格式版本不符）时探针在启动时做的擦除重建。每次删除都记下来源。命令行 `wifi status` 和 0x86 的状态回复给出上次清除的来源和上次复位的原因（上电、软件重启、看门狗、异常、欠压），热点参数不见了时据此判断原因。

不装 `xrdap` 时也可以用串口终端：S3 插 USB 后多出的虚拟串口，或 C3 的 USB 口（终端不要动 DTR/RTS），输入 `wifi set lab 密码`、`wifi status`、`wifi clear`、`help`。

```
python -m serial.tools.miniterm COMx 115200 --dtr 0 --rts 0
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

几种连接共用一个 DAP，同一时间只有一条连接拥有探针：第一条发出 DAP_Info 以外命令的连接成为主人，别的连接的命令回“忙”（DAP_Info 照常回答），直到主人发出 DAP_Disconnect、TCP 连接断开、USB 总线复位（拔线或主机复位），或串口 5 秒没有请求。Wi-Fi 的 3240 和 3241 同一时间只接受一个客户端（一个连着时另一个排队）；客户端没有正常断开时约 11 秒后释放。

### 找到探针

- 序列号：`XRobot-XRDAP-` 加出厂 MAC 的 12 位大写十六进制，例如 `XRobot-XRDAP-0123456789AB`。USB 描述符、DAP_Info、mDNS 里都是这个值，多块探针时用它区分（OpenOCD 用 `adapter serial`，pyOCD 用 `-u`，probe-rs 用 `--probe`）。
- mDNS：主机名 `xrdap-` 加 MAC 后 3 字节小写（上例为 `xrdap-6789ab.local`），服务 `_xrdap._tcp`，TXT 记录 `serial`、`chip`、`fw`。
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

虚拟设备的 VID:PID 是 1D50:6199，bcdDevice 0x0200（S3 原生 USB 是 0x0100，S2 是 0x0500，几者不同是因为 Windows 按这三项缓存描述符）。

### OpenOCD 版本

- 走 USB（2.1）和 USB/IP（2.2、2.3）用的是 OpenOCD 的 USB 后端，正式版 0.12.0 就行。
- 2.4 的 CMSIS-DAP TCP 后端不在正式版 0.12.0 里，要从 master 自己编译。Windows 上 master 在提交 e89e73839d53 之前有套接字处理的 bug（把“暂时没有数据”当错误，启动等十几秒并刷屏 `error reading header`），要用包含这个提交的版本。

### Windows 上的驱动

Windows 偶尔会重装 S3 接口的驱动并丢掉设备接口 GUID，表现为所有工具都打不开探针。用管理员 PowerShell 删掉设备后重新插上：

```
pnputil /remove-device "USB\VID_1D50&PID_6199\<序列号>" /subtree
```

### 性能

下面是典型值，实际结果随目标芯片和接线变化。S3：

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

OpenOCD 读 128 KiB（4 MHz 请求）：

| 路径 | S3 | C3 |
|---|---|---|
| Wi-Fi，TCP 后端 | 1.6 s | 1.9 s |
| Wi-Fi，USB/IP | 2.1 s | 2.7 s |
| C3 插 USB，`xrdap share usbip` | - | 1.8 s |

C3 是单核，Wi-Fi 收发和采样在同一个核上：经 Wi-Fi 采样 4 个变量 1 ms 不跳拍，500 µs 约跳 0.2% 的拍。

传统 ESP32（串口 921600 baud）：SWD 1 到 40 MHz 请求下读写全部正确；GPIO 寄存器在外设总线上（写一次约 12 个 CPU 周期，读一次约 17 个），请求 5 MHz 以上都按全速跑，`xrdap info` 报告的单次访问约 30 µs（约 150 个 SWCLK 周期，折合不到 5 MHz）；探针端采样 8 个变量 1 ms、1 个变量 200 µs 不跳拍不丢帧，再短时探针仍不跳拍，但 921600 的串口取不完数据，缓冲开始丢帧；经 `xrdap share usbip`，OpenOCD 读 128 KiB 3.4 s。经 Wi-Fi：4 个变量 500 µs、8 个变量 600 µs、1 个变量 150 µs 不丢帧（150 µs 时偶尔跳 1 拍）；USB/IP 挂载后 OpenOCD 读 128 KiB 3.2 s，TCP 后端 2.3 s。

### SWD 时钟和接线

S3 上 SWCLK 高低电平等长，频率按 CPU 周期计算；请求 25 MHz 及以上时用最快的一档，约 23.7 MHz。C3 和传统 ESP32 用 libxr 的通用 GPIO 位带，频率由 libxr 按 `config.hpp` 的 `kSwdTiming`（每个 SWCLK 周期的 CPU 周期数）换算成延时循环次数，实际时钟与请求值相差不超过半档；C3 最快一档约 5.2 MHz，更高的请求都按这一档跑。能跑多快取决于线长、目标板上拉和走线，先用 4 MHz；高时钟出错时先看 SWCLK、SWDIO 波形，缩短线或加串联电阻。引脚驱动强度设为 1（IDF 默认 2），减小过冲和振铃。

### 自己编译

需要 ESP-IDF v5.5。libxr 是 `third_party/libxr` 子模块；厂商扩展、状态灯规则和探针身份来自 [XRDAP-Core](https://github.com/Jiu-xiao/XRDAP-Core)，是 `third_party/xrdap-core` 子模块。

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32sx      # S2、S3 共用；C3 是 firmware/esp32c3，传统 ESP32 是 firmware/esp32
idf.py build
idf.py -p COMx flash
```

`idf.py build merge-bin` 生成和 release 相同的合并镜像 `build/merged-binary.bin`。`firmware/esp32sx` 默认编 S3 开发板。Lite 板多叠加一份配置，S2 板换目标（`sdkconfig.defaults.esp32s2` 随目标自动叠加）：

```
idf.py -B build-lite -D SDKCONFIG=build-lite/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.lite" build merge-bin
idf.py -D IDF_TARGET=esp32s2 -B build-s2 -D SDKCONFIG=build-s2/sdkconfig build merge-bin
```

也可以在 `idf.py menuconfig` 的 XR-DAP → Board 里选板型。

命令行、0x86、TCP 帧、USB/IP 和状态灯输入这些纯逻辑不依赖 ESP-IDF，`firmware/tests` 是它们在电脑上运行的单元测试（CMake 和 C++17 编译器，GCC 或 MSVC）：

```
cmake -S firmware/tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests
```

| 目录 | 内容 |
|---|---|
| `firmware/esp32sx`、`firmware/esp32c3`、`firmware/esp32` | 各芯片的工程：`main.cpp`、`config.hpp`（引脚、核、优先级；`esp32sx` 是 S2、S3 共用的工程，按目标和板型选），S2、S3 的 SWD 引擎和状态灯 |
| `firmware/common/net` | Wi-Fi 和热点参数存储、0x86 Wi-Fi 命令、OpenOCD TCP 服务、USB/IP 虚拟设备、mDNS |
| `firmware/common/serial`、`console` | 串口 DAP 链路、C3 的串口分流、Wi-Fi 命令行 |
| `firmware/common/probe` | 三种芯片共用的参数、探针身份、启动步骤、重启、状态灯输入、采样任务和它的 gptimer |
| `firmware/tests` | 电脑上运行的单元测试 |
| `firmware/common/swd` | S3、C3 的专用 GPIO 引脚，传统 ESP32 的 GPIO 寄存器引脚 |
| `third_party/xrdap-core` | XR 厂商扩展、状态灯规则、探针身份和协议文档（[XRDAP-Core](https://github.com/Jiu-xiao/XRDAP-Core)） |

每次推送都由 GitHub Actions 编译五个固件（S3、S3 Lite、S2、C3、传统 ESP32）、运行单元测试并检查格式（clang-format 21.1.8），打 `v*` 标签自动发布 release。网页烧录站点由 `pages` 工作流发布：取最新 release 的固件和 main 上 `web/` 的页面，打标签的编译完成后、或 `web/` 有改动时重新发布（`web/make_site.py` 按 `web/boards.json` 为每块板生成 ESP Web Tools 的 manifest）。

### 厂商扩展

除标准 CMSIS-DAP 命令外，探针实现了 0x80 到 0x86：一次读写多个地址、沿指针链读变量、在探针上按固定周期采样并由电脑批量取回、经 DAP 配 Wi-Fi。采样期间探针只接受 DAP_Info、DAP_HostStatus 和这组命令，所以采样和 OpenOCD 调试不能同时进行。格式见 XRDAP-Core 的 [docs/protocol.md](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.md)。

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
