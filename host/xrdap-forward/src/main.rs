//! XR-DAP 串口转 TCP：让 OpenOCD 通过 `cmsis-dap backend tcp` 使用串口连接的探针。
//!
//! 串口一侧：libxr Topic 帧，请求 topic 为 `dap_req`，响应为 `dap_resp`，
//! 载荷是 `长度(u16 小端) + DAP 包`。
//! TCP 一侧：`"DAP\0" + 长度(u16 小端) + 类型(1 请求 2 响应) + 保留(1) + DAP 包`。
//!
//! 请求和响应原样转发，不解析 DAP 内容。同一时间只服务一个 TCP 客户端。

use std::io::{ErrorKind, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::time::{Duration, Instant};

use clap::Parser;
use serial2::SerialPort;

const TCP_MAGIC: &[u8; 4] = b"DAP\0";
const REQ_TOPIC: &[u8] = b"dap_req";
const RESP_TOPIC: &[u8] = b"dap_resp";

const PACKET_PREFIX: u8 = 0x5A;
const PACKET_VERSION: u8 = 1;
const HEADER_LEN: usize = 16;
#[cfg(test)]
const FRAME_OVERHEAD: usize = 17;
const MAX_TOPIC_PAYLOAD: usize = 2 + 4096;
/// SUB_CTRL 停止采样
const STOP_SAMPLING: [u8; 2] = [0x84, 0x00];

#[derive(Parser, Debug)]
#[command(name = "xrdap-forward", about = "XR-DAP 串口转 TCP，供 OpenOCD 的 cmsis-dap tcp 后端使用")]
struct Args {
    /// 探针的串口，例如 COM35 或 /dev/ttyUSB0
    #[arg(long)]
    serial: String,
    /// 串口波特率，要和探针固件一致
    #[arg(long, default_value_t = 4_000_000)]
    baud: u32,
    /// TCP 监听地址
    #[arg(long, default_value = "127.0.0.1:3241")]
    listen: String,
    /// 一个请求等待响应的超时（毫秒）
    #[arg(long, default_value_t = 2000)]
    timeout_ms: u64,
    /// 打开串口后不改 DTR/RTS（默认置为 DTR=1、RTS=0，即 ESP32 开发板自动下载电路的运行状态）
    #[arg(long)]
    keep_lines: bool,
    /// 打印每个转发的请求和响应
    #[arg(long)]
    verbose: bool,
}

/// libxr 的 CRC8（多项式 0x8C 反射，初值 0xFF）。
fn crc8(data: &[u8]) -> u8 {
    let mut crc = 0xFFu8;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0x8C } else { crc >> 1 };
        }
    }
    crc
}

/// libxr 的 CRC32（反射，初值 0xFFFFFFFF，最后不取反），用来算 topic 名的键。
fn crc32(data: &[u8]) -> u32 {
    let mut crc = 0xFFFF_FFFFu32;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0xEDB8_8320 } else { crc >> 1 };
        }
    }
    crc
}

/// 组一个 Topic 帧：16 字节头 + 载荷 + 1 字节尾 CRC8。
fn build_frame(topic_crc: u32, payload: &[u8], timestamp_us: u64) -> Vec<u8> {
    let mut frame = vec![0u8; HEADER_LEN + payload.len() + 1];
    frame[0] = PACKET_PREFIX;
    frame[1..4].copy_from_slice(&(payload.len() as u32).to_le_bytes()[..3]);
    frame[4..8].copy_from_slice(&topic_crc.to_le_bytes());
    frame[8..14].copy_from_slice(&timestamp_us.to_le_bytes()[..6]);
    frame[14] = PACKET_VERSION;
    frame[15] = crc8(&frame[..15]);
    frame[HEADER_LEN..HEADER_LEN + payload.len()].copy_from_slice(payload);
    frame[HEADER_LEN + payload.len()] = crc8(&frame[..HEADER_LEN + payload.len()]);
    frame
}

/// 从字节流里切出 Topic 帧；头或尾 CRC 错时丢一个字节重新找前缀。
struct FrameParser {
    buf: Vec<u8>,
    resp_crc: u32,
}

impl FrameParser {
    fn new(resp_crc: u32) -> Self {
        Self { buf: Vec::new(), resp_crc }
    }

