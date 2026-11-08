# 连接方式

[English](connections.en.md)

本文说明 XRDAP-ESP 探针与电脑之间的各种连接、找到探针的方法、同时使用几条连接时的规则，以及 USB/IP、OpenOCD、Windows 驱动和 Wi-Fi 的设置。

## 各芯片的连接

| 连接 | S3 | S2 | C3 | 传统 ESP32 | 使用者 |
|---|---|---|---|---|---|
| 原生 USB（CMSIS-DAP v2） | 有 | 有 | 无 | 无 | 所有调试工具 |
| 芯片 USB 口上的串口链路 | 无 | 无 | 有 | 无 | `xrdap`；经 `xrdap share` 给其他调试工具 |
| 板上 USB 转串口（UART0） | 有，4 Mbaud（Lite 板无） | 无 | 无 | 有，921600 baud | 同上 |
| Wi-Fi，经电脑上的 `xrdap share usbip` | 有 | 有 | 有 | 有 | 挂载后所有调试工具 |
| Wi-Fi，CMSIS-DAP TCP 后端，端口 3241 | 有 | 有 | 有 | 有 | OpenOCD、`xrdap` |

S3 和 S2 的原生 USB 是 USB 设备 `1D50:6199`，产品名为 `XR-DAP-S3 CMSIS-DAP` 或 `XR-DAP-S2 CMSIS-DAP`，Windows 按描述符自动使用 WinUSB 驱动。同一个 USB 设备还带一个虚拟串口，即 Wi-Fi 命令行。

串口链路使用 XRDAP-Core 的 [`docs/protocol.md`](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.md) 第 1 节的帧格式，由 `xrdap` 使用。`xrdap` 按串口芯片选择波特率：CH343 等先试 4 Mbaud，CP2102、CH340 用 921600。C3 的 USB 口同时承载 DAP 帧和 Wi-Fi 命令行，固件自动区分两者（同一文档第 5 节）。

## 找到探针

序列号为 `XRobot-XRDAP-` 加出厂 MAC 的 12 位大写十六进制，例如 `XRobot-XRDAP-0123456789AB`。USB 描述符、DAP_Info 和 mDNS 中都是这个值，接了多块探针时用它区分：OpenOCD 用 `adapter serial`，pyOCD 用 `-u`，probe-rs 用 `--probe`。

连上 Wi-Fi 后，探针用 mDNS 广播主机名 `xrdap-` 加 MAC 后 3 字节的小写十六进制（上例为 `xrdap-6789ab.local`），服务为 `_xrdap._tcp`，TXT 记录为 `serial`、`chip` 和 `fw`。

`xrdap list` 列出 USB 和局域网上的探针、经芯片自带 USB 口（C3）连接的探针，以及可能接着探针的其他 USB 串口；同一块探针的几条连接列在一起，并给出选择每条连接时 `-p` 的写法。`xrdap list --ask-serial` 逐个打开这些串口询问，经 USB 转串口芯片连接的探针由此列出；打开串口可能使接在上面的开发板复位。

## 一次只使用一条连接

各种连接共用同一个 DAP。同一时间只经一条连接使用探针，由使用者保证：探针执行每条连接发来的命令，两条连接同时发命令时一次执行一条，USB 的命令优先，SWD 的状态可能因此错乱。

`DAP_Connect` 记下它来自哪条连接。以下情况下这个连接作废，正在进行的采样停止，厂商扩展命令需要重新 `DAP_Connect`：

| 情况 | 作废的连接 |
|---|---|
| 任何一条连接发出 `DAP_Disconnect` | 任何连接 |
| TCP 客户端断开 | 在 TCP 上的连接 |
| USB 总线复位（拔线或主机复位） | 在 USB 上的连接 |
| 串口 5 s 没有请求 | 在串口上的连接 |

Wi-Fi 的 3241 端口同一时间服务一个客户端，其他客户端排队等待；客户端没有正常断开时，探针约 11 s 后释放它。完整规则见 XRDAP-Core 的 [`docs/protocol.md`](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.md) 第 3.1 节。

## USB/IP

经串口或 Wi-Fi 连接的探针由电脑上的 `xrdap share usbip` 共享为 USB/IP 设备，挂载后在系统中是 USB CMSIS-DAP v2 探针，VID:PID 为 `1D50:6199`，产品名为 `XR-DAP-C3 CMSIS-DAP`（传统 ESP32 为 `XR-DAP-ESP32 CMSIS-DAP`，S3、S2 为 `XR-DAP-S3 CMSIS-DAP`、`XR-DAP-S2 CMSIS-DAP`）。探针本身在 Wi-Fi 上只提供 TCP 端口 3241 的 CMSIS-DAP，USB/IP 由 `xrdap` 提供。

`-p` 写串口名（Linux 上为 `/dev/ttyACM0` 或 `/dev/ttyUSB0`），Wi-Fi 上的探针写 mDNS 主机名或 IP，例如 `-p xrdap-6789ab.local`：

```
xrdap share usbip -p COM3
```

`xrdap share` 一直运行到按 Ctrl-C，挂载在另一个终端中进行：

