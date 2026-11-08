// SPDX-License-Identifier: Apache-2.0
// SerialMux：判据是 serial_mux.hpp 文件头对规则的陈述——读到的每块数据先交给 DAP 链路；
// 这一块没有执行任何 DAP 帧、且最近 kQuietMs（2000 ms）内也没有 DAP 帧时，字节才交给
// 命令行；DAP 帧执行时命令行丢掉没写完的一行。命令行是否收到字节，用 "wifi clear"、
// "wifi set" 是否生效来看。时间由测试推进 /
// SerialMux: the oracle is the rule stated in the header of serial_mux.hpp — each chunk
// read goes to the DAP link first; only when the chunk executed no DAP frame and no DAP
// frame was seen within kQuietMs (2000 ms) do the bytes go to the console; when a DAP
// frame runs, the console drops its unfinished line. Whether the console received bytes
// shows in whether "wifi clear" and "wifi set" took effect. The test advances time.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "fakes.hpp"
#include "serial_mux.hpp"
#include "xrdap/serial/serial_frame.hpp"

// 测试用的 FreeRTOS：任务是分离的线程，滴答由测试设定 / FreeRTOS for the test: a task is
// a detached thread, the tick is set by the test.
namespace
{
std::atomic<uint32_t> g_tick{0};
}  // namespace

BaseType_t xTaskCreatePinnedToCore(void (*entry)(void*), const char*, uint32_t, void* arg,
                                   UBaseType_t, void*, BaseType_t)
{
  std::thread(entry, arg).detach();
  return pdPASS;
}

TickType_t xTaskGetTickCount() { return g_tick.load(); }

void vTaskDelay(TickType_t) {}

namespace
{
using Test::Bytes;

constexpr uint32_t kQuietMs = 2000U;

// 文档第 1 节的例子：DAP_Info(0xFF) 请求 / The example of docs/protocol.md section 1:
// the DAP_Info(0xFF) request.
const uint8_t kInfoFrame[] = {0x5A, 0x04, 0x00, 0x00, 0xC5, 0x09, 0x83,
                              0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x01, 0xB1, 0x02, 0x00, 0x00, 0xFF, 0x32};

Bytes Frame() { return Bytes(kInfoFrame, kInfoFrame + sizeof(kInfoFrame)); }

Bytes Text(const char* s) { return Bytes(s, s + std::strlen(s)); }

Bytes Cat(const Bytes& a, const Bytes& b)
{
  Bytes all = a;
  all.insert(all.end(), b.begin(), b.end());
  return all;
}

// 假串口：读端口的队列，零长度读等到队列非空 / Fake UART: the read port's queue; a
// zero-length read waits for a nonempty queue.
class FakeUart
{
 public:
  struct Port
  {
    FakeUart* uart;
    size_t Size() const { return uart->Queued(); }
  };

  Port* read_port_ = &port_;

  LibXR::ErrorCode Read(LibXR::RawData data, LibXR::ReadOperation&)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (data.size_ == 0U)
    {
      const bool got = cv_.wait_for(lock, std::chrono::milliseconds(50),
                                    [this] { return !queue_.empty(); });
      return got ? LibXR::ErrorCode::OK : LibXR::ErrorCode::TIMEOUT;
    }
    if (queue_.size() < data.size_)
    {
      return LibXR::ErrorCode::EMPTY;
    }
    auto* out = static_cast<uint8_t*>(data.addr_);
    for (size_t i = 0; i < data.size_; ++i)
    {
      out[i] = queue_[i];
    }
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<long>(data.size_));
    return LibXR::ErrorCode::OK;
  }

  LibXR::ErrorCode Write(LibXR::ConstRawData, LibXR::WriteOperation&)
  {
    return LibXR::ErrorCode::OK;
  }

  // 一次放进整块，读任务看到的就是整块 / Put in a whole chunk at once, so the reader
  // sees the whole chunk.
  void Push(const Bytes& chunk)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.insert(queue_.end(), chunk.begin(), chunk.end());
    cv_.notify_all();
  }

  size_t Queued()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
  }

 private:
  Port port_{this};
  std::mutex mutex_;
  std::condition_variable cv_;
  Bytes queue_;
};

// 假 DAP 链路：用 Core 的 Framer 找帧，返回这块执行的请求数；CheckLease 数读任务的轮数 /
// Fake DAP link: finds frames with Core's Framer and returns the requests this chunk ran;
// CheckLease counts the reader's passes.
class FakeLink
{
 public:
  std::atomic<uint32_t> passes{0U};
  std::atomic<uint32_t> requests{0U};

  void CheckLease() { passes.fetch_add(1U); }

  uint32_t Feed(const uint8_t* data, size_t len)
  {
    const uint32_t ran = framer_.Feed(data, len, [](const uint8_t*, uint16_t, bool) {});
    requests.fetch_add(ran);
    return ran;
  }

 private:
  XrDap::Serial::Framer<1024> framer_;
};