    fn feed(&mut self, data: &[u8]) -> Vec<Vec<u8>> {
        self.buf.extend_from_slice(data);
        let mut out = Vec::new();
        loop {
            let Some(start) = self.buf.iter().position(|&b| b == PACKET_PREFIX) else {
                self.buf.clear();
                break;
            };
            if start > 0 {
                self.buf.drain(..start);
            }
            if self.buf.len() < HEADER_LEN {
                break;
            }
            let data_len = self.buf[1] as usize
                | (self.buf[2] as usize) << 8
                | (self.buf[3] as usize) << 16;
            if self.buf[14] != PACKET_VERSION
                || data_len > MAX_TOPIC_PAYLOAD
                || crc8(&self.buf[..HEADER_LEN - 1]) != self.buf[HEADER_LEN - 1]
            {
                self.buf.drain(..1);
                continue;
            }
            let total = HEADER_LEN + data_len + 1;
            if self.buf.len() < total {
                break;
            }
            if crc8(&self.buf[..HEADER_LEN + data_len]) != self.buf[HEADER_LEN + data_len] {
                self.buf.drain(..1);
                continue;
            }
            let topic_crc = u32::from_le_bytes(self.buf[4..8].try_into().unwrap());
            let payload = self.buf[HEADER_LEN..HEADER_LEN + data_len].to_vec();
            self.buf.drain(..total);
            if topic_crc == self.resp_crc {
                out.push(payload);
            }
        }
        if self.buf.len() > 65536 {
            self.buf.clear();
        }
        out
    }
}

/// 串口上的 Topic 载荷是 `u16 长度 + DAP 包`。
fn serial_payload(request: &[u8]) -> Vec<u8> {
    let mut payload = Vec::with_capacity(2 + request.len());
    payload.extend_from_slice(&(request.len() as u16).to_le_bytes());
    payload.extend_from_slice(request);
    payload
}

struct Link {
    port: SerialPort,
    rx: Receiver<Vec<u8>>,
    req_crc: u32,
    timeout: Duration,
}

impl Link {
    /// 发一个请求，等它的响应。之前残留的响应（比如超时后才到的）先丢掉。
    fn transaction(&mut self, request: &[u8]) -> Result<Vec<u8>, String> {
        while self.rx.try_recv().is_ok() {}
        let frame = build_frame(self.req_crc, &serial_payload(request), 0);
        self.port
            .write_all(&frame)
            .map_err(|e| format!("串口写失败：{e}"))?;
        self.port.flush().map_err(|e| format!("串口写失败：{e}"))?;

        let deadline = Instant::now() + self.timeout;
        loop {
            let remaining = deadline.saturating_duration_since(Instant::now());
            if remaining.is_zero() {
                return Err("等待串口响应超时".into());
            }
            match self.rx.recv_timeout(remaining) {
                Ok(payload) => {
                    if payload.len() < 2 {
                        continue;
                    }
                    let len = u16::from_le_bytes([payload[0], payload[1]]) as usize;
                    if len > payload.len() - 2 {
                        continue;
                    }
                    return Ok(payload[2..2 + len].to_vec());
                }
                Err(RecvTimeoutError::Timeout) => return Err("等待串口响应超时".into()),
                Err(RecvTimeoutError::Disconnected) => return Err("串口读线程已退出".into()),
            }
        }
    }
}

fn reader_thread(port: SerialPort, tx: Sender<Vec<u8>>, resp_crc: u32) {
    let mut parser = FrameParser::new(resp_crc);
    let mut buf = [0u8; 1024];
    loop {
        match port.read(&mut buf) {
            Ok(0) => continue,
            Ok(n) => {
                for payload in parser.feed(&buf[..n]) {
                    if tx.send(payload).is_err() {
                        return;
                    }
                }
            }
            Err(e) if e.kind() == ErrorKind::TimedOut => continue,
            Err(_) => return,
        }
    }
}

fn open_link(args: &Args) -> Result<Link, String> {
    let mut port = SerialPort::open(&args.serial, args.baud)
        .map_err(|e| format!("打不开 {}：{e}", args.serial))?;
    port.set_read_timeout(Duration::from_millis(100))
        .map_err(|e| format!("设置串口超时失败：{e}"))?;
    port.set_write_timeout(Duration::from_millis(1000))
        .map_err(|e| format!("设置串口超时失败：{e}"))?;
    // ESP32 开发板的 USB 转串口芯片用 DTR/RTS 控制 EN 和 BOOT，运行状态是 DTR=1、RTS=0。
    // 打开串口会让探针复位一次，后面的握手会等它启动完。
    if !args.keep_lines {
        let _ = port.set_dtr(true);
        let _ = port.set_rts(false);
    }
    let mut reader = port
        .try_clone()
        .map_err(|e| format!("复制串口句柄失败：{e}"))?;
    reader
        .set_read_timeout(Duration::from_millis(100))
        .map_err(|e| format!("设置串口超时失败：{e}"))?;
    let resp_crc = crc32(RESP_TOPIC);
    let (tx, rx) = mpsc::channel();
    std::thread::spawn(move || reader_thread(reader, tx, resp_crc));
    Ok(Link {
        port,
        rx,
        req_crc: crc32(REQ_TOPIC),
        timeout: Duration::from_millis(args.timeout_ms),
    })
}

