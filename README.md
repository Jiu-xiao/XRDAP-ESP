# XRDAP-ESP

[English](README.en.md)

ESP32-S3 和 ESP32-C3 做的 CMSIS-DAP 调试探针，基于 [libxr](https://github.com/xrobot-org/libxr)。用来烧录和调试 SWD 目标芯片（已测 STM32G431KB、STM32F401RC）。探针可以经 USB、串口或 Wi-Fi 连接，三种连接共用同一个 DAP 实例；除标准 CMSIS-DAP 命令外，还提供 XR 厂商扩展（多地址读写、指针链、探针端周期采样）。

| | ESP32-S3 | ESP32-C3 |
|---|---|---|
| 产品名（DAP_Info） | `XR-DAP-S3` | `XR-DAP-C3` |
| USB CMSIS-DAP v2（Keil、pyOCD、probe-rs、OpenOCD 直接用） | 有 | 没有（芯片的 USB 是固定功能的 USB Serial/JTAG） |
| 串口 DAP 链路（经 `xrdap-forward` 给 OpenOCD） | UART0，板上 USB 转串口，4 Mbaud | 芯片的 USB 口（USB Serial/JTAG） |
| Wi-Fi，TCP 3241 上的 CMSIS-DAP（OpenOCD 的 TCP 后端） | 有 | 有 |
| Wi-Fi，3240 端口的 USB/IP 虚拟 USB 探针（挂载后 Keil、pyOCD、probe-rs、OpenOCD 直接用） | 有 | 有 |
| Wi-Fi 命令行（配网） | USB 虚拟串口 | 芯片的 USB 口（与 DAP 链路共用，自动区分）、UART0 |
| 经 DAP 配网（`xrdap wifi set`，厂商命令 0x86） | USB、串口、TCP | 串口、TCP |
| mDNS 广播（`xrdap-xxxxxx.local`） | 有 | 有 |
| SWD 最高时钟 | 约 23.7 MHz（汇编高速路径） | 约 20 MHz 请求下读写正常 |
| XR 厂商扩展（多地址读写、指针链、探针端采样） | 有 | 有 |

两个固件共用 `firmware/common` 下的厂商扩展、TCP 服务、串口适配、命令行和 Wi-Fi 代码，行为一致。

电脑端工具在 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) 仓库：命令行 `xrdap`、图形界面 `xrdap-gui`、串口转发 `xrdap-forward`，以及 Rust 库 `xrdap`。

## 接线

两种芯片都用 GPIO5 和 GPIO4：

| 探针 | 目标 |
|---|---|
| GPIO5 | SWCLK |
| GPIO4 | SWDIO |
| GND | GND |

引脚驱动强度设为 1（ESP-IDF 默认是 2），减小边沿过冲和振铃。S3 上实测 0 档（最弱）在 10 MHz 以上出错更多。

一个目标只接一个探针：没在用的探针也可能在驱动 SWCLK、SWDIO。

ESP32-C3 的 DAP 串口链路走芯片的 USB D-/D+（GPIO18 / GPIO19）。USB 口直接连到这两个脚的开发板插上就能用；ESP32-C3-DevKitM-1 这类板上 USB 口接 USB 转串口芯片（UART0）的，要另接一根 USB 线到排针：D- 接 GPIO18，D+ 接 GPIO19，再接 5V 和 GND。

## 下载和烧录

在 [Releases](../../releases) 下载 `xrdap-esp32-s3.bin` 或 `xrdap-esp32-c3.bin`，同一页的 `SHA256SUMS` 给出两个文件的校验和。这是合并好的完整镜像（引导程序、分区表、应用），从地址 0 烧录。

