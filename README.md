# XRDAP-ESP

[English](README.en.md)

XRDAP-ESP 是运行在乐鑫 ESP32-S3、ESP32-S2、ESP32-C3 和传统 ESP32 上的 CMSIS-DAP v2 调试探针固件，基于 [libxr](https://github.com/xrobot-org/libxr)，属于 XR-DAP 系列。Keil、pyOCD、probe-rs、OpenOCD 把它当作普通的 CMSIS-DAP 探针使用，探针经 USB 线或 Wi-Fi 连接电脑。固件还实现了 XR 厂商扩展，配合电脑端工具 [XRDAP-Host](https://github.com/Jiu-xiao/XRDAP-Host) 读写变量、在探针上按固定周期采样变量。XR-DAP 各探针共用的代码在 [XRDAP-Core](https://github.com/Jiu-xiao/XRDAP-Core)。

## 板子

| 板子 | 固件文件 | 与电脑的连接 |
|---|---|---|
| ESP32-S3 开发板 | `xrdap-esp32-s3.bin` | 原生 USB、板上 USB 转串口、Wi-Fi |
| “高速无线 DAP 调试器 Lite”板（ESP32-S3 模组） | `xrdap-esp32-s3-lite.bin` | 原生 USB、Wi-Fi |
| ESP32-S2 板（ESP32-S2FH4） | `xrdap-esp32-s2.bin` | 原生 USB、Wi-Fi |
| ESP32-C3 开发板 | `xrdap-esp32-c3.bin` | 芯片自带 USB 口上的串口、Wi-Fi |
| 传统 ESP32 开发板 | `xrdap-esp32.bin` | 板上 USB 转串口、Wi-Fi |

ESP32-S3 和 ESP32-S2 的原生 USB 直接作为 CMSIS-DAP 探针使用。S3 的 SWD 时钟和采样速度在这几种芯片中最高；S2 为单核，内部 RAM 较小。ESP32-C3 和传统 ESP32 经串口连接电脑，其他调试工具经 `xrdap share` 使用它们；传统 ESP32 的 SWD 由外设总线上的 GPIO 寄存器产生，速度最低。各板的引脚、下载模式、状态灯和按键见 [docs/boards.md](docs/boards.md)。

## 烧录和升级

在 Chrome 或 Edge 桌面版中打开[网页烧录](https://jiu-xiao.github.io/XRDAP-ESP/)页面，选择板子后点击“烧录固件”，页面经 Web Serial 写入最新发布版本的固件。

每个[发布版本](https://github.com/Jiu-xiao/XRDAP-ESP/releases)提供以下文件：

| 文件 | 内容 |
|---|---|
| 上表中的五个 `.bin` 文件 | 各板的合并镜像，包含 bootloader、分区表和应用，从地址 0 写入 |
| `SHA256SUMS` | 以上文件的 SHA-256 校验和 |

把下载的文件和 `SHA256SUMS` 放在同一目录后运行 `sha256sum -c --ignore-missing SHA256SUMS`，与发布一致的文件显示 `OK`。命令行用 esptool 写入：

```
python -m esptool --chip esp32s3 write_flash 0x0 xrdap-esp32-s3.bin
python -m esptool --chip esp32s2 write_flash 0x0 xrdap-esp32-s2.bin
python -m esptool --chip esp32c3 write_flash 0x0 xrdap-esp32-c3.bin
python -m esptool --chip esp32 write_flash 0x0 xrdap-esp32.bin
```

Lite 板烧录前手动进入下载模式：拔下 USB，按住 SW1 再插上，然后松开，电脑上出现原生 USB 的串口 `303A:1001`。经这个串口烧录时加 `--after watchdog_reset`，写入后芯片由看门狗复位并启动新固件：

```
python -m esptool --chip esp32s3 --after watchdog_reset write_flash 0x0 xrdap-esp32-s3-lite.bin
```

其他板子进入下载模式的方法见 [docs/boards.md](docs/boards.md#下载模式)。升级使用同样的网页或命令，Wi-Fi 设置保留。

## 连接方式

ESP32-S3 和 ESP32-S2 的原生 USB 在电脑上是 CMSIS-DAP v2 探针，调试工具直接使用。ESP32-C3 和传统 ESP32 经串口连接电脑，`xrdap share usbip` 把这样的探针共享为 USB/IP 设备，在另一个终端挂载后同样作为 USB 探针使用。`-p` 写串口名（Linux 上为 `/dev/ttyACM0` 或 `/dev/ttyUSB0`）；Linux 上先加载 `vhci-hcd` 模块，并以 root 运行 `usbip`：

```
xrdap share usbip -p COM3
usbip attach -r 127.0.0.1 -b 1-1
```

连上 Wi-Fi 后，探针在 TCP 端口 3241 上提供 CMSIS-DAP。局域网中的电脑用同样的命令共享它，`-p` 写探针的 mDNS 主机名或 IP，例如 `-p xrdap-6789ab.local`。OpenOCD 也可以用 CMSIS-DAP TCP 后端直接连接探针：

```
openocd -c "adapter driver cmsis-dap; cmsis-dap backend tcp; cmsis-dap tcp host xrdap-6789ab.local; cmsis-dap tcp port 3241; transport select swd" -f target/stm32f4x.cfg
```

`xrdap list` 列出各探针的连接和 `-p` 的写法。各芯片的连接、USB/IP 客户端、OpenOCD 版本和同时使用几条连接时的规则见 [docs/connections.md](docs/connections.md)。

## Wi-Fi

探针作为 Wi-Fi 客户端连接 2.4 GHz 的热点，认证方式为 WPA2-Personal 或开放网络。热点参数用 `xrdap wifi set lab password` 写入（`lab` 为热点名，`password` 为密码），探针保存后重启并连接热点；`xrdap wifi status` 查看连接状态，`xrdap wifi clear` 清除热点参数。`xrdap` 图形界面的“Wi-Fi”一栏提供同样的设置，Lite 板和 S2 板也可以长按板上的按键清除热点参数。热点参数的存储、清除记录和串口终端中的命令见 [docs/connections.md](docs/connections.md#wi-fi-设置)。

## 变量读写与采样

XR 厂商扩展让一条命令读写多个地址、沿指针链解引用，并由探针按固定周期采样变量、电脑批量取回结果，采样周期不受 USB 或 Wi-Fi 往返延迟的影响。`xrdap` 的 `read`、`write` 和 `watch` 使用这些命令；扩展为协议第 2 版，需要 0.6.0 或更新版本的 `xrdap`：

```
xrdap read --elf app.elf motor.speed motor.state
xrdap watch --elf app.elf motor.speed motor.current --period 1ms --csv log.csv
```

采样期间探针只执行 DAP_Info、DAP_HostStatus 和厂商扩展命令，因此采样和 OpenOCD 调试分开进行。扩展命令为 0x80 到 0x86，其中 0x86 用于配置 Wi-Fi，格式见 XRDAP-Core 的 [`docs/protocol.md`](https://github.com/Jiu-xiao/XRDAP-Core/blob/main/docs/protocol.md) 第 3 节。

## 性能

下表为经 USB 线连接时的典型值，实际结果随目标芯片和接线变化。全部测量结果和条件见 [docs/performance.md](docs/performance.md)。

| 项目 | ESP32-S3（原生 USB） | ESP32-C3（芯片 USB 口） | 传统 ESP32（串口 921600 baud） |
|---|---|---|---|
| SWD 最高时钟 | 约 23.7 MHz | 约 5.2 MHz | 约 5 MHz |
| 探针端采样的最短周期 | 4 个变量 125 µs | 4 个变量 225 µs | 8 个变量 900 µs，1 个变量 350 µs |
| OpenOCD 读 128 KiB（请求 10 MHz；C3、传统 ESP32 经 `xrdap share openocd`） | 约 0.3 s | 0.64 s | 2.0 s |

## 编译

编译需要 ESP-IDF v5.5（CI 使用 v5.5.2）。libxr 和 XRDAP-Core 是 `third_party/` 下的子模块。`firmware/esp32sx` 是 S3 和 S2 共用的工程，`firmware/esp32c3` 和 `firmware/esp32` 分别对应 C3 和传统 ESP32：

```
git clone --recurse-submodules https://github.com/Jiu-xiao/XRDAP-ESP.git
cd XRDAP-ESP/firmware/esp32sx
idf.py build merge-bin
```

`build/merged-binary.bin` 是与发布文件相同的合并镜像，`idf.py -p COM3 flash` 把编译结果直接写入板子。`firmware/esp32sx` 默认编译 S3 开发板，S3 的板子也可以在 `idf.py menuconfig` 的 XR-DAP → Board 中选择。以下命令分别编译 Lite 板（多叠加一份配置）和 S2 板（更换目标，`sdkconfig.defaults.esp32s2` 随目标自动叠加）：

```
idf.py -B build-lite -D SDKCONFIG=build-lite/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.lite" build merge-bin
idf.py -D IDF_TARGET=esp32s2 -B build-s2 -D SDKCONFIG=build-s2/sdkconfig build merge-bin
```

命令行、0x86、TCP 帧、状态灯输入和串口复用的逻辑有编译机上的单元测试，需要 CMake、C++20 编译器和 libudev（Ubuntu 上为 `libudev-dev`）。在仓库根目录运行：

```
cmake -S firmware/tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests
```

GitHub Actions 编译五个固件、运行单元测试并检查格式（clang-format 21.1.8）；推送 `v*` 标签时发布 release，网页烧录页面随之更新。版本号是 `firmware/common/probe/probe_info.hpp` 中的 `kFirmwareVersion`。

## 目录

| 目录 | 内容 |
|---|---|
| `firmware/esp32sx/` | ESP32-S3 和 ESP32-S2 的工程：`main.cpp`、`config.hpp`（按目标和板子选择引脚）、SWD 引擎、状态灯、清除按键 |
| `firmware/esp32c3/`、`firmware/esp32/` | ESP32-C3 和传统 ESP32 的工程：`main.cpp`、`config.hpp`（引脚、SWD 时序） |
| `firmware/common/probe/` | 各芯片共用的参数、探针身份、启动步骤、重启、状态灯输入、执行任务和采样节拍的定时器 |
| `firmware/common/net/` | Wi-Fi 和热点参数存储、0x86 Wi-Fi 命令、CMSIS-DAP TCP 服务、mDNS |
| `firmware/common/serial/`、`firmware/common/console/` | 串口上 DAP 帧与命令行的分流、Wi-Fi 命令行 |
| `firmware/common/swd/` | SWD 引脚：S2、S3、C3 使用专用 GPIO，传统 ESP32 使用 GPIO 寄存器 |
| `firmware/tests/` | 编译机上的单元测试 |
| `third_party/`、`firmware/components/libxr/` | 子模块 libxr 和 XRDAP-Core；把 libxr 接入 ESP-IDF 的组件 |
| `web/` | 网页烧录页面（ESP Web Tools）和各板的固件列表 `boards.json`，由 `pages` 工作流发布到 GitHub Pages |

## 许可证

Apache-2.0，见 [LICENSE](LICENSE)。