/// 反复发 DAP_Info(包长) 直到探针应答：打开串口引起的复位要等探针启动完。
fn handshake(link: &mut Link) -> Result<(), String> {
    let normal = link.timeout;
    link.timeout = Duration::from_millis(200);
    let deadline = Instant::now() + Duration::from_secs(10);
    let result = loop {
        match link.transaction(&[0x00, 0xFF]) {
            Ok(response) => break Ok(response),
            Err(e) if Instant::now() >= deadline => break Err(format!("探针没有应答：{e}")),
            Err(_) => {}
        }
    };
    link.timeout = normal;
    let response = result?;
    if response.len() >= 4 && response[0] == 0x00 && response[1] == 0x02 {
        let size = u16::from_le_bytes([response[2], response[3]]);
        println!("探针已应答，DAP 包长 {size} 字节");
    } else {
        println!("探针已应答：{response:02X?}");
    }
    Ok(())
}

fn handle_client(stream: &mut TcpStream, link: &mut Link, verbose: bool) {
    let peer = stream.peer_addr().map(|a| a.to_string()).unwrap_or_default();
    let mut header = [0u8; 8];
    loop {
        if let Err(e) = stream.read_exact(&mut header) {
            if e.kind() != ErrorKind::UnexpectedEof {
                eprintln!("[{peer}] TCP 读失败：{e}");
            }
            return;
        }
        if &header[..4] != TCP_MAGIC || header[6] != 1 {
            eprintln!("[{peer}] 帧头无效，关闭连接");
            return;
        }
        let len = u16::from_le_bytes([header[4], header[5]]) as usize;
        let mut request = vec![0u8; len];
        if let Err(e) = stream.read_exact(&mut request) {
            eprintln!("[{peer}] TCP 读失败：{e}");
            return;
        }
        if verbose {
            println!("[{peer}] 请求 {} 字节：{:02X?}", request.len(), request);
        }

        // 串口没有应答时回 0xFF（命令不支持），OpenOCD 会报错而不是一直等。
        let response = link.transaction(&request).unwrap_or_else(|e| {
            eprintln!("[{peer}] {e}，回 0xFF");
            vec![0xFF]
        });
        if verbose {
            println!("[{peer}] 响应 {} 字节：{:02X?}", response.len(), response);
        }

        let mut out = Vec::with_capacity(8 + response.len());
        out.extend_from_slice(TCP_MAGIC);
        out.extend_from_slice(&(response.len() as u16).to_le_bytes());
        out.extend_from_slice(&[2, 0]);
        out.extend_from_slice(&response);
        if stream.write_all(&out).is_err() || stream.flush().is_err() {
            return;
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args = Args::parse();
    let mut link = open_link(&args)?;
    println!("串口 {}，{} baud", args.serial, args.baud);
    handshake(&mut link)?;

    let listener = TcpListener::bind(&args.listen)?;
    println!("在 {} 等待 OpenOCD 连接", listener.local_addr()?);
    for connection in listener.incoming() {
        let mut stream = match connection {
            Ok(stream) => stream,
            Err(e) => {
                eprintln!("接受连接失败：{e}");
                continue;
            }
        };
        let _ = stream.set_nodelay(true);
        let peer = stream.peer_addr().map(|a| a.to_string()).unwrap_or_default();
        println!("[{peer}] 已连接");
        handle_client(&mut stream, &mut link, args.verbose);
        // 客户端开过采样的话替它停掉，否则探针会一直独占 SWD，直到串口租期过期。
        println!("[{peer}] 已断开");
        if let Err(e) = link.transaction(&STOP_SAMPLING) {
            eprintln!("停止采样失败：{e}");
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_reference_value() {
        // 反射 CRC-32，最后不取反
        assert_eq!(crc32(b"123456789"), 0x340B_C6D9);
    }

    #[test]
    fn frame_round_trip() {
        let topic = crc32(RESP_TOPIC);
        let payload: Vec<u8> = (0..=255u8).cycle().take(2 + 1024).collect();
        let frame = build_frame(topic, &payload, 1_000_000);
        assert_eq!(frame.len(), FRAME_OVERHEAD + payload.len());
        let mut parser = FrameParser::new(topic);
        assert_eq!(parser.feed(&frame), vec![payload]);
    }

    #[test]
    fn parser_resyncs_after_garbage() {
        let topic = crc32(RESP_TOPIC);
        let payload = vec![0x00, 0x02, 0x80, 0x00];
        let mut stream = vec![0x5A, 0x00]; // 被截断的残帧
        stream.extend_from_slice(&build_frame(topic, &payload, 42));
        let mut parser = FrameParser::new(topic);
        assert_eq!(parser.feed(&stream), vec![payload]);
    }

    #[test]
    fn request_payload_has_length_prefix() {
        let payload = serial_payload(&[0x00, 0xFF]);
        assert_eq!(payload, vec![0x02, 0x00, 0x00, 0xFF]);
    }

    #[test]
    fn parser_skips_other_topic() {
        let other = build_frame(crc32(REQ_TOPIC), &[0x00, 0x01, 0xAA], 0);
        let mut parser = FrameParser::new(crc32(RESP_TOPIC));
        assert!(parser.feed(&other).is_empty());
    }
}