ESP32-S3：

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
```

ESP32-C3：

```
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
```

esptool 通过芯片的 ROM 下载模式烧录：S3 使用 USB 口或串口桥的口，C3 使用芯片的 USB 口或 UART0。需要手动进入下载模式时，按住 BOOT 再按一下 RESET。

### 升级和 Wi-Fi 设置

热点参数保存在 NVS 分区。从 0.2.0 起 NVS 在应用分区后面（S3 在 `0x210000`，C3 在 `0x190000`），release 的合并镜像只覆盖从地址 0 到应用末尾的范围，所以用同样的命令升级后，Wi-Fi 设置保留。

从旧版固件（0.1.x，NVS 在 `0x9000`）升级到 0.2.0 时，旧位置的热点参数不会带过来，需要重新配网一次，之后的升级不再丢失。`esptool erase_flash` 会清掉整片 Flash，包括热点参数。

## 自己编译

需要 ESP-IDF v5.5。libxr 以 git 子模块形式放在 `third_party/libxr`，跟踪 libxr 的 `feat/xrdap-serial-transport` 分支。克隆时带上 `--recurse-submodules`（已经克隆的执行 `git submodule update --init`）。

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32s3      # C3 版是 firmware/esp32c3
idf.py build
idf.py -p COM3 flash
```

`idf.py build merge-bin` 额外生成 `build/merged-binary.bin`，内容与 release 中的镜像相同，可用上一节的 `esptool write_flash 0x0` 烧录。

| 目录 | 内容 |
|---|---|
| `firmware/esp32s3` | S3 工程：`main.cpp`、`config.hpp`（引脚、核分配、优先级等固定参数）、SWD 引擎和汇编高速路径 |
| `firmware/esp32c3` | C3 工程：`main.cpp`、`config.hpp`、SWD 时钟适配 |
| `firmware/common` | 两者共用：厂商扩展、TCP 服务、串口适配、串口分流、Wi-Fi 命令行和连接、mDNS、探针身份、专用 GPIO |
| `firmware/components/libxr` | 把 libxr 子模块接成 ESP-IDF 组件 |
| `docs/protocol.md` | 串口帧格式、厂商命令、探针身份、mDNS |

每次推送和 PR 都会由 GitHub Actions 编译两个固件，打 `v*` 标签会自动发布 release（两个镜像和 `SHA256SUMS`）。

## 连接方式

探针的几种连接共用同一个 DAP 实例，同一时间只用其中一种。下面的 OpenOCD 配置以 STM32G431KB 为目标，换芯片时替换 `source [find target/...]` 一行。OpenOCD 使用 Espressif 的 v0.12.0-esp32-20260831 或更新的版本。

### USB（ESP32-S3）

插上 ESP32-S3 的 USB 口（板上标 USB 的那个，不是串口桥那个）。USB 产品名 `XR-DAP-S3 CMSIS-DAP`，VID:PID 为 `1D50:6199`（XRUSB 登记的标识），序列号是 `XRobot-XRDAP-` 加出厂 MAC（12 位大写十六进制）。CMSIS-DAP v2（Bulk），Windows 自带 WinUSB，免驱。同时会多出一个 CDC 虚拟串口，用来配 Wi-Fi。有 TCP 客户端连接时 USB 会自动关闭，客户端断开后恢复。

OpenOCD，保存为 `s3-usb.cfg`，运行 `openocd -f s3-usb.cfg`：

