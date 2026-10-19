# XR-DAP Link Framing and XR Vendor Extension

[中文](protocol.md)

This document describes the frame format used by XRDAP-ESP (ESP32-S3, ESP32-C3) to carry CMSIS-DAP packets over serial and TCP, the XR vendor extension commands implemented by the probe (0x80 to 0x85 are data commands, 0x86 is Wi-Fi configuration), the probe identity and mDNS advertisement, and how the ESP32-C3 USB port separates the DAP link from the command line. DAP packets on USB use the CMSIS-DAP v2 Bulk endpoints (ESP32-S3 only) without any extra framing.

All integers are little-endian. Spaces in hexadecimal examples are for readability only.

Contents: 1 Serial frames; 2 TCP frames; 3 XR vendor extension (3.1 to 3.6 cover rules, commands and sampling for 0x80 to 0x85, 3.7 covers 0x86, 3.8 holds byte examples); 4 Probe identity and discovery (DAP_Info, mDNS); 5 The ESP32-C3 USB port.

## 1. Serial frames

The serial link uses libxr Topic packed frames. On the firmware side it is implemented by libxr `DapSerialTransport` (`src/driver/debug/dap_serial_transport.hpp`); on the host side by the `xrdap` library of XRDAP-Host (`transport::serial`).

```
prefix 0x5A(1) + data_len(3) + topic_crc32(4) + timestamp_us(6) + version(1) + header_crc8(1) + payload(data_len) + crc8(1)
```

| Field | Content |
|---|---|
| prefix | Fixed `0x5A` |
| data_len | Payload length in bytes, 24 bits |
| topic_crc32 | CRC32 of the topic name. The request topic is `dap_req` (`0xDC8309C5`), the response topic is `dap_resp` (`0x89E1A688`) |
| timestamp_us | 48 bits. The probe fills in its own microsecond time in responses; requests carry 0 and the probe ignores it |
| version | Fixed `0x01` |
| header_crc8 | CRC8 of the first 15 bytes |
| payload | `n(2) + DAP packet(n)`, where n is the DAP packet length; no padding |
| crc8 | CRC8 of the header and payload |

