# XRDAP-ESP32-S3

基于 [libxr](https://github.com/xrobot-org/libxr) 的 ESP32-S3 CMSIS-DAP 调试探针固件:USB Bulk v2 和 CMSIS-DAP over TCP(Wi-Fi),SWD 用专用 GPIO 位带实现。

- 目标:STM32G431KB(SWD:SWCLK = GPIO5,SWDIO = GPIO4)
- 传输:USB 高速不可用(ESP32-S3 只有全速);USB 在没有 TCP 客户端连接时有效,TCP 客户端连上后 USB 关闭,客户端断开后 USB 自动恢复
- 工具:OpenOCD、probe-rs、pyOCD、Keil 经 USB 验证;OpenOCD(Espressif v0.12.0-esp32-20260831 起带 `cmsis-dap backend tcp`)经 TCP 验证。pyOCD、probe-rs、Keil 不支持 TCP

## 目录

| 目录 | 内容 |
|---|---|
| `firmware/` | ESP-IDF 工程(应用层:SWD 引擎、DAP 实例装配、TCP 服务、Wi-Fi) |
| `scripts/build.ps1` | 构建脚本 |
| `tools/` | 测试脚本(USB/TCP 吞吐、浸泡、热插拔、切换、Keil 工程生成等) |
| `docs/results.md` | 全部测量记录(按时间追加) |
| `docs/libxr-issues.md`、`docs/requests-for-libxr-*.md` | 给 libxr 的问题和需求(已验证的因果) |
| `docs/libxr-patches/` | 实验用补丁(0006、0007 已合入 libxr 上游) |

## 构建

需要 ESP-IDF v5.5.x 和 libxr-c(测试用 `feat/usb-slot-model-dap` 分支 commit `22ba8642` 或更新):

```powershell
scripts\build.ps1 -LibxrRoot D:\src\libxr-c build
scripts\build.ps1 -LibxrRoot D:\src\libxr-c flash -p COM3
```

默认配置:Wi-Fi + USB,SWD 和 Step 线程在核 1(USB 中断、Wi-Fi、lwIP 在核 0),`sdkconfig.defaults` 里已含性能调优(QIO、32 KB 指令缓存、lwIP 放 IRAM、Wi-Fi 缓冲区加大)。常用开关在 `firmware/main/xrdap_config.hpp`(`XRDAP_WIFI_ENABLE`、`XRDAP_SWD_CORE`、各类测试/调试宏),用 `-DXRDAP_xxx=...` 加到 `CMAKE_CXX_FLAGS`。

Wi-Fi 凭据不在源码里:固件读取 NVS 命名空间 `rmlink`、键 `wifi` 里已存的热点参数(由 RMLink 固件配网写入),不打印、不落盘。没有凭据时只有 USB 可用。

## 实测(同一块板子、杜邦线、驱动强度降一档)

| 项目 | 结果 |
|---|---|
| USB 块读 12 MHz,4 个在飞 | 约 960 到 990 KiB/s(全速 USB 上限) |
| USB 纯 OUT / TAR+块写 | 约 770 / 780 KiB/s |
| OpenOCD(USB)读 128 KiB,12 MHz | 约 690 KiB/s;烧录校验 4、12 MHz 全通过 |
| TCP 块读 / 块写 12 MHz | 约 380 / 450 KiB/s(Wi-Fi 往返延迟限制) |
| USB ↔ TCP 切换、拔插 USB、Wi-Fi 掉线、死客户端 | 通过(见 `docs/results.md`) |

**已知限制**:SWD 时钟 10 MHz 以上读写偶发 FAULT(约 1e-4 / 块),8 MHz 浸泡也出现过零星写 FAULT;OpenOCD、probe-rs 会清除并重试所以不易察觉。4 MHz 的 5 分钟浸泡(6.4 万块)0 错误,见 `docs/results.md`;目前能确认干净的时钟是 4 MHz。手写 SWD 引擎(`fast_swd.hpp`)只是实验,没有接入默认路径。

## 测试脚本

脚本里有作者实验室的路径(`D:/Codex/...`)和地址(Pi `10.43.0.1` 上的 Wi-Fi 网络),使用前按自己的环境改。TCP 测试脚本在与探针同一网络的主机上运行。`tools/winusb_dap.py` 和 `rmlink_protocol.py` 改自 RMLink 的 Apache-2.0 代码,保留了原文件头。

## 许可

尚未确定。