```tcl
adapter driver cmsis-dap
adapter usb vid_pid 0x1d50 0x6199
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

这一版 OpenOCD 不接受 `cmsis-dap vid_pid`，要用 `adapter usb vid_pid`。同一台电脑上有多个使用这组 VID:PID 的设备时，加一行 `adapter serial XRobot-XRDAP-E8069093EF1C`（换成探针的序列号）指定。

Keil：调试器选 CMSIS-DAP Debugger，端口选 SW，设备选目标芯片。在 Debug 选项卡的 Settings 里，SW Device 栏出现目标芯片的 IDCODE 即表示连接成功；Max Clock 先设 4 MHz。

pyOCD：

```
pyocd list
pyocd pack install stm32g431kb
pyocd flash -t stm32g431kbtx -f 4m app.hex
pyocd gdbserver -t stm32g431kbtx -f 4m
```

`pyocd list` 应列出 `XR-DAP-S3 CMSIS-DAP`（pyOCD 这几行没有实测）。有多块 CMSIS-DAP 探针时加 `-u <序列号>`。目标名以 `pyocd list --targets` 的输出为准。

probe-rs：

```
probe-rs list
probe-rs run --chip STM32G431KBTx --probe 1d50:6199 --speed 4000 app.elf
```

`probe-rs list` 列出 `XR-DAP-S3 CMSIS-DAP -- 1d50:6199-0:XRobot-XRDAP-<MAC>`。有多块探针时把列表里 `--` 后面那一串写在 `--probe` 后面。

Windows 偶尔会重新安装这个接口的驱动并丢掉设备接口 GUID，表现为 OpenOCD、xrdap 都打不开探针。用管理员 PowerShell 删掉设备再重新插上即可恢复：`pnputil /remove-device "USB\VID_1D50&PID_6199\<序列号>" /subtree`。

### 串口（ESP32-S3）

S3 在 UART0 上提供 CMSIS-DAP。UART0 接板上的 USB 转串口芯片（GPIO43 TX，GPIO44 RX），也就是串口桥那个 USB 口，波特率 4 Mbaud（板上的 CH343 在 5 Mbaud 下不能正常工作）。UART0 用于 DAP 之后，系统日志控制台已关闭。USB 和串口之间没有自动切换，不要同时使用。

OpenOCD 不能直接使用串口，电脑端用 `xrdap-forward` 把串口转成 OpenOCD 的 CMSIS-DAP TCP 后端。先在一个终端运行（`COM35` 换成串口桥的端口，Linux 上是 `/dev/ttyUSB0` 这类设备名）：

```
xrdap-forward --serial COM35
```

S3 的 USB 转串口芯片打开串口时会用 DTR/RTS 让 ESP32 复位一次，`xrdap-forward` 反复查询探针，等它启动完成（最长 10 秒）后开始监听 127.0.0.1:3241。再在另一个终端用下面的配置运行 OpenOCD，保存为 `s3-serial.cfg`：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

```
openocd -f s3-serial.cfg
```

Windows 上要在配置里多加两行，见下面的 Wi-Fi（TCP）一节。

### 串口（ESP32-C3）

C3 在芯片的 USB 口（USB Serial/JTAG，电脑上是一个 CDC 串口）上提供 CMSIS-DAP，波特率不起作用。USB Serial/JTAG 自己根据 DTR、RTS 复位芯片，所以 `xrdap-forward` 加 `--keep-lines`，不改这两根线：

```
xrdap-forward --serial COM7 --keep-lines
```

OpenOCD 配置与 S3 的串口方式相同，保存为 `c3-serial.cfg`：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 127.0.0.1
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32f4x.cfg]
adapter speed 4000
```

```
openocd -f c3-serial.cfg
```

这个口同时是 Wi-Fi 命令行所在的口，见下面的 Wi-Fi 配网。串口帧格式见 [docs/protocol.md](docs/protocol.md)。

### Wi-Fi（TCP）

探针连上 Wi-Fi 后，在 TCP 3241 端口提供 CMSIS-DAP，S3 和 C3 相同。IP 可以用 `xrdap wifi status` 或串口命令行的 `wifi status` 查看，也可以在路由器里查；探针同时广播 mDNS 主机名 `xrdap-xxxxxx.local`（xxxxxx 是出厂 MAC 的后 3 字节，小写），见下面的“找到探针”。

OpenOCD 要用带 TCP 后端的版本，按 IP 连接，保存为 `wifi.cfg`：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 192.168.1.50   ;# 换成探针的 IP
cmsis-dap tcp port 3241
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

按主机名连接时只替换 host 一行：

```tcl
cmsis-dap tcp host xrdap-93ef1c.local
```

Windows 上在 `cmsis-dap tcp port` 之后加两行：

```tcl
cmsis-dap quirk enable
cmsis-dap tcp min_timeout 10
```

完整的 Windows 版配置：

