// SPDX-License-Identifier: Apache-2.0
// USB/IP 虚拟设备：判据是 Linux 内核文档 Documentation/usb/usbip_protocol.rst（报文布局、
// 大端字段、RET_UNLINK 的 -ECONNRESET）、CMSIS-DAP v2 规范（接口 GUID）、微软 MS OS 2.0
// 描述符规范，以及 README 写明的设备参数（1D50:6199，bcdDevice 0x0200）。
//
// USB/IP virtual device: the oracles are the Linux kernel document
// Documentation/usb/usbip_protocol.rst (message layout, big-endian fields, -ECONNRESET in
// RET_UNLINK), the CMSIS-DAP v2 specification (interface GUID), Microsoft's MS OS 2.0
// descriptor specification, and the device parameters stated in the README (1D50:6199,
// bcdDevice 0x0200).
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>

#include "fakes.hpp"
#include "usbip_device.hpp"

namespace
{
using Test::Bytes;
using Test::FakeDap;
using Test::MemoryStream;

void Be16(Bytes& b, uint16_t v)
{
  b.push_back(static_cast<uint8_t>(v >> 8));
  b.push_back(static_cast<uint8_t>(v));
}

void Be32(Bytes& b, uint32_t v)
{
  Be16(b, static_cast<uint16_t>(v >> 16));
  Be16(b, static_cast<uint16_t>(v));
}

uint32_t GetBe32(const Bytes& b, size_t at)
{
  return (static_cast<uint32_t>(b[at]) << 24) | (static_cast<uint32_t>(b[at + 1]) << 16) |
         (static_cast<uint32_t>(b[at + 2]) << 8) | b[at + 3];
}

uint16_t GetBe16(const Bytes& b, size_t at)
{
  return static_cast<uint16_t>((b[at] << 8) | b[at + 1]);
}

Bytes OpRequest(uint16_t code)
{
  Bytes b;
  Be16(b, 0x0111);
  Be16(b, code);
  Be32(b, 0);
  return b;
}

Bytes Import(const char* busid)
{
  Bytes b = OpRequest(0x8003);
  Bytes id(32, 0);
  std::memcpy(id.data(), busid, std::strlen(busid));
  Test::Append(b, id);
  return b;
}

// CMD_SUBMIT：48 字节头加 OUT 数据 / CMD_SUBMIT: 48-byte header plus OUT data.
Bytes Submit(uint32_t seq, uint32_t dir, uint32_t ep, uint32_t length, const Bytes& setup,
             const Bytes& out = {})
{
  Bytes b;
  Be32(b, 1);
  Be32(b, seq);
  Be32(b, 0x00010002);
  Be32(b, dir);
  Be32(b, ep);
  Be32(b, 0);
  Be32(b, length);
  Be32(b, 0);
  Be32(b, 0);
  Be32(b, 0);
  Bytes s = setup;
  s.resize(8, 0);
  Test::Append(b, s);
  Test::Append(b, out);
  return b;
}

Bytes Unlink(uint32_t seq, uint32_t victim)
{
  Bytes b;
  Be32(b, 2);
  Be32(b, seq);
  Be32(b, 0x00010002);
  Be32(b, 0);
  Be32(b, 0);
  Be32(b, victim);
  b.resize(48, 0);
  return b;
}

struct Ret
{
  uint32_t command;
  uint32_t seqnum;
  int32_t status;
  uint32_t actual;
  Bytes data;
};

// 把 RET_SUBMIT / RET_UNLINK 序列拆开；IN 回复的数据长度按 actual_length 取，所以调用方
// 要告诉哪几个 seqnum 带数据 / Split a RET_SUBMIT / RET_UNLINK sequence; the data length
// of an IN reply is actual_length, so the caller says which seqnums carry data.
std::vector<Ret> ParseRets(const Bytes& out, const std::vector<uint32_t>& with_data)
{
  std::vector<Ret> rets;
  size_t at = 0;
  while (at + 48 <= out.size())
  {
    Ret r = {GetBe32(out, at),
             GetBe32(out, at + 4),
             static_cast<int32_t>(GetBe32(out, at + 20)),
             GetBe32(out, at + 24),
             {}};
    at += 48;
    bool has_data = false;
    for (uint32_t s : with_data)
    {
      has_data = has_data || s == r.seqnum;
    }
    if (r.command == 3 && has_data)
    {
      r.data.assign(out.begin() + static_cast<long>(at),
                    out.begin() + static_cast<long>(at + r.actual));
      at += r.actual;
    }
    rets.push_back(r);
  }
  CHECK(at == out.size());
  return rets;
}

struct Device
{
  FakeDap dap;
  XrDap::UsbipDevice<FakeDap> dev{dap};
  Device() { dev.SetStrings("XR-DAP-S3 CMSIS-DAP", "XRobot-XRDAP-000000000001"); }