```
usbip attach -r 127.0.0.1 -b 1-1
```

挂载后调试工具像使用 USB 探针一样使用它；用完后执行 `usbip detach -p 0`，再在 `xrdap` 的终端按 Ctrl-C 停止共享。`xrdap` 图形界面中连上探针后勾选“共享为 USB DAP”，效果相同。给局域网中另一台电脑使用时见 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) 的说明（`--listen`）。

| 系统 | 安装 | 挂载 | 卸载 |
|---|---|---|---|
| Linux | 内核自带：`sudo modprobe vhci-hcd`；Ubuntu 的 `usbip` 命令在 `linux-tools-generic` 包中 | `sudo usbip attach -r <地址> -b 1-1` | `sudo usbip detach -p 0` |
| Windows | [usbip-win2](https://github.com/vadimgrn/usbip-win2) | `usbip attach -r <地址> -b 1-1` | `usbip detach -p 0` |

Linux 上加入以下 udev 规则后，普通用户可以打开设备；S3、S2 的原生 USB 使用同一个 VID:PID，规则同样适用：

```
SUBSYSTEM=="usb", ATTR{idVendor}=="1d50", ATTR{idProduct}=="6199", MODE="0666"
```

## OpenOCD

经原生 USB 和 USB/IP 时，OpenOCD 使用它的 USB 后端（`-f interface/cmsis-dap.cfg`），正式版 0.12.0 即可。Wi-Fi 上的探针也可以经 OpenOCD 的 CMSIS-DAP TCP 后端直接连接 3241 端口，不经 USB/IP。以下示例以 STM32F4 为目标，其他芯片换掉 `target/stm32f4x.cfg`：

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-6789ab.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

经串口连接的探针由 `xrdap share openocd` 在本机提供同样的 TCP 后端，用法见 XRDAP-Host。

TCP 后端不在正式版 0.12.0 中，需要从 master 编译。Windows 上要使用包含提交 e89e73839d53 的版本：更早的 master 在 Windows 上把“暂时没有数据”当作错误，启动时等待十几秒并反复输出 `error reading header`。

## Windows 上的驱动

Windows 偶尔会重装 S3 接口的驱动并丢失设备接口 GUID，此时所有工具都无法打开探针。在管理员 PowerShell 中删除设备后重新插上，`<序列号>` 为探针的序列号：

```
pnputil /remove-device "USB\VID_1D50&PID_6199\<序列号>" /subtree
```

## Wi-Fi 设置

探针作为 Wi-Fi 客户端连接已有的热点。ESP32 系列的 Wi-Fi 工作在 2.4 GHz 频段；路由器常把两个频段合用一个名字，探针连不上时先确认 2.4 GHz 频段已打开。填写密码时认证方式为 WPA2-Personal，不填密码为开放网络。热点名为 1 到 32 字节，密码为 8 到 63 字节。

热点参数用 `xrdap wifi set`、`xrdap wifi status`、`xrdap wifi clear` 设置、查询和清除，`-p` 选择探针；第一次设置时探针经 USB 或串口连在电脑上。`xrdap` 图形界面中连上探针后展开“Wi-Fi”，填写热点名和密码，点击“保存并重启”，效果相同。保存后探针约 0.5 s 后重启并连接热点；更换热点时再保存一次，清除后探针不再连接 Wi-Fi。热点断开后探针持续重连。

热点参数保存在 NVS 中，升级固件时保留。删除它们的途径有：命令行的 `wifi clear`，0x86 命令或控制口的清除请求（`xrdap wifi clear` 发送的就是这种请求），Lite 板 SW2 或 S2 板 KEY 的长按，以及 NVS 分区损坏（写满或格式版本不符）时探针在启动时做的擦除重建。每次删除都记下来源。`xrdap wifi status`、命令行的 `wifi status` 和 0x86 的状态回复给出上次清除的来源和上次复位的原因（上电、软件重启、看门狗、异常、欠压），热点参数丢失时据此判断原因；任务看门狗超时后，命令行的 `wifi status` 还给出当时在运行的任务。

### 串口终端中的命令行

不使用 `xrdap` 时，Wi-Fi 也可以在串口终端中设置。命令行所在的口：

| 板子 | 口 |
|---|---|
| S3 开发板、Lite 板、S2 板 | 原生 USB 连接电脑后出现的虚拟串口 |
| C3 开发板 | 芯片自带的 USB 口；引出 UART0 的板子上还有 UART0，115200 baud |
| 传统 ESP32 开发板 | 板上 USB 转串口芯片的口，921600 baud |

终端打开串口时 DTR、RTS 保持为 0，以免复位芯片。以 pySerial 的 miniterm 为例（传统 ESP32 把 115200 换成 921600）：

```
python -m serial.tools.miniterm COM3 115200 --dtr 0 --rts 0
```

命令有 `wifi set <热点> [<密码>]`（含空格的热点名或密码用双引号括起，不填密码为开放网络）、`wifi status`、`wifi clear`、`heap`（空闲堆和各任务栈的剩余量）和 `help`。