```tcl
adapter driver cmsis-dap
cmsis-dap backend tcp
cmsis-dap tcp host 192.168.1.50
cmsis-dap tcp port 3241
cmsis-dap quirk enable
cmsis-dap tcp min_timeout 10
transport select swd
source [find target/stm32g4x.cfg]
adapter speed 4000
```

说明：

- TCP 只有 OpenOCD 支持。Keil、pyOCD、probe-rs 不支持 CMSIS-DAP over TCP，改用下面的 USB/IP。
- 同一时间只服务一个 TCP 客户端。客户端异常消失（没有正常断开）时，约 11 秒后连接会被释放。
- 这一版 OpenOCD 的 TCP 后端在 Windows 上把“暂时没有数据”当成错误（它看 `errno`，Winsock 不写 `errno`）：启动要等约 10 秒、刷屏 `error reading header`，多个请求同时在途时响应会错位，报 `command mismatch`。`quirk enable` 让它一次只发一个请求，`min_timeout 10` 把启动等待缩到约 1 秒，启动时的报错仍会打印，可以忽略。OpenOCD 上游已修复（提交 e89e73839d53），Espressif 还没有带这个修复的发布版。Linux 上不需要这两行。
- 串口方式（`xrdap-forward` 加 OpenOCD）在 Windows 上同样使用这两行。

### Wi-Fi（USB/IP 虚拟 USB 探针）

固件 0.3.0 起，探针还在 3240 端口提供 USB/IP 设备。电脑用 USB/IP 客户端挂载后，系统里出现一个 USB CMSIS-DAP v2 探针（VID:PID 1D50:6199，bcdDevice 0x0200，产品名 `XR-DAP-S3 CMSIS-DAP` 或 `XR-DAP-C3 CMSIS-DAP`，序列号同 DAP_Info），Keil、pyOCD、probe-rs、OpenOCD 按 USB 探针使用，Windows 自动用 WinUSB 驱动。

Linux（内核自带 USB/IP）：

```
sudo modprobe vhci-hcd
usbip list -r xrdap-721384.local
sudo usbip attach -r xrdap-721384.local -b 1-1
openocd -f interface/cmsis-dap.cfg -c "adapter serial XRobot-XRDAP-9C139E721384" -f target/stm32f4x.cfg
sudo usbip port
sudo usbip detach -p 0
```