  // 先 import 再跑 URB，返回 URB 部分的输出 / Import, then serve URBs; return the URB
  // part of the output.
  Bytes Urbs(const Bytes& urbs)
  {
    MemoryStream io;
    io.in = Import("1-1");
    Test::Append(io.in, urbs);
    CHECK(dev.Handshake(io));
    const size_t import_len = io.out.size();
    dev.ServeUrbs(io);
    return Bytes(io.out.begin() + static_cast<long>(import_len), io.out.end());
  }

  // GET_DESCRIPTOR 等控制读 / A control read such as GET_DESCRIPTOR.
  Bytes ControlIn(const Bytes& setup, uint32_t length)
  {
    const auto rets = ParseRets(Urbs(Submit(1, 1, 0, length, setup)), {1});
    CHECK(rets.size() == 1 && rets[0].status == 0);
    return rets.empty() ? Bytes{} : rets[0].data;
  }
};

// usbip_usb_device 的固定字段 / Fixed fields of usbip_usb_device.
void CheckUsbDevice(const Bytes& b, size_t at)
{
  CHECK(std::string(reinterpret_cast<const char*>(&b[at + 256])) == "1-1");
  CHECK(GetBe32(b, at + 288) == 1);  // busnum
  CHECK(GetBe32(b, at + 292) == 1);  // devnum
  CHECK(GetBe32(b, at + 296) == 3);  // USB_SPEED_HIGH
  CHECK(GetBe16(b, at + 300) == 0x1D50);
  CHECK(GetBe16(b, at + 302) == 0x6199);
  CHECK(GetBe16(b, at + 304) == 0x0200);
  CHECK(b[at + 309] == 1);  // bConfigurationValue
  CHECK(b[at + 310] == 1);  // bNumConfigurations
  CHECK(b[at + 311] == 1);  // bNumInterfaces
}
}  // namespace

TEST(usbip_devlist_reports_one_vendor_interface)
{
  Device d;
  MemoryStream io;
  io.in = OpRequest(0x8005);
  CHECK(!d.dev.Handshake(io));  // 列表查询不算挂载 / a list query is not an attach
  CHECK(io.out.size() == 8 + 4 + 312 + 4);
  CHECK(GetBe16(io.out, 0) == 0x0111);
  CHECK(GetBe16(io.out, 2) == 0x0005);
  CHECK(GetBe32(io.out, 4) == 0);
  CHECK(GetBe32(io.out, 8) == 1);
  CheckUsbDevice(io.out, 12);
  CHECK(io.out[12 + 312] == 0xFF);  // bInterfaceClass vendor
}

TEST(usbip_import_known_busid)
{
  Device d;
  MemoryStream io;
  io.in = Import("1-1");
  CHECK(d.dev.Handshake(io));
  CHECK(io.out.size() == 8 + 312);
  CHECK(GetBe16(io.out, 2) == 0x0003);
  CHECK(GetBe32(io.out, 4) == 0);
  CheckUsbDevice(io.out, 8);
}

TEST(usbip_import_unknown_busid_fails)
{
  Device d;
  MemoryStream io;
  io.in = Import("2-1");
  CHECK(!d.dev.Handshake(io));
  CHECK(io.out.size() == 8);
  CHECK(GetBe32(io.out, 4) != 0);
}

TEST(usbip_out_then_in_returns_dap_response)
{
  Device d;
  Bytes urbs = Submit(1, 0, 1, 2, {}, {0x00, 0xFE});
  Test::Append(urbs, Submit(2, 1, 1, 512, {}));
  const auto rets = ParseRets(d.Urbs(urbs), {2});
  CHECK(rets.size() == 2);
  if (rets.size() == 2)
  {
    CHECK(rets[0].command == 3 && rets[0].seqnum == 1 && rets[0].status == 0);
    CHECK(rets[0].actual == 2);
    CHECK(rets[1].seqnum == 2 && rets[1].status == 0);
    CHECK_BYTES(rets[1].data, (Bytes{0x00, 0xFE}));
  }
}

