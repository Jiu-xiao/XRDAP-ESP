# 给 libxr 会话:两个已定位并上板验证的 bug(基于 libxr-c b493fed8,ESP32-S3 + STM32G431)

补丁(都只有一行,diff 在 `D:\Codex\tmp\xrdap-s3\libxr-patches\`):
- `0006-swd-idle-clocks-low.patch`
- `0007-esp-usb-reset-reenable-global-irq.patch`

数据:`D:\Codex\tmp\rml\results-20261016.md`(末尾几节)。我没有改 libxr-c 本体,验证用的是 libxr-c 的拷贝(`libxr-idle`、`libxr-usbdbg`)。

## 1. ESP32 USB 拔插后不再枚举(必修)

**现象**:拔掉 USB 线再插回,Windows 报"未知 USB 设备(设备描述符请求失败)"。空闲设备(没有主机会话)第一次拔插就复现,52fa2e92 和 b493fed8 一样。同一块板子、线、口,TinyUSB 的 RMLink 固件拔插正常,所以不是硬件问题。串口复位(esptool)才能恢复。

**根因**(串口寄存器和 ISR 计数日志证实):
- 拔线后核进入挂起,插回后主机发总线复位,`HandleBusReset` 里的有界停止检查没有稳定下来(拔线后有 IN 端点一直是使能状态),进入 `OnStopTimeout` 升级路径,再走复位链的 `ResetDone`。
- `ResetDone` 做了 `ResetCoreRegisters` + `RebuildCoreRegisters` + `sftdiscon = 0`,但没有把 `GAHBCFG.glbllntrmsk` 重新置 1。`Start()` 第 173 行置了,`ResetDone` 没有。
- 结果:全局中断被关(`GAHBCFG` 从 0x1a9 变成 0x1a8,`DAINTMSK` 为 0),控制器收不到任何中断,不应答 EP0 的 SETUP。

**修补**(`ResetDone` 里 `started = true` 之前加一行):
```cpp
dev->dctl_reg.sftdiscon = 0;
dev->gahbcfg_reg.glbllntrmsk = 1;
self->runtime_.started = true;
```

**验证**(b493fed8 加这一行):
- 空闲拔插 3 次,每次都重新枚举(总线复位计数 1→2→3,`GAHBCFG` 保持 0x1a9,配置阶段后 `DAINTMSK` 回到 0x0007000f)。
- 数据流拔插(4 个请求在飞的块读,约 15000 轮):拔插 3 次(快速、3 秒、10 秒),3 次自动重连,数据校验 0 错,没有复位板子,事后 OpenOCD 烧录和校验通过。

**未解决**:为什么拔线后停止检查不稳定、要走升级路径(IN 端点拔线后一直使能)。现在升级路径靠这一行能恢复,但每次拔线都会走一次重路径。主机侧只做总线复位、拔插发生在命令执行中的其他时序没测。

## 2. SwdGeneralGPIO 的空闲周期期间 SWDIO 驱动高电平(必修)

**现象**:pyOCD 连不上目标,报 "No ACK"。pyOCD 发 `DAP_TransferConfigure`,空闲周期设 2,读 DPIDR 成功,紧接着第一次写(ABORT)收到 ack=7。OpenOCD、probe-rs、Keil 默认发空闲周期 0,走不到这条路径,所以一直没暴露。

**定位**:自己的客户端对比空闲周期 0/1/2/4:0 正常,1 以上都失败。`SwdGeneralGPIO::IdleClocks` 在空闲时钟期间把 SWDIO 驱动成高电平,目标把它当成起始位,下一个真正的请求就对不齐了。CMSIS-DAP 参考实现在空闲周期里驱动 SWDIO 为低(我是凭记忆,没有重新核对源码)。

**修补**(`swd_general_gpio.hpp` 的 `IdleClocks`):
```cpp
(void)SetSwdioDriveMode();
swdio_.Write(false);      // 原来是 Write(true)
```

**验证**:
- 空闲周期 0/1/2/4 都正常,pyOCD 0.45.1 能连上、读写内存(12 MHz 通过 Python 接口读 128 KiB 542 KiB/s,写 16 KiB 713 KiB/s,内容对)。
- OpenOCD 烧录和校验 12 MHz 16/16、4 MHz 16/16,读 128 KiB 684 和 259 KiB/s,没有变化。
- probe-rs 在 4 MHz 下载并校验 3 次通过,9.6 MHz 基准没有变化。
- Keil 通过(Flash 下载和调试),它默认发 0,不触发这个 bug。

**请对方确认**:注释写着"保留历史序列:周期内驱动 SWDIO 高电平,结束后拉低"。为什么当年选高电平?有没有某个目标需要?我只在 STM32G431 上验证,没有看波形。建议加一个主机测试,检查空闲周期内 SWDIO 为低。其他 `Swd` 后端和手写引擎里如果有同样写法,也要看。

## 3. 这次一并复测、不用再动的

b493fed8(命令池 4 槽、暂存响应池):
- 块读 4 条在飞 12 MHz 966 KiB/s,4 MHz 292;纯 OUT 762~766;TAR 加块写 771;OpenOCD 读 128 KiB 12 MHz 692、4 MHz 261。
- 开机后第一条命令:复位 20 次,每次不预热直接打开会话,20/20 通过。
- 同步客户端先写 4 个再读不再死锁。

## 4. 仍未验证

热插拔时命令正在 SWD 执行、主机侧总线复位、Wi-Fi/TCP 路径、IAR、G431 目标上的空闲周期波形。
