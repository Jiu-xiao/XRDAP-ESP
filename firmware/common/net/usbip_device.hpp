// SPDX-License-Identifier: Apache-2.0
#pragma once

// 经 USB/IP 协议提供的虚拟 CMSIS-DAP v2 USB 设备。电脑用 USB/IP 客户端（Linux 的
// `usbip attach`，Windows 的 usbip-win2）挂载后，系统里出现一个 USB 探针，Keil、pyOCD、
// probe-rs、OpenOCD 都按 USB 探针使用。设备：VID:PID 1D50:6199，bcdDevice 0x0200（与 S3
// 的真 USB 设备 0x0100 区分，Windows 按这三项缓存描述符），一个厂商类接口
// "CMSIS-DAP v2"，Bulk OUT 0x01、Bulk IN 0x81，高速 512 字节；BOS 里的 MS OS 2.0 描述符让
// Windows 自动绑定 WinUSB。协议字段大端，见 Linux 内核文档
// usb/usbip_protocol.rst。与上位机 xrdap 库的 usbip 模块是同一个设备模型。
//
// A virtual CMSIS-DAP v2 USB device served over the USB/IP protocol. After a USB/IP
// client attaches it (`usbip attach` on Linux, usbip-win2 on Windows) the computer sees a
// USB probe that Keil, pyOCD, probe-rs and OpenOCD use as usual. Device: VID:PID
// 1D50:6199, bcdDevice 0x0200 (distinct from the S3's real USB device, 0x0100, because
// Windows caches descriptors by these three), one vendor interface "CMSIS-DAP v2", Bulk
// OUT 0x01 and Bulk IN 0x81, high speed, 512 bytes; the MS OS 2.0 descriptor in the BOS
// makes Windows bind WinUSB. Fields are big-endian; see the Linux kernel's
// usb/usbip_protocol.rst. It is the same device model as the usbip module of the
// host-side xrdap crate.

#include <cstdint>
#include <cstring>

#include "probe_config.hpp"
#include "xrdap/probe/family.hpp"
#include "xrdap/probe/probe_identity.hpp"

namespace XrDap
{

/// USB/IP 虚拟 CMSIS-DAP 设备的一个会话 / One session of the USB/IP virtual CMSIS-DAP
/// device.
/// @tparam DapT 需提供 ExecuteExternal() / Must provide ExecuteExternal().
template <typename DapT>
class UsbipDevice
{
 public:
  /// @brief 构造设备 / Construct the device.
  /// @param dap 执行请求的 DAP 实例 / DAP instance that runs the requests.
  explicit UsbipDevice(DapT& dap) : dap_(dap) {}

  /// @brief 设置 USB 产品名和序列号 / Set the USB product name and serial number.
  /// @param product 例如 "XR-DAP-C3 CMSIS-DAP"，需一直有效 / Such as
  ///        "XR-DAP-C3 CMSIS-DAP"; must stay valid.
  /// @param serial 序列号，与 DAP_Info 相同，需一直有效 / Serial number, the same as in
  ///        DAP_Info; must stay valid.
  void SetStrings(const char* product, const char* serial)
  {
    product_ = product;
    serial_ = serial;
  }

  /// @brief 处理连接上的第一个操作 / Handle the first operation on a connection.
  /// @param io 已连接的字节流，见 DapTcpSession 对 StreamT 的要求 / Connected byte
  /// stream;
  ///        see DapTcpSession for the StreamT requirements.
  /// @return 客户端挂载了设备时返回 true，之后调用 ServeUrbs()；设备列表查询或出错时返回
  ///         false，调用方关闭连接 / True when the client attached the device (call
  ///         ServeUrbs() next); false for a device-list query or an error (close it).
  template <typename StreamT>
  bool Handshake(StreamT& io)
  {
    uint8_t op[8];
    if (!io.ReadAll(op, sizeof(op)))
    {
      return false;
    }
    const uint16_t code = static_cast<uint16_t>((op[2] << 8) | op[3]);
    uint8_t reply[8 + 4 + kUsbDeviceLen + 4];
    size_t n = 0;
    if (code == kOpReqDevlist)
    {
      n = PutOpHeader(reply, kOpRepDevlist, 0);
      n += PutU32(reply + n, 1);
      n += PutUsbDevice(reply + n);
      const uint8_t interface[4] = {0xFF, 0, 0, 0};
      memcpy(reply + n, interface, sizeof(interface));
      (void)io.WriteAll(reply, n + sizeof(interface));
      return false;
    }
    if (code != kOpReqImport)
    {
      return false;
    }
    char busid[32];
    if (!io.ReadAll(reinterpret_cast<uint8_t*>(busid), sizeof(busid)))
    {
      return false;
    }
    busid[sizeof(busid) - 1] = '\0';
    if (strcmp(busid, kBusId) != 0)
    {
      n = PutOpHeader(reply, kOpRepImport, 1);
      (void)io.WriteAll(reply, n);
      return false;
    }
    n = PutOpHeader(reply, kOpRepImport, 0);
    n += PutUsbDevice(reply + n);
    return io.WriteAll(reply, n);
  }