// 客户端可以先挂 IN 再发 OUT（OpenOCD、probe-rs 都这样）/ A client may queue the IN
// before the OUT (OpenOCD and probe-rs do).
TEST(usbip_in_queued_before_out_is_paired)
{
  Device d;
  Bytes urbs = Submit(1, 1, 1, 512, {});
  Test::Append(urbs, Submit(2, 0, 1, 2, {}, {0x00, 0x04}));
  const auto rets = ParseRets(d.Urbs(urbs), {1});
  CHECK(rets.size() == 2);
  if (rets.size() == 2)
  {
    CHECK(rets[0].seqnum == 2);
    CHECK(rets[1].seqnum == 1);
    CHECK_BYTES(rets[1].data, (Bytes{0x00, 0x04}));
  }
}

TEST(usbip_command_without_response_takes_no_in)
{
  Device d;
  Bytes urbs = Submit(1, 0, 1, 1, {}, {0x07});
  Test::Append(urbs, Submit(2, 0, 1, 2, {}, {0x00, 0x01}));
  Test::Append(urbs, Submit(3, 1, 1, 512, {}));
  const auto rets = ParseRets(d.Urbs(urbs), {3});
  CHECK(rets.size() == 3);
  if (rets.size() == 3)
  {
    CHECK_BYTES(rets[2].data, (Bytes{0x00, 0x01}));
  }
}

TEST(usbip_unlink_pending_in)
{
  Device d;
  Bytes urbs = Submit(5, 1, 1, 512, {});
  Test::Append(urbs, Unlink(6, 5));
  Test::Append(urbs, Unlink(7, 5));
  const auto rets = ParseRets(d.Urbs(urbs), {});
  CHECK(rets.size() == 2);
  if (rets.size() == 2)
  {
    CHECK(rets[0].command == 4 && rets[0].seqnum == 6 && rets[0].status == -104);
    CHECK(rets[1].command == 4 && rets[1].seqnum == 7 && rets[1].status == 0);
  }
}

TEST(usbip_unknown_endpoint_stalls)
{
  Device d;
  const auto rets = ParseRets(d.Urbs(Submit(1, 1, 2, 64, {})), {});
  CHECK(rets.size() == 1 && rets[0].status == -32);
}

TEST(usbip_device_descriptor)
{
  Device d;
  const Bytes desc = d.ControlIn({0x80, 0x06, 0x00, 0x01, 0, 0, 18, 0}, 18);
  CHECK(desc.size() == 18);
  if (desc.size() == 18)
  {
    CHECK(desc[0] == 18 && desc[1] == 1);
    CHECK(desc[2] == 0x10 && desc[3] == 0x02);  // bcdUSB 2.10（有 BOS / has a BOS）
    CHECK(desc[8] == 0x50 && desc[9] == 0x1D && desc[10] == 0x99 && desc[11] == 0x61);
    CHECK(desc[12] == 0x00 && desc[13] == 0x02);
  }
}

TEST(usbip_control_read_is_cut_to_requested_length)
{
  Device d;
  CHECK(d.ControlIn({0x80, 0x06, 0x00, 0x01, 0, 0, 8, 0}, 8).size() == 8);
}

TEST(usbip_strings)
{
  Device d;
  const Bytes product = d.ControlIn({0x80, 0x06, 0x02, 0x03, 0x09, 0x04, 255, 0}, 255);
  const Bytes iface = d.ControlIn({0x80, 0x06, 0x04, 0x03, 0x09, 0x04, 255, 0}, 255);
  auto ascii = [](const Bytes& s)
  {
    std::string r;
    for (size_t i = 2; i + 1 < s.size(); i += 2)
    {
      r += static_cast<char>(s[i]);
    }
    return r;
  };
  CHECK(ascii(product) == "XR-DAP-S3 CMSIS-DAP");
  // CMSIS-DAP v2：接口字符串含 "CMSIS-DAP" / CMSIS-DAP v2: the interface string contains
  // "CMSIS-DAP".
  CHECK(ascii(iface).find("CMSIS-DAP") != std::string::npos);
  const Bytes serial = d.ControlIn({0x80, 0x06, 0x03, 0x03, 0x09, 0x04, 255, 0}, 255);
  CHECK(ascii(serial) == "XRobot-XRDAP-000000000001");
}