// 一个运行中的复用器。读任务不会退出，所以整套对象不释放 / A running multiplexer. The
// reader never exits, so the objects are never freed.
struct Rig
{
  Test::FakeWifiSettings wifi;
  FakeUart uart;
  FakeLink link;
  XrDap::SerialMux<FakeLink, FakeUart> mux{link, uart, wifi, nullptr};

  Rig() { mux.Start(0, 1U); }

  // 在滴答 tick 处放入一块，等读任务处理完 / Put a chunk in at the given tick and wait
  // until the reader has processed it.
  void Send(uint32_t tick, const Bytes& chunk)
  {
    g_tick.store(tick);
    const uint32_t before = link.passes.load();
    uart.Push(chunk);
    // 放入时读任务可能已在这一轮里：再多一轮才能确定这块处理完了 / The reader may already
    // be in a pass when the chunk arrives: one more pass proves it was processed.
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((link.passes.load() < before + 2U || uart.Queued() != 0U) &&
           std::chrono::steady_clock::now() < limit)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(link.passes.load() >= before + 2U && uart.Queued() == 0U);
  }
};

Rig& NewRig() { return *new Rig(); }
}  // namespace

TEST(mux_text_reaches_console_when_no_frame_was_seen)
{
  Rig& rig = NewRig();
  rig.Send(10000U, Text("wifi clear\r"));
  CHECK(rig.wifi.clears == 1);
  CHECK(rig.link.requests.load() == 0U);
}

TEST(mux_no_frame_seen_means_no_quiet_window_even_near_clock_zero)
{
  Rig& rig = NewRig();
  rig.Send(100U, Text("wifi clear\r"));
  CHECK(rig.wifi.clears == 1);
}

TEST(mux_frame_chunk_does_not_reach_console)
{
  Rig& rig = NewRig();
  rig.Send(10000U, Frame());
  CHECK(rig.link.requests.load() == 1U);
  CHECK(rig.wifi.clears == 0);
}

TEST(mux_text_in_a_chunk_that_ran_a_frame_is_dropped)
{
  Rig& rig = NewRig();
  rig.Send(10000U, Cat(Text("wifi clear\r"), Frame()));
  CHECK(rig.link.requests.load() == 1U);
  CHECK(rig.wifi.clears == 0);
  rig.Send(20000U, Cat(Frame(), Text("wifi clear\r")));
  CHECK(rig.link.requests.load() == 2U);
  CHECK(rig.wifi.clears == 0);
}

TEST(mux_text_inside_the_quiet_window_is_dropped)
{
  Rig& rig = NewRig();
  rig.Send(5000U, Frame());
  rig.Send(5000U + kQuietMs - 1U, Text("wifi clear\r"));
  CHECK(rig.wifi.clears == 0);
}

TEST(mux_text_at_the_end_of_the_quiet_window_is_taken)
{
  Rig& rig = NewRig();
  rig.Send(5000U, Frame());
  rig.Send(5000U + kQuietMs, Text("wifi clear\r"));
  CHECK(rig.wifi.clears == 1);
}

TEST(mux_each_frame_renews_the_quiet_window)
{
  Rig& rig = NewRig();
  rig.Send(5000U, Frame());
  rig.Send(6500U, Frame());
  rig.Send(6500U + kQuietMs - 1U, Text("wifi clear\r"));
  CHECK(rig.wifi.clears == 0);
  rig.Send(6500U + kQuietMs, Text("wifi clear\r"));
  CHECK(rig.wifi.clears == 1);
}

TEST(mux_frame_drops_the_unfinished_console_line)
{
  Rig& rig = NewRig();
  rig.Send(1000U, Text("wifi cl"));
  rig.Send(1000U, Frame());
  rig.Send(9000U, Text("ear\r"));
  CHECK(rig.wifi.clears == 0);
}

TEST(mux_line_in_two_chunks_runs_when_no_frame_comes)
{
  Rig& rig = NewRig();
  rig.Send(1000U, Text("wifi cl"));
  rig.Send(9000U, Text("ear\r"));
  CHECK(rig.wifi.clears == 1);
}

TEST(mux_text_before_an_unfinished_frame_reaches_console)
{
  // 块里只有一个没写完的帧：没有帧执行，前面的文字照常交给命令行 / A chunk with only an
  // unfinished frame: no frame ran, so the text before it goes to the console.
  Rig& rig = NewRig();
  Bytes partial = Frame();
  partial.resize(10);
  rig.Send(10000U, Cat(Text("wifi clear\r"), partial));
  CHECK(rig.link.requests.load() == 0U);
  CHECK(rig.wifi.clears == 1);
}

TEST(mux_line_ending_in_the_frame_start_byte_reaches_console)
{
  // 'Z' 是帧起始字节；命令行只丢以 'Z' 开头的行，行尾的 'Z' 要照常到达 / 'Z' is the frame
  // start byte; the console drops only lines that start with 'Z', so a trailing 'Z' must
  // arrive as usual.
  Rig& rig = NewRig();
  rig.Send(10000U, Text("wifi set net passwordZ\r"));
  CHECK(rig.wifi.saves == 1);
  CHECK(rig.wifi.saved_password == "passwordZ");
}