  /// @brief 处理 URB，直到连接断开 / Serve URBs until the connection closes.
  /// @param io Handshake() 返回 true 的字节流 / Stream for which Handshake() returned
  ///        true.
  template <typename StreamT>
  void ServeUrbs(StreamT& io)
  {
    pending_count_ = 0;
    tx_len_ = 0;
    resp_head_ = 0;
    resp_count_ = 0;
    uint8_t hdr[kHeaderLen];
    while (io.ReadAll(hdr, sizeof(hdr)))
    {
      const uint32_t cmd = GetU32(hdr);
      bool ok = false;
      if (cmd == kCmdSubmit)
      {
        ok = Submit(io, hdr);
      }
      else if (cmd == kCmdUnlink)
      {
        ok = Unlink(hdr);
      }
      if (!ok || !Flush(io))
      {
        return;
      }
    }
  }

 private:
  static constexpr uint16_t kVersion = 0x0111;
  static constexpr uint16_t kOpReqDevlist = 0x8005;
  static constexpr uint16_t kOpRepDevlist = 0x0005;
  static constexpr uint16_t kOpReqImport = 0x8003;
  static constexpr uint16_t kOpRepImport = 0x0003;
  static constexpr uint32_t kCmdSubmit = 1;
  static constexpr uint32_t kCmdUnlink = 2;
  static constexpr uint32_t kRetSubmit = 3;
  static constexpr uint32_t kRetUnlink = 4;
  static constexpr uint32_t kDirIn = 1;
  static constexpr uint32_t kDapEp = 1;
  static constexpr int32_t kEpipe = -32;
  static constexpr int32_t kEconnreset = -104;
  static constexpr size_t kHeaderLen = 48;
  static constexpr size_t kUsbDeviceLen = 312;
  static constexpr char kBusId[] = "1-1";
  static constexpr uint16_t kVid = Family::kUsbVid;
  static constexpr uint16_t kPid = Family::kUsbPid;
  static constexpr uint16_t kBcdDevice = Family::kBcdUsbip;
  static constexpr uint8_t kMsVendorCode = 0x20;
  static constexpr size_t kMaxPacket = Config::kDapPacketSize;
  static constexpr char kMsName[] = "DeviceInterfaceGUIDs";
  static constexpr char kMsGuid[] = "{CDB3B5AD-293B-4663-AA36-1AAE46463776}";
  // UTF-16LE。名称带一个结尾 0，值是 REG_MULTI_SZ，带两个 / UTF-16LE. The name has one
  // terminating NUL; the REG_MULTI_SZ value has two.
  static constexpr size_t kMsNameLen = 2 * sizeof(kMsName);
  static constexpr size_t kMsDataLen = 2 * (sizeof(kMsGuid) + 1);
  static constexpr size_t kMsOs20SetLen = 10 + 20 + 10 + kMsNameLen + kMsDataLen;
  static_assert(kMsOs20SetLen == 162, "MS OS 2.0 descriptor set length");
  // 客户端最多先发 DAP_Info 报的包数个 OUT，再取响应 / A client sends at most the
  // DAP_Info packet count of OUTs before it collects the responses.
  static constexpr size_t kQueueDepth = Config::kDapPacketCount;

  struct PendingIn
  {
    uint32_t seqnum;
    uint32_t length;
  };

  struct Response
  {
    uint16_t length;
    uint8_t data[kMaxPacket];
  };

  // ---------------------------------------------------------------- 字节序

  static size_t PutU16(uint8_t* p, uint16_t v)
  {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
    return 2;
  }

  static size_t PutU32(uint8_t* p, uint32_t v)
  {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
    return 4;
  }

  static uint32_t GetU32(const uint8_t* p)
  {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
  }

  static size_t PutOpHeader(uint8_t* p, uint16_t code, uint32_t status)
  {
    PutU16(p, kVersion);
    PutU16(p + 2, code);
    PutU32(p + 4, status);
    return 8;
  }