// MS OS 2.0：BOS 里的平台能力给出厂商码，wIndex 7 取描述符集，里面有 WINUSB 兼容 ID 和
// CMSIS-DAP v2 的接口 GUID / MS OS 2.0: the platform capability in the BOS gives the
// vendor code; wIndex 7 fetches the set, which holds the WINUSB compatible ID and the
// CMSIS-DAP v2 interface GUID.
TEST(usbip_ms_os_20_descriptor_set)
{
  Device d;
  const Bytes bos = d.ControlIn({0x80, 0x06, 0x00, 0x0F, 0, 0, 255, 0}, 255);
  CHECK(bos.size() == 33);
  if (bos.size() != 33)
  {
    return;
  }
  const uint8_t vendor_code = bos[31];
  const uint16_t set_len = static_cast<uint16_t>(bos[29] | (bos[30] << 8));
  const Bytes set = d.ControlIn({0xC0, vendor_code, 0, 0, 0x07, 0x00, 255, 0}, 255);
  CHECK(set.size() == set_len);
  const std::string raw(set.begin(), set.end());
  CHECK(raw.find("WINUSB") != std::string::npos);
  std::string guid;
  for (size_t i = 0; i + 1 < set.size(); i += 2)
  {
    guid += static_cast<char>(set[i]);
  }
  CHECK(guid.find("{CDB3B5AD-293B-4663-AA36-1AAE46463776}") != std::string::npos);
}

// XRDAP-Core docs/protocol.en.md 第 6 节的描述符字节是唯一来源，XRDAP-Host 的 usbip
// 模块也 对照它：按顺序是设备、配置、限定、BOS、MS OS 2.0 描述符集 / The descriptor bytes
// in section 6 of XRDAP-Core docs/protocol.en.md are the single source, which
// XRDAP-Host's usbip module checks too: device, configuration, qualifier, BOS and MS
// OS 2.0 set, in that order.
namespace
{
std::vector<Bytes> DocDescriptors()
{
  std::vector<Bytes> blocks;
  std::ifstream file(XRDAP_PROTOCOL_DOC, std::ios::binary);
  CHECK(file.good());
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  const size_t start = text.find("Device descriptor (18 bytes)");
  CHECK(start != std::string::npos);
  const size_t end = text.find("```", start);
  if (start == std::string::npos || end == std::string::npos)
  {
    return blocks;
  }
  size_t pos = start;
  while (pos < end)
  {
    size_t eol = text.find('\n', pos);
    eol = (eol == std::string::npos || eol > end) ? end : eol;
    std::string line = text.substr(pos, eol - pos);
    pos = eol + 1;
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    // 全是两位十六进制数的行是数据，其余非空行开始一个新描述符 / A line of two-digit hex
    // values is data; any other non-empty line starts a new descriptor.
    Bytes row;
    bool hex = !line.empty();
    for (size_t i = 0; hex && i < line.size(); i += 3)
    {
      hex = i + 1 < line.size() && std::isxdigit(static_cast<unsigned char>(line[i])) &&
            std::isxdigit(static_cast<unsigned char>(line[i + 1])) &&
            (i + 2 == line.size() || line[i + 2] == ' ');
      if (hex)
      {
        row.push_back(static_cast<uint8_t>(std::stoul(line.substr(i, 2), nullptr, 16)));
      }
    }
    if (hex && !blocks.empty())
    {
      Test::Append(blocks.back(), row);
    }
    else if (!line.empty())
    {
      blocks.emplace_back();
    }
  }
  return blocks;
}
}  // namespace

TEST(usbip_descriptors_match_protocol_document)
{
  const std::vector<Bytes> doc = DocDescriptors();
  CHECK(doc.size() == 5);
  if (doc.size() != 5)
  {
    return;
  }
  Device d;
  CHECK_BYTES(d.ControlIn({0x80, 0x06, 0x00, 0x01, 0, 0, 255, 0}, 255), doc[0]);
  CHECK_BYTES(d.ControlIn({0x80, 0x06, 0x00, 0x02, 0, 0, 255, 0}, 255), doc[1]);
  CHECK_BYTES(d.ControlIn({0x80, 0x06, 0x00, 0x06, 0, 0, 255, 0}, 255), doc[2]);
  CHECK_BYTES(d.ControlIn({0x80, 0x06, 0x00, 0x0F, 0, 0, 255, 0}, 255), doc[3]);
  CHECK_BYTES(d.ControlIn({0xC0, 0x20, 0, 0, 0x07, 0x00, 255, 0}, 255), doc[4]);
}