普通用户要能打开设备，需要 udev 规则 `SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6199", MODE="0666"`，否则调试工具用 sudo 运行。Windows 先安装 [usbip-win2](https://github.com/vadimgrn/usbip-win2)，命令同样是 `usbip attach -r <探针地址> -b 1-1`。

说明：

- 3240 和 3241 由同一个任务服务，同一时间只有一个客户端；一个连着时另一个排队等。`usbip list` 只查询设备，不占用探针。
- 挂载期间 S3 的 USB 设备关闭（与 TCP 3241 相同），断开后恢复。
- 外部工具没有正常断开时，约 11 秒后连接被释放。
- 实测（ubuntu24，OpenOCD 读 F401 的 128 KiB）：S3 2.1 s，C3 2.7 s；同样条件下 TCP 3241 为 1.6 s、1.9 s。
- C3 插在电脑上时，USB 线比 Wi-Fi 延迟小：用 XRDAP-Host 的 `xrdap-forward -p COM7 --usbip` 或 `xrdap-gui` 的“共享为 USB DAP”在本机提供同样的设备，见 [XRDAP-Host 的说明](https://github.com/Jiu-xiao/XRDAP-Host#共享为-usb-dapusbip)。

### Wi-Fi 配网

探针要先知道热点的名称和密码，有三种方式。热点名或密码里有空格时用双引号括起来；开放网络不填密码；密码长度为 8 到 63 个字符。保存后设备会重启并连接热点；没有热点参数时 Wi-Fi 不启动。热点参数存在设备的 NVS 里（命名空间 `rmlink`，键 `wifi`），格式和 RMLink 固件一致，用 RMLink 配过网的板子不用重配。

#### S3：USB 虚拟串口

S3 用 USB 连接后多出一个虚拟串口（设备管理器里的 `USB 串行设备`），命令行独占这个口。用串口终端打开，波特率随意：

```
python -m serial.tools.miniterm COM36 115200
```

输入：

```
wifi set 热点名 密码
```

#### C3：与 DAP 链路共用的 USB 口

C3 的命令行在芯片的 USB 口上，也就是 DAP 串口链路那个口，不需要切换模式：探针自动区分 DAP 帧和人打的命令（`firmware/common/serial/serial_mux.hpp`，机制见 [docs/protocol.md](docs/protocol.md) 第 5 节）。命令行只在收到一行命令并回车后才回答，一行里有控制字符时整行丢弃；最近 2 秒有 DAP 帧时不理会文字输入，所以调试工具不会收到命令行的输出。终端不要改 DTR/RTS，否则可能复位芯片：

```
python -m serial.tools.miniterm COM7 115200 --dtr 0 --rts 0
```

输入同样的 `wifi set 热点名 密码`。引出了 UART0（GPIO21 TX、GPIO20 RX）的板子也可以在 UART0 上用，115200 波特率。

终端里的命令：

| 命令 | 作用 |
|---|---|
| `wifi set <热点名> [<密码>]` | 保存热点参数并重启 |
| `wifi status` | 查看热点、是否连接、IP、信号强度 |
| `wifi clear` | 清除已保存的热点参数并重启 |
| `help` | 列出命令 |

#### 经 DAP：`xrdap wifi`

厂商命令 0x86 经当前的 DAP 连接（USB、串口或 TCP）读写热点参数，不需要串口终端，S3 和 C3 都适用。`-p` 选择连接方式，见下面的“找到探针”：

```
xrdap wifi status
xrdap -p COM7 wifi set 热点名 密码
xrdap wifi clear
```

`wifi set` 和 `wifi clear` 成功后探针约 0.5 秒重启，当前连接随之中断。`wifi status` 显示热点名、是否已连接、IP 和信号强度，不显示密码。命令格式见 [docs/protocol.md](docs/protocol.md) 3.7 节。

### 找到探针

#### xrdap list

```
xrdap list
```

列出 USB 上的探针、串口和局域网（mDNS）里的探针，按序列号归并：同一块探针在 USB、串口、Wi-Fi 上的条目放在一起，每条给出对应的 `--probe` 值。

#### 序列号

序列号形如 `XRobot-XRDAP-E8069093EF1C`，是 `XRobot-XRDAP-` 加出厂 MAC 的 12 位大写十六进制，S3 的 USB 描述符、`DAP_Info` 和 mDNS 记录里都是这个值。

#### mDNS

探针拿到 IP 后广播服务 `_xrdap._tcp`（端口 3241），主机名 `xrdap-` 加 MAC 后 3 字节的小写十六进制，例如序列号 `XRobot-XRDAP-E8069093EF1C` 对应 `xrdap-93ef1c.local`。TXT 记录有 `serial`、`chip`（`esp32s3` 或 `esp32c3`）、`fw`（固件版本）。OpenOCD 的 `cmsis-dap tcp host` 可以直接写这个主机名。

#### xrdap 的 --probe

没有 `-p` 时，只有一个 USB 探针则直接使用它，否则要用 `-p` 指定：

```
xrdap info                          # 只有一个 USB 探针时自动选择
xrdap -p COM35 info                 # 串口；Espressif 的 USB Serial/JTAG 口自动保持 DTR/RTS
xrdap -p 192.168.0.108 info         # TCP，按 IP
xrdap -p xrdap-93ef1c.local info    # TCP，按 mDNS 主机名
xrdap -p 93EF1C info                # 按序列号后缀
```

## XR 厂商扩展

除标准 CMSIS-DAP 命令外，探针还实现了一组厂商命令：0x80 到 0x85 一次读写多个地址，沿指针链读取变量，以及在探针上按固定周期采样变量、由上位机批量取回；0x86 经 DAP 读写 Wi-Fi 热点参数。采样由探针定时，频率不受链路往返延迟限制。USB、串口、TCP 下行为相同。INFO 命令（0x80）返回功能位，bit4 为 1 表示支持 0x86。

采样期间探针只接受 `DAP_Info`、`DAP_HostStatus` 和这组命令，其他命令回 `0xFF`，所以采样不能和 OpenOCD 调试同时进行。命令格式和采样规则见 [docs/protocol.md](docs/protocol.md)，0x86 在 3.7 节。

采样任务由定时器中断按节拍唤醒，节拍之间阻塞，排在 DAP 命令、TCP、串口任务之上，所以上位机取数据不会挤掉节拍。每个周期至少给别的任务留 1/8 周期（最少 15 µs）：一拍做完离下一拍太近，或者已经来不及时，跳过下一拍并计入跳拍数，探针始终能响应。

## xrdap 使用示例

`xrdap`（命令行）和 `xrdap-gui`（图形界面）用这组命令按变量名读写目标内存、在探针上采样并画曲线。变量的地址和类型取自目标程序 ELF 里的 DWARF 调试信息，所以需要 `--elf`：

```
xrdap --elf app.elf read motor.speed
xrdap --elf app.elf write motor.kp=1.5
xrdap --elf app.elf watch motor.speed --period 1ms --csv log.csv
xrdap halt
xrdap run
xrdap reset --halt
```

`read` 读一次变量，`write` 写入 `变量=值`，`watch` 在探针上按给定周期采样并写入 CSV。`halt`、`run`、`reset` 控制目标内核，`reset --halt` 复位后停在复位向量。每条命令都可以加 `-p` 选择探针，例如 `xrdap -p 192.168.0.108 --elf app.elf watch motor.speed --period 1ms`。完整参数见 `xrdap --help` 和 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host)。

## 实测：ESP32-S3

目标 STM32G431KB，杜邦线连接，OpenOCD / 自写客户端测得。4 MHz 的 OpenOCD 各项和采样一项为 2026-10-18 测得。

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
| 探针端采样 8 个条目（含分频），周期 2 ms，连续 5 分钟（USB） | 149999 帧，没有丢帧、跳拍，时间戳与 k × 2 ms 的偏差在 -27 到 +12 µs |
| Windows 上 OpenOCD 经串口转发读 128 KiB / 烧录 16 KiB 并校验（4 MHz，加 `quirk` 和 `min_timeout` 两行） | 2.1 s / 2.3 s（含约 1 s 连接等待） |
| Windows 上 OpenOCD 经 Wi-Fi 读 128 KiB / 烧录 16 KiB 并校验（4 MHz，加 `quirk` 和 `min_timeout` 两行） | 2.5 s / 3.0 s（含约 1 s 连接等待） |

### 各连接方式的极限（2026-10-19，SWD 请求 25 MHz，实际约 23.7 MHz）

主机是 Windows 上的 Python 脚本，每项取 1.5 秒内的平均，一次一个请求（同步）。Wi-Fi 的往返时间平均约 2 ms、最大 6 ms，Wi-Fi 的数会随环境波动。

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

探针端采样（每个变量每拍都读，4 字节直接读）连续 2 秒没有跳拍、没有丢帧的最短周期：

| 变量数 | USB | 串口 | Wi-Fi |
|---|---|---|---|
| 1 | 86 µs（11.6 kHz） | 86 µs（11.6 kHz） | 118 µs（8.5 kHz） |
| 2 | 86 µs（11.6 kHz） | 101 µs（9.9 kHz） | 137 µs（7.3 kHz） |
| 4 | 118 µs（8.5 kHz） | 137 µs（7.3 kHz） | 137 µs（7.3 kHz） |
| 8 | 167 µs（6.0 kHz） | 220 µs（4.5 kHz） | 251 µs（4.0 kHz） |
| 16 | 325 µs（3.1 kHz） | 372 µs（2.7 kHz） | 372 µs（2.7 kHz） |
| 32 | 604 µs（1.66 kHz） | 681 µs（1.47 kHz） | 608 µs（1.64 kHz） |

32 个变量时总吞吐 USB 约 5.3 万次/秒，串口约 4.7 万，Wi-Fi 约 5.3 万。SWD 为 4 MHz 时 32 个变量最短周期要 2 ms 以上。每多一级指针多一次读。读一个变量是 3 次 SWD 传输（写地址、读数据、再读一次 RDBUFF），25 MHz 下每个变量约 11.4 µs，其中线上时间约 6 µs。

Flash（OpenOCD，页 2 KiB，STM32G431KB）：擦除约 16 ms 一页，不随连接方式变化；编程约 75 到 84 KiB/s，由芯片 Flash 决定。

| 项目（128 KiB） | USB | 串口转发（Windows） | Wi-Fi（Windows） |
|---|---|---|---|
| 擦除 | 2.0 s | 2.0 s | 2.0 s |
| 编程 | 1.5 s | 1.6 s | 1.7 s |
| 校验 | 0.94 s | 0.95 s | 0.98 s |
| 读出 | 0.21 s | 0.69 s | 0.68 s |

## 实测：ESP32-C3

2026-10-19，原生 USB 的 C3 板，目标 STM32F401RC，杜邦线。

- 串口链路（USB Serial/JTAG）：读写 RAM 的 1/2/4/8 字节，写后读回一致；SWD 时钟 1、4、10、20 MHz 都没有错误。
- 探针端采样（SWD 20 MHz）：经 USB 口，4 个变量最短 300 µs（约 3.3 kHz）没有跳拍，32 个变量 2 ms 没有跳拍。经 Wi-Fi，4 个变量 1 ms 周期 5 秒没有跳拍，500 µs 跳 19 拍（约 0.2%）。C3 是单核，Wi-Fi 收发和采样在同一个核上，周期更短时跳拍增多；连着 Wi-Fi 时经 USB 口采样也会偶尔跳拍。
- OpenOCD：经 `xrdap-forward` 导出 64 KiB SRAM 0.56 s（2 MHz），经 Wi-Fi 直连 0.87 s；烧录 420 字节的测试程序并校验，经 USB 口 1.9 s，经 Wi-Fi 2.9 s。
- 同一个 USB 口上终端输入、DAP 读写、采样交替进行都正常，二进制数据不会让命令行输出。

## 已知问题

- SWD 时钟：SWCLK 的高低电平等长，实际频率和请求值基本一致（S3 上 4 MHz 时实测 4.03 MHz；请求 25 MHz 及以上时用最快的一档，约 23.7 MHz，这个数是按 CPU 周期数算出来的，没有用示波器量过）。C3 的 SWCLK 频率校正值沿用 S3 的实测值，没有量过。能跑多快取决于外部条件：线的长度和阻抗、目标板的上拉和走线、杜邦线质量等，先用 4 MHz。S3 测试板用杜邦线连接，4 到 20 MHz 下各做 3000 轮（每轮写 1016 字节再读回比较，共约 2400 万字）没有出现应答错误和数据不一致。换了线、换了目标板都要重新测；高时钟下出问题时，先用示波器看 SWCLK 和 SWDIO 的波形，调整外围电路（缩短线、串联电阻、上拉等）。
- 旧版（libxr 自带的 SWD 实现）在 10 MHz 以上写应答常出 FAULT，并有偶发的数据错误：切换到输出的 GPIO_ENABLE 寄存器写在外设总线上，生效比预期晚，前两位数据被上拉成 1。现在切换后读回该寄存器，等写完成再发数据。
- Windows 上 OpenOCD 的 TCP 后端问题见“连接方式”中的 Wi-Fi（TCP）一节。
- 从 0.1.x 升级到 0.2.0 时，热点参数需要重新配置一次，见“升级和 Wi-Fi 设置”。

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