  /// usbip_usb_device：路径、总线号、速度、VID/PID 等 / Path, bus id, speed, VID/PID...
  static size_t PutUsbDevice(uint8_t* p)
  {
    memset(p, 0, kUsbDeviceLen);
    static constexpr char kPath[] = "/sys/devices/xrdap/usb1/1-1";
    memcpy(p, kPath, sizeof(kPath));
    memcpy(p + 256, kBusId, sizeof(kBusId));
    size_t n = 288;
    n += PutU32(p + n, 1);  // busnum
    n += PutU32(p + n, 1);  // devnum
    n += PutU32(p + n, 3);  // USB_SPEED_HIGH
    n += PutU16(p + n, kVid);
    n += PutU16(p + n, kPid);
    n += PutU16(p + n, kBcdDevice);
    // 设备类、子类、协议、当前配置、配置数、接口数 / Class, subclass, protocol,
    // configuration value, configuration count, interface count.
    const uint8_t tail[6] = {0, 0, 0, 1, 1, 1};
    memcpy(p + n, tail, sizeof(tail));
    return kUsbDeviceLen;
  }

  /// 追加一个 RET_SUBMIT 或 RET_UNLINK，数据已放在 Payload() / Append a RET_SUBMIT or
  /// RET_UNLINK whose data is already in Payload().
  void AddRet(uint32_t cmd, uint32_t seqnum, int32_t status, uint32_t actual,
              size_t data_len)
  {
    uint8_t* h = tx_ + tx_len_;
    memset(h, 0, kHeaderLen);
    PutU32(h, cmd);
    PutU32(h + 4, seqnum);
    PutU32(h + 20, static_cast<uint32_t>(status));
    PutU32(h + 24, actual);
    tx_len_ += kHeaderLen + data_len;
  }

  /// 下一个回复的数据区 / Data area of the next reply.
  uint8_t* Payload() { return tx_ + tx_len_ + kHeaderLen; }

  /// 一个命令引起的回复（OUT 的回复和配上的 IN 响应）合成一次 send()。分两次发时，第二段
  /// 要等对方的延迟 ACK，每个来回多出约 40 ms / Send all replies caused by one command
  /// (the OUT reply and the paired IN response) in one send(). Sent separately, the
  /// second segment waits for the peer's delayed ACK, adding about 40 ms per round trip.
  template <typename StreamT>
  bool Flush(StreamT& io)
  {
    const size_t n = tx_len_;
    tx_len_ = 0;
    return n == 0 || io.WriteAll(tx_, n);
  }

  // ---------------------------------------------------------------- 描述符

  static size_t PutString(uint8_t* p, const char* s)
  {
    size_t n = 2;
    for (; *s && n + 2 <= 255; ++s)
    {
      p[n++] = static_cast<uint8_t>(*s);
      p[n++] = 0;
    }
    p[0] = static_cast<uint8_t>(n);
    p[1] = 3;
    return n;
  }

  static size_t PutUtf16(uint8_t* p, const char* s, size_t len)
  {
    for (size_t i = 0; i < len; ++i)
    {
      p[2 * i] = static_cast<uint8_t>(s[i]);
      p[2 * i + 1] = 0;
    }
    return 2 * len;
  }

  /// MS OS 2.0 描述符集：兼容 ID WINUSB，设备接口 GUID 为 CMSIS-DAP v2 约定的值 /
  /// MS OS 2.0 descriptor set: compatible ID WINUSB and the CMSIS-DAP v2 interface GUID.
  static size_t PutMsOs20Set(uint8_t* p)
  {
    const size_t name_len = kMsNameLen;
    const size_t data_len = kMsDataLen;
    const size_t prop_len = 10 + name_len + data_len;
    const size_t total = kMsOs20SetLen;
    memset(p, 0, total);
    const uint8_t header[8] = {10, 0, 0, 0, 0x00, 0x00, 0x03, 0x06};
    memcpy(p, header, sizeof(header));
    p[8] = static_cast<uint8_t>(total);
    p[9] = static_cast<uint8_t>(total >> 8);
    const uint8_t compat[4] = {20, 0, 3, 0};
    memcpy(p + 10, compat, sizeof(compat));
    memcpy(p + 14, "WINUSB", 6);
    size_t n = 30;
    p[n] = static_cast<uint8_t>(prop_len);
    p[n + 1] = static_cast<uint8_t>(prop_len >> 8);
    p[n + 2] = 4;  // MS_OS_20_FEATURE_REG_PROPERTY
    p[n + 4] = 7;  // REG_MULTI_SZ
    p[n + 6] = static_cast<uint8_t>(name_len);
    n += 8;
    n += PutUtf16(p + n, kMsName, sizeof(kMsName));
    p[n] = static_cast<uint8_t>(data_len);
    n += 2;
    PutUtf16(p + n, kMsGuid, sizeof(kMsGuid));
    return total;
  }