CRC8 uses the reflected polynomial `0x8C` with initial value `0xFF`. CRC32 uses the reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF` and no final inversion (the value for `"123456789"` is `0x340BC6D9`).

Example: the complete frame for the request `DAP_Info(0xFF)` (query packet size; the DAP packet is `00 FF`):

```
5A 04 00 00 C5 09 83 DC 00 00 00 00 00 00 01 B1 02 00 00 FF 32
```

The probe parses frames by these rules:

- If the header CRC8 is wrong, version is not 1, the topic is not `dap_req`, or data_len is smaller than 2 or larger than 2 + 1024, the current candidate frame is discarded and parsing restarts at the next `0x5A` in the buffer.
- A wrong trailing CRC8 likewise discards the frame and resynchronises.
- A frame with n larger than 1024 or n + 2 larger than data_len is discarded without a response.
- A discarded request gets no response; the host retries after a timeout. `xrdap share` waits 2 seconds (`--timeout-ms`) and then returns a single byte `0xFF` to the debug tool.

Requests and responses correspond one to one. The host sends one request at a time and sends the next after receiving the response. For a command with no response content, n in the response frame is 0.

The firmware baud rate is 4 000 000 (`kSerialBaud` in `firmware/esp32s3/main/config.hpp` for the S3; the C3 uses USB Serial/JTAG, where the baud rate has no effect), 8 data bits, no parity, 1 stop bit, no flow control.

A serial port has no notion of connecting or disconnecting. The probe handles sampling started over serial separately, see section 3.5.

## 2. TCP frames

The probe's TCP port 3241 and the listening port of `xrdap share openocd` use the same frame, matching OpenOCD `cmsis-dap backend tcp`:

```
"DAP\0"(4) + length(2) + type(1) + reserved(1) + DAP packet
```

Type 1 is a request, type 2 is a response; the reserved byte is 0. The length is the number of DAP packet bytes that follow.

- Only one client is served at a time; requests are executed and answered in order.
- The probe closes the connection when it receives an invalid header (wrong magic, type other than 1, length 0 or larger than 1024).
- A command with no response content (for example `DAP_TransferAbort`) gets no TCP frame back.
- `xrdap share openocd` strips the 8-byte header from a TCP frame, hands the DAP packet to the probe (wrapped in a serial frame when the link is serial), and converts responses back; it does not interpret DAP content. After the client disconnects it sends `84 00` (stop sampling) to the probe once.

## Notes for USB hosts

A full-speed USB Bulk packet is 64 bytes. When a request length is an exact multiple of 64 (128, 192 bytes, ...) and smaller than the DAP packet size, the OUT transfer has no short packet to mark its end, so the probe keeps waiting: the request gets no response and later responses are misaligned. After sending such a request the host must send a zero-length packet. A SUB_SET with 8 items is exactly 128 bytes and is the most likely case. `xrdap` handles this; clients written with libusb or pyusb must add it. For reception, the IN request length must be a multiple of the endpoint maximum packet size (and not smaller than the DAP packet size).

## 3. XR vendor extension

### 3.1 Rules

- The commands live in the CMSIS-DAP vendor command range: 0x80 to 0x85 are data commands (sections 3.2 to 3.6) and 0x86 is Wi-Fi configuration (section 3.7). USB, TCP and serial use the same packets.
- A command the probe does not support gets a one-byte response `0xFF`, which is standard CMSIS-DAP behaviour. A host that receives `FF` for INFO can conclude that the probe does not implement the extension. The probe supports 0x86 only when INFO feature bit 4 is 1.
- Only SWD is supported. The target is treated as little-endian; values are the raw bytes in target memory.
- The host resolves variable addresses (symbol table, DWARF); the probe only handles addresses.
- Before using the extension commands, the host must connect with `DAP_Connect` using SWD (port 0 or 1) and confirm the target is accessible. The probe does not connect on behalf of the host; before a connection, READ, WRITE, SUB_SET and start-sampling return 0x04. After `DAP_Disconnect`, a TCP client connecting or disconnecting, or sampling stopping automatically because its transport went away, `DAP_Connect` must be issued again.
- After READ, WRITE or sampling, the host must treat DP SELECT, AP CSW and TAR as unknown and write them again. OpenOCD caches these registers, so the extension commands must not be mixed with OpenOCD in the same connection.

While sampling runs, the probe is held exclusively by the subscription:

- From start to stop, only `DAP_Info` (0x00), `DAP_HostStatus` (0x01) and 0x80 to 0x86 are accepted. Every other command gets a single byte `0xFF` and is not executed.
- READ and WRITE can be used during sampling. They share the SWD bus with the sampling task in turn and can cause skipped ticks.
- Sampling stops in these cases:

| Case | Sampling stopped |
|---|---|
| SUB_CTRL stop (`84 00`) received | Sampling started over any transport |
| USB bus reset (unplug or host reset) | Sampling started over USB |
| TCP client disconnects | Sampling started over any transport |
| A new TCP client connects | Sampling started over any transport |
| No serial request for 5 seconds | Sampling started over serial |

After starting sampling over serial, the host must send a request (SUB_READ will do) at least every 5 seconds. After an automatic stop the frames in the buffer are kept and can still be read with SUB_READ until the next SUB_SET.

### 3.2 Requests, responses and status

A request is `command byte + parameters`; a response is `command byte + overall status + data`, with the command byte echoed unchanged. When the overall status is not 0, the response is just these two bytes, except for the 0x03 returned by SUB_SET for insufficient resources (see 3.4).

A request whose length does not match the count field in the command returns 0x01 and is not executed. READ and WRITE compute the response size before executing; if it does not fit in one DAP packet (1024 bytes) they return 0x03 and do nothing.

| Overall status | Meaning |
|---|---|
| 0x00 | Success; per-item results are in the item status |
| 0x01 | Invalid argument: length mismatch, count 0, size not 1/2/4/8, dereference count above 8, period 0, div 0 or phase >= div, reserved flag bits set, undefined SUB_CTRL op |
| 0x02 | State not allowed: SUB_SET or start while sampling; start or read without a successful SUB_SET; the SWD link was not free within 2 seconds |
| 0x03 | Insufficient resources: more than 32 items, response does not fit in one packet, one subscription uses more than 8 distinct APs, estimated frame time larger than the period |
| 0x04 | No target connected: `DAP_Connect` not yet issued, or reading the AP CSW failed |

Item status (one byte per item for READ and WRITE; a sampling frame uses 1 bit per item for success or failure):

| Item status | Meaning |
|---|---|
| 0x00 | Success |
| 0x10 + k | SWD returned FAULT or timed out while reading the pointer at level k (0 to 7) |
| 0x1F | SWD failed on the final read or write (including a sticky error detected after a write) |
| 0x20 + k | The read address at level k is not 4-byte aligned |
| 0x2F | The final address is not aligned to size (4-byte alignment when size is 8) |
| 0x30 + k | The pointer read at level k is 0; stop there |

### 3.3 Items

An item describes the location of one variable, through at most 8 levels of pointers.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | ap: MEM-AP index |
| 1 | 2 | id: chosen by the host, echoed in the response |
| 3 | 1 | size: 1, 2, 4 or 8 |
| 4 | 4 | base: start address |
| 8 | 1 | n: number of dereferences, 0 to 8 |
| 9 | 2xn | deref_off[i], i16, i = 0 to n-1 |
| 9+2n | 2 | final_off, i16 |

An item is 11 + 2n bytes. A WRITE item is followed by `size` bytes of value; a sampling item is followed by `div(2)` and `phase(2)`; a READ item has no extra fields.

The probe computes the final address as follows:

```
cur = base
for i in 0..n-1:
    addr = cur + sign_extend(deref_off[i])      // 32-bit wraparound
    addr not a multiple of 4  -> item status 0x20+i, stop
    p = read32(addr)                             // SWD failure -> 0x10+i, stop
    p == 0                    -> item status 0x30+i, stop
    cur = p