  /// 控制传输的响应数据；不支持的请求返回 -1（STALL）/ Response data for a control
  /// request; -1 (STALL) when unsupported.
  int Control(const uint8_t* setup, uint8_t* out)
  {
    const uint8_t type = setup[0];
    const uint8_t req = setup[1];
    const uint8_t desc_index = setup[2];
    const uint8_t desc_type = setup[3];
    const uint16_t index = static_cast<uint16_t>(setup[4] | (setup[5] << 8));
    if (type == 0x80 && req == 0x06)
    {
      switch (desc_type)
      {
        case 1:
        {
          const uint8_t d[18] = {18,
                                 1,
                                 0x10,
                                 0x02,  // bcdUSB 2.10：有 BOS / bcdUSB 2.10: has a BOS
                                 0,
                                 0,
                                 0,
                                 64,
                                 static_cast<uint8_t>(kVid),
                                 static_cast<uint8_t>(kVid >> 8),
                                 static_cast<uint8_t>(kPid),
                                 static_cast<uint8_t>(kPid >> 8),
                                 static_cast<uint8_t>(kBcdDevice),
                                 static_cast<uint8_t>(kBcdDevice >> 8),
                                 1,
                                 2,
                                 3,
                                 1};
          memcpy(out, d, sizeof(d));
          return sizeof(d);
        }
        case 2:
        {
          const uint8_t d[32] = {
              9, 2, 32,   0, 1,    1,    0, 0x80, 50,  // 配置 / Configuration
              9, 4, 0,    0, 2,    0xFF, 0, 0,    4,   // 厂商类接口 / Vendor interface
              7, 5, 0x01, 2, 0x00, 0x02, 0,            // Bulk OUT 512
              7, 5, 0x81, 2, 0x00, 0x02, 0,            // Bulk IN 512
          };
          memcpy(out, d, sizeof(d));
          return sizeof(d);
        }
        case 6:
        {
          const uint8_t d[10] = {10, 6, 0x00, 0x02, 0, 0, 0, 64, 1, 0};
          memcpy(out, d, sizeof(d));
          return sizeof(d);
        }
        case 0x0F:
        {
          constexpr size_t set_len = kMsOs20SetLen;
          // BOS 头加一个 MS OS 2.0 平台能力描述符 / BOS header plus one MS OS 2.0
          // platform capability descriptor.
          const uint8_t d[33] = {5,
                                 0x0F,
                                 33,
                                 0,
                                 1,
                                 28,
                                 0x10,
                                 5,
                                 0,
                                 0xDF,
                                 0x60,
                                 0xDD,
                                 0xD8,
                                 0x89,
                                 0x45,
                                 0xC7,
                                 0x4C,
                                 0x9C,
                                 0xD2,
                                 0x65,
                                 0x9D,
                                 0x9E,
                                 0x64,
                                 0x8A,
                                 0x9F,
                                 0x00,
                                 0x00,
                                 0x03,
                                 0x06,
                                 static_cast<uint8_t>(set_len),
                                 static_cast<uint8_t>(set_len >> 8),
                                 kMsVendorCode,
                                 0};
          memcpy(out, d, sizeof(d));
          return sizeof(d);
        }
        case 3:
          switch (desc_index)
          {
            case 0:
            {
              const uint8_t d[4] = {4, 3, 0x09, 0x04};
              memcpy(out, d, sizeof(d));
              return sizeof(d);
            }
            case 1:
              return static_cast<int>(PutString(out, kVendorName));
            case 2:
              return static_cast<int>(PutString(out, product_));
            case 3:
              return static_cast<int>(PutString(out, serial_));
            case 4:
              return static_cast<int>(PutString(out, "CMSIS-DAP v2"));
            default:
              return -1;
          }
        default:
          return -1;
      }
    }
    if (type == 0xC0 && req == kMsVendorCode && index == 7)
    {
      return static_cast<int>(PutMsOs20Set(out));
    }
    if (type >= 0x80 && type <= 0x82 && req == 0x00)  // GET_STATUS
    {
      out[0] = 0;
      out[1] = 0;
      return 2;
    }
    if (type == 0x80 && req == 0x08)  // GET_CONFIGURATION
    {
      out[0] = 1;
      return 1;
    }
    if (type == 0x81 && req == 0x0A)  // GET_INTERFACE
    {
      out[0] = 0;
      return 1;
    }
    // SET_CONFIGURATION、SET_INTERFACE、CLEAR_FEATURE、SET_FEATURE、SET_ADDRESS
    if ((type == 0x00 && (req == 0x09 || req == 0x05)) || (type == 0x01 && req == 0x0B) ||
        (type == 0x02 && (req == 0x01 || req == 0x03)))
    {
      return 0;
    }
    return -1;
  }