addr = cur + sign_extend(final_off)
addr not a multiple of size (4 when size is 8) -> 0x2F, stop
read or write size bytes                         // SWD failure -> 0x1F
```

With n equal to 0 the item accesses `base + final_off` directly. A base of 0 is legal (address 0 is valid on Cortex-M); only a pointer read back as 0 counts as null. Array indexing and linked-list traversal are expanded by the host into multiple items.

### 3.4 Commands

#### 0x80 INFO

Request: `80`

The response is a fixed 26 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | 0x80 |
| 1 | 1 | Overall status |
| 2 | 1 | Protocol version, currently 1 |
| 3 | 4 | Feature bits: bit0 READ, bit1 WRITE, bit2 sampling, bit3 8-byte access, bit4 Wi-Fi configuration (0x86); 0x1F for this firmware |
| 7 | 2 | Maximum items per request or subscription: 32 |
| 9 | 1 | Maximum dereference depth: 8 |
| 10 | 4 | Ring buffer size in bytes: 16384 |
| 14 | 8 | Current probe time, in µs |
| 22 | 4 | Calibrated duration of one SWD access (write TAR plus read one AP register), in µs |

The value at offset 22 is recalibrated at each SUB_SET using the current SWD clock: the probe writes TAR and reads CSW 16 times in a row and takes the average. This matches the number of SWD transactions of a single-word read (n equal to 0) and does not touch target memory. It is 0 until the first SUB_SET after power-up.

#### 0x81 READ

Request: `81 count(1) item...`, count from 1 to 32.

Response: `81 status count(1)`, then for each item `id(2) item_status(1) value(size)`. The value of a failed item is all zeros.

#### 0x82 WRITE

Request: `82 count(1) (item value)...`, where value is `size` bytes in target byte order.

Response: `82 status count(1)`, then for each item `id(2) item_status(1)`.

Items are executed in order; writes already done are not rolled back, and a failing item does not affect later ones. An 8-byte write is two 32-bit writes and is not atomic.

#### 0x83 SUB_SET

Request: `83 period_us(4) flags(1) count(2) (item div(2) phase(2))...`

- period_us: base period.
- flags: with bit0 equal to 0 the oldest frame is overwritten when the buffer is full; with 1 new frames are dropped. The other bits must be 0.
- count: 1 to 32.

Response: `83 status max_frame_bytes(2) est_frame_us(4)`

- max_frame_bytes: bytes in one frame when all items are due.
- est_frame_us: estimated duration of one frame.

SUB_SET can only be sent while sampling is stopped; otherwise it returns `83 02`. Before `DAP_Connect` it returns `83 04`.

After the parameter checks pass, the probe measures at the current SWD clock: it first calibrates the single-access time at INFO offset 22, reads the CSW of each AP, then trial-reads all items twice in the order used by sampling (the first pass warms the cache) and records the duration of the second pass as T (excluding the CSW reads). The trial reads use the same addresses as sampling and have the same side effects.

```
est_frame_us = 10 + 1.6 x T        (integer arithmetic: 10 + T x 8 / 5)
```

The factor 1.6 comes from measurement: during sampling the DAP thread on the same core also handles SUB_READ and preempts the sampling task. With 8 items and T of about 265 µs, the actual time per frame is about 1.5 times T.

When est_frame_us is larger than period_us, 0x03 is returned. max_frame_bytes and est_frame_us are still filled in so the host can pick a feasible period. The configuration does not take effect; the previous configuration and buffer are unchanged. On success the buffer and counters are cleared.

The estimate only rejects configurations that clearly cannot keep up; the actual situation is given by the skipped tick count returned by SUB_READ.

#### 0x84 SUB_CTRL

Request: `84 op(1)`; op 0 stops, 1 starts, 2 clears the buffer and counters.

Response: `84 status`

- Start: tick index starts from 0, counters are zeroed, and the CSW of each AP is read once at this point. Returns 0x02 if already running or no successful SUB_SET exists; returns 0x04 without `DAP_Connect` or when reading CSW fails.
- Stop: returns 0x00 even when not running. Frames in the buffer can still be read with SUB_READ.

#### 0x85 SUB_READ

Request: `85 max_bytes(2)`

The response has a 24-byte header followed by frames:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | 0x85 |
| 1 | 1 | Overall status |
| 2 | 8 | Current probe time, in µs |
| 10 | 4 | Frame bytes still unread in the buffer |
| 14 | 4 | Cumulative frames dropped by the buffer |
| 18 | 4 | Cumulative skipped ticks |
| 22 | 2 | Number of frames returned |
| 24 | ... | Frames |

Only whole frames are returned; their total length does not exceed `min(max_bytes, 1024 - 24)`. The frame count is 0 when the buffer is empty. Without any successful SUB_SET, 0x02 is returned. Both counters are cleared on a successful SUB_SET, on start, and on op 2.

### 3.5 Sampling

#### Period and divider

- Tick index k starts from 0. The target time of tick k is start time + k x period_us, so drift does not accumulate.
- An item is sampled on ticks where `(k + phase) % div == 0`. A div of 1 samples on every tick; phase staggers slow items. The items due in one tick are read in SUB_SET order.
- A tick with no due item produces no frame.
- When the next tick is more than 3 ms away the sampling task sleeps; otherwise it busy-waits on the core that runs SWD to keep tick timing accurate.

#### Frame format

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Tick index k |
| 4 | 6 | Timestamp, µs, taken before reading for this tick starts |
| 10 | ceil(m/8) | Status bitmap, m being the number of due items; the i-th due item maps to bit i (LSB first), 1 means failure |
| ... | ... | Values of the due items in due order, each `size` bytes, all zeros on failure |

The due items are derived from k and each item's div and phase; the frame does not list them again.

- The timestamp comes from `esp_timer_get_time`, the probe's monotonic microsecond time, 48 bits. The time axis is not reset when sampling starts; the tick index restarts from 0. After a probe reboot the time axis restarts from 0.
- Items within a frame are read one after another, so the variables of one frame differ in time and are not values at a single instant.
- To align with the host clock: record the local time before and after an INFO or SUB_READ request, pair the midpoint with the probe time in the response, and take the difference as the offset. The error is about half a round trip. Every SUB_READ can update the offset.

#### SWD cannot keep up

When a tick takes longer than the period, the probe does not catch up; it jumps to the next tick that has not yet passed. The tick index then has a gap, and the number of missing ticks accumulates in the skipped tick count. The probe does not lower the rate or drop items on its own; the host adjusts according to the counters.

#### Reading too slowly

Frames are first written to a 16384-byte ring buffer (each frame takes another 2 bytes for its length). When the buffer is full, the oldest frame is overwritten or the new frame is dropped according to flags.bit0; the number of frames overwritten or dropped accumulates in the dropped frame count, and gaps in the tick index also show which ticks were lost. When the unread byte count is not 0, the host should read again immediately.

Bytes per tick are 10 + bitmap + the sizes of the due items; averaging by div and comparing with the real link speed shows whether the bandwidth suffices.

| Counter | Cause | Remedy |
|---|---|---|
| Skipped ticks | SWD cannot keep up | Increase the period, increase div, reduce items |
| Dropped frames | Reading too slowly or link bandwidth too low | Read more often, increase div, reduce items |

### 3.6 SWD access

- DP SELECT: `ap << 24` is written when accessing a MEM-AP. CSW, TAR and DRW are all in bank 0.
- CSW: the CSW of an AP is read once the first time a READ or WRITE command uses that AP, during the SUB_SET trial reads, and when sampling starts. When accessing, `(old & ~0x37) | Size` is written, which clears Size (bits 2:0) and AddrInc (bits 5:4), keeps Prot, HNONSEC and the other bits, sets the access width and disables auto-increment. It is not re-read within the same command or the same sampling run.
- Reading a word: write TAR, read DRW, then read DP RDBUFF for the result. Items are accessed one by one in order.
- Writing a word: write TAR, write DRW, then read DP CTRL/STAT and check STICKYORUN, STICKYERR and WDATAERR.
- 1- and 2-byte accesses: CSW Size is set to 8 or 16 bits; the value sits in DRW on the byte lane selected by the low address bits, and the probe places it in the low bytes of value.
- 8 bytes: two 32-bit accesses, low address first, not atomic; a running target can yield an inconsistent value. Aligned accesses of 4 bytes or fewer are a single access.
- WAIT acknowledge: the retry count set by `DAP_TransferConfigure` applies; exceeding it counts as failure.
- After any FAULT, timeout or sticky error, DP ABORT = `0x0000001E` (STKCMPCLR, STKERRCLR, WDERRCLR, ORUNERRCLR) is written before the next item continues.
- Memory is accessed through the MEM-AP; the core does not need to be halted.

### 3.7 0x86 Wi-Fi configuration

Query, save and clear the Wi-Fi credentials through DAP itself (USB, serial or TCP), without a separate serial terminal. Supported only when INFO feature bit 4 is 1. The credentials are stored in NVS (namespace `rmlink`, key `wifi`), the same data that the serial console command `wifi set` saves.

The command does not touch the target and does not require `DAP_Connect`; it also works during sampling. A request is `86 op(1) parameters`; byte 1 of the response echoes `86` and byte 2 is the status.

| Status | Meaning |
|---|---|
| 0x00 | Success |
| 0x01 | Invalid argument: undefined op, request length does not match the op, SSID empty or longer than 32 bytes, password non-empty and not between 8 and 63 bytes (WPA password length) |
| 0x03 | Insufficient resources: NVS read or write failed, or the response buffer cannot hold the status response |

#### op 0 query

Request: `86 00` (the length must be 2 bytes)

Successful response:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | 0x86 |
| 1 | 1 | Status 0x00 |
| 2 | 1 | Flags: bit0 Wi-Fi started (it starts only when credentials are saved), bit1 IP obtained |
| 3 | 4 | IPv4 address in network byte order (`C0 A8 00 6C` is 192.168.0.108); 0 when not connected |
| 7 | 1 | Signal strength RSSI, i8, in dBm; 0 when not connected |
| 8 | 1 | SSID length n |
| 9 | n | Saved SSID; n is 0 when none is saved; the password is not returned |

On failure the response is just `86` and the status.

#### op 1 save

Request: `86 01 ssid_len(1) ssid(ssid_len) pw_len(1) password(pw_len)`

The total request length must equal `4 + ssid_len + pw_len`. For an open network pw_len is 0.

Response: `86 status`. With status 0x00 the credentials have been written to NVS, and about 0.5 seconds after the response is sent the probe restarts and connects to the access point with the new credentials. During the restart the USB device re-enumerates and serial and TCP connections are lost, so the host has to reconnect.

#### op 2 clear

Request: `86 02` (the length must be 2 bytes)

Response: `86 status`. With status 0x00 the credentials have been cleared, and about 0.5 seconds after the response is sent the probe restarts; Wi-Fi does not start after the restart.

### 3.8 Byte examples

The values below are examples only.

#### INFO

Request: `80`

Response: `80 00 01 1F000000 2000 08 00400000 40420F0000000000 32000000`

Meaning: version 1, feature bits 0x1F, at most 32 items, depth 8, buffer 16384 bytes, probe time 1 000 000 µs, single access 50 µs.

#### READ, one item

Read `*(uint32_t*)(*(uint32_t*)0x20000008 + 4)`:

Item: `00 0100 04 00000020 01 0800 0400` (ap 0, id 1, size 4, base 0x20000000, n 1, deref_off 8, final_off 4)

Request: `81 01 00 0100 04 00000020 01 0800 0400`

Response (the pointer reads 0x20001000, the value there is 0x12345678): `81 00 01 0100 00 78563412`

Response when the pointer is 0: `81 00 01 0100 30 00000000`

#### WRITE, one item

Write the 16-bit value 0x1234 to 0x20000010:

Item: `00 0200 02 10000020 00 0000`, followed by the value `3412`

Request: `82 01 00 0200 02 10000020 00 0000 3412`

Response: `82 00 01 0200 00`

#### SUB_SET, two items

Period 1000 µs, flags 0, 2 items:

- Item 1: id 0x10, size 4, base 0x20000100, n 0, div 1, phase 0: `00 1000 04 00010020 00 0000 0100 0000`
- Item 2: id 0x11, size 2, base 0x20000104, n 0, div 10, phase 3: `00 1100 02 04010020 00 0000 0A00 0300`

Request: `83 E8030000 00 0200`, followed by item 1 and item 2

Response: `83 00 1100 78000000` (at most 17 bytes per frame, estimated frame time 120 µs)

Item 2 is due at k = 7, 17, 27, ... (`(k + 3) % 10 == 0`).

With the same two items and a period of 100 µs the request is rejected: `83 03 1100 78000000`

Before `DAP_Connect`: `83 04`

#### SUB_CTRL start

Request: `84 01`, response: `84 00`

#### SUB_READ

Request: `85 0004` (at most 1024 bytes)

The response holds two frames: at k = 6 only item 1 is due, at k = 7 both items are due.

```
85 00                      command, overall status
80610F0000000000           current probe time 1 008 000 µs
00000000                   0 bytes unread
00000000                   0 frames dropped in total
00000000                   0 ticks skipped in total
0200                       2 frames
06000000 B0590F000000 00 07000000              k=6, timestamp 1 006 000 µs, bitmap 00, value of item 1 is 7
07000000 985D0F000000 00 08000000 3412         k=7, timestamp 1 007 000 µs, bitmap 00, item 1 value 8, item 2 value 0x1234
```

#### Stop

Request: `84 00`, response: `84 00`

#### 0x86 query

Request: `86 00`

Response (connected to access point `lab`, IP 192.168.0.108, signal -52 dBm): `86 00 03 C0A8006C CC 03 6C6162`

With no credentials saved: `86 00 00 00000000 00 00`

#### 0x86 save

Save SSID `lab` and password `12345678`:

Request: `86 01 03 6C6162 08 3132333435363738`

Response: `86 00`; the probe restarts about 0.5 seconds later.

#### 0x86 clear

Request: `86 02`, response: `86 00`; the probe restarts about 0.5 seconds later.

#### Standard command during sampling

Request: `05 ...` (`DAP_Transfer`), response: `FF`

## 4. Probe identity and discovery

### 4.1 DAP_Info strings

The firmware returns these strings in `DAP_Info`. The USB device descriptor of the ESP32-S3 uses the same vendor, product and serial number.

| Info ID | Meaning | Content |
|---|---|---|
| 0x01 | Vendor | `XRobot` |
| 0x02 | Product | ESP32-S3: `XR-DAP-S3`; ESP32-C3: `XR-DAP-C3` |
| 0x03 | Serial number | `XRobot-XRDAP-` followed by the factory MAC (6 bytes from eFuse) as 12 upper-case hexadecimal digits, e.g. `XRobot-XRDAP-E8069093EF1C` |
| 0x04 | CMSIS-DAP protocol version | `2.0.0` |
| 0x05, 0x06 | Target device vendor, name | Not provided (empty) |
| 0x07 | Board vendor | `XRobot` |
| 0x08 | Board name | Same as the product |
| 0x09 | Firmware version | Currently `0.3.0` |

The serial number is the same over USB, serial and TCP, which lets host tools recognise one probe across the three links.

### 4.2 mDNS advertisement

Once the probe has an IP address it advertises itself on the LAN:

| Item | Content |
|---|---|
| Host name | `xrdap-` followed by the last 3 MAC bytes in lower-case hexadecimal, e.g. `xrdap-93ef1c.local` |
| Service | `_xrdap._tcp`, on the TCP DAP port 3241 |
| Instance name | The serial number, e.g. `XRobot-XRDAP-E8069093EF1C` |
| TXT `serial` | The serial number |
| TXT `chip` | `esp32s3` or `esp32c3` |
| TXT `fw` | Firmware version, e.g. `0.3.0` |

OpenOCD accepts the host name directly in `cmsis-dap tcp host`. Host tools browse the `_xrdap._tcp` service to list the probes on the LAN and match them with the same probe on USB or serial through the TXT `serial`. An mDNS startup failure is only logged and does not affect the TCP service.

## 5. The ESP32-C3 USB port

The USB port of the ESP32-C3 is USB Serial/JTAG, which appears on the computer as one CDC serial port. The DAP link (the serial frames of section 1) and the Wi-Fi command line share this port without any mode switch. For each chunk of received data the firmware (`firmware/common/serial/serial_mux.hpp`) does the following:

1. The chunk is first given to the DAP link. The link finds the frame header `0x5A` and checks the CRC itself; bytes that are not part of a frame are discarded.
2. If the chunk executed at least one DAP frame, or a DAP frame was executed within the last 2 seconds, the data is not given to the command line.
3. Any other data goes to the command line byte by byte. The command line produces output only after a line typed by a person ends with Enter, and a line containing control characters is dropped as a whole.

As a result DAP frames and command-line output never mix: the debug tool always receives DAP response frames, and commands such as `help` and `wifi set` keep working in a terminal. The terminal must not change DTR/RTS, otherwise USB Serial/JTAG may reset the chip. The serial link of the ESP32-S3 is on UART0 and does not use this separation; its command line is on the USB CDC virtual serial port.

## 6. USB/IP virtual device

The probe serves USB/IP on TCP port 3240 (protocol in the Linux kernel document `usb/usbip_protocol.rst`, fields big-endian). The implementation is in `firmware/common/net/usbip_device.hpp`, and the `usbip` module of the host xrdap library is the same device model.

| Operation | Handling |
|---|---|
| `OP_REQ_DEVLIST` (0x8005) | Replies with one device, bus ID `1-1`, then closes the connection |
| `OP_REQ_IMPORT` (0x8003) | Replies with the device information when the bus ID is `1-1`, followed by URBs; any other bus ID gets status 1 |
| `USBIP_CMD_SUBMIT` (1) | Endpoint 0: standard requests and descriptors; endpoint 1: Bulk OUT executes a DAP command, Bulk IN fetches the response |
| `USBIP_CMD_UNLINK` (2) | Cancels an IN that is still waiting for data; replies -ECONNRESET when found, otherwise 0 |

Device:

| Item | Value |
|---|---|
| VID:PID, bcdDevice | 1D50:6199, 0x0200 (the real USB device of the S3 uses 0x0100; Windows caches descriptors by these three items) |
| bcdUSB, speed | 2.10 (with BOS), high speed |
| Strings | 1 `XRobot`, 2 `XR-DAP-S3 CMSIS-DAP` / `XR-DAP-C3 CMSIS-DAP`, 3 serial number (same as DAP_Info), 4 `CMSIS-DAP v2` |
| Interface | One, vendor class 0xFF, interface name is string 4 |
| Endpoints | Bulk OUT 0x01, Bulk IN 0x81, 512 bytes |
| BOS | MS OS 2.0 platform capability, vendor request code 0x20; descriptor set of 162 bytes: compatible ID `WINUSB`, `DeviceInterfaceGUIDs` = `{CDB3B5AD-293B-4663-AA36-1AAE46463776}` |

DAP command pairing: every Bulk OUT is executed immediately and its response is queued (depth 4, equal to the packet count reported by DAP_Info) and handed to the waiting Bulk IN in order of arrival; an IN that arrives first waits in the queue for a response. A command without a response (DAP_TransferAbort) does not take an IN. When the queue is full the URB is answered with -EPIPE.

The replies caused by one command (the reply to the OUT and the response paired with an IN) are combined into a single `send()`. When sent in two parts, the second part waits for the peer's delayed ACK, which adds about 40 ms to every round trip.