  // ---------------------------------------------------------------- URB

  /// 把排队的响应交给等着的 IN 请求 / Hand queued responses to waiting IN requests.
  void Pair()
  {
    while (pending_count_ && resp_count_)
    {
      const PendingIn in = pending_[0];
      memmove(pending_, pending_ + 1, (pending_count_ - 1) * sizeof(PendingIn));
      --pending_count_;
      const Response& r = responses_[resp_head_];
      resp_head_ = (resp_head_ + 1) % kQueueDepth;
      --resp_count_;
      const size_t len = r.length < in.length ? r.length : in.length;
      memcpy(Payload(), r.data, len);
      AddRet(kRetSubmit, in.seqnum, 0, static_cast<uint32_t>(len), len);
    }
  }

  template <typename StreamT>
  bool Submit(StreamT& io, const uint8_t* hdr)
  {
    const uint32_t seqnum = GetU32(hdr + 4);
    const uint32_t dir = GetU32(hdr + 12);
    const uint32_t ep = GetU32(hdr + 16);
    const uint32_t length = GetU32(hdr + 24);
    const uint8_t* setup = hdr + 40;
    const uint32_t out_len = dir == kDirIn ? 0 : length;
    if (out_len > kMaxPacket)
    {
      return false;
    }
    if (!io.ReadAll(rx_, out_len))
    {
      return false;
    }

    if (ep == 0)
    {
      const int n = Control(setup, Payload());
      if (n < 0)
      {
        AddRet(kRetSubmit, seqnum, kEpipe, 0, 0);
        return true;
      }
      size_t len = dir == kDirIn ? static_cast<size_t>(n) : 0;
      len = len < length ? len : length;
      AddRet(kRetSubmit, seqnum, 0, static_cast<uint32_t>(len), len);
      return true;
    }
    if (ep != kDapEp)
    {
      AddRet(kRetSubmit, seqnum, kEpipe, 0, 0);
      return true;
    }
    if (dir == kDirIn)
    {
      if (pending_count_ == kQueueDepth)
      {
        AddRet(kRetSubmit, seqnum, kEpipe, 0, 0);
        return true;
      }
      pending_[pending_count_++] = {seqnum, length};
      Pair();
      return true;
    }
    if (resp_count_ == kQueueDepth)
    {
      AddRet(kRetSubmit, seqnum, kEpipe, 0, 0);
      return true;
    }
    Response& r = responses_[(resp_head_ + resp_count_) % kQueueDepth];
    uint16_t resp_len = 0;
    (void)dap_.ExecuteExternal(rx_, static_cast<uint16_t>(out_len), r.data, kMaxPacket,
                               resp_len);
    r.length = resp_len;
    // 没有应答的命令（DAP_TransferAbort）不占 IN / Commands without a response
    // (DAP_TransferAbort) take no IN.
    if (resp_len)
    {
      ++resp_count_;
    }
    // OUT 的回复只带写入的字节数，不带数据 / An OUT reply carries the byte count only.
    AddRet(kRetSubmit, seqnum, 0, out_len, 0);
    Pair();
    return true;
  }

  bool Unlink(const uint8_t* hdr)
  {
    const uint32_t seqnum = GetU32(hdr + 4);
    const uint32_t victim = GetU32(hdr + 20);
    int32_t status = 0;
    for (size_t i = 0; i < pending_count_; ++i)
    {
      if (pending_[i].seqnum == victim)
      {
        memmove(pending_ + i, pending_ + i + 1,
                (pending_count_ - i - 1) * sizeof(PendingIn));
        --pending_count_;
        status = kEconnreset;
        break;
      }
    }
    AddRet(kRetUnlink, seqnum, status, 0, 0);
    return true;
  }

  DapT& dap_;
  const char* product_ = "XR-DAP CMSIS-DAP";
  const char* serial_ = "";
  PendingIn pending_[kQueueDepth] = {};
  size_t pending_count_ = 0;
  Response responses_[kQueueDepth] = {};
  size_t resp_head_ = 0;
  size_t resp_count_ = 0;
  uint8_t rx_[kMaxPacket] = {};
  // 一个命令最多引起两个回复：OUT 的回复和一个 IN 响应 / One command causes at most two
  // replies: the OUT reply and one IN response.
  uint8_t tx_[2 * kHeaderLen + kMaxPacket] = {};
  size_t tx_len_ = 0;
};

}  // namespace XrDap
