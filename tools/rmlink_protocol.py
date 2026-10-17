"""Shared wire bounds; capacities are negotiated, never inferred from USB speed."""
PACKET_SIZES = (64, 128, 256, 512, 1024)
MAX_PACKET_SIZE = 1024
PACKET_COUNT = 8


def packet_size_from_info(response):
    if len(response) != 4 or response[:2] != b'\x00\x02':
        raise ValueError('Invalid DAP_Info packet-size response')
    size = int.from_bytes(response[2:4], 'little')
    if size not in PACKET_SIZES:
        raise ValueError('Unsupported RMLink packet size')
    return size


def without_usb_padding(packet):
    """Remove only proven zero padding; preserve data zeros and unknown formats.

    This is a length decoder, not target validation. Firmware still validates
    flags, response budget and session ownership. Callers check the original
    packet against the negotiated capacity before using this helper.
    """
    if not packet: return packet
    cmd = packet[0]
    length = {0:2, 1:3, 2:2, 3:1, 4:6, 7:1, 8:6, 9:3, 10:1,
              0x10:7, 0x11:5, 0x13:2}.get(cmd)
    if cmd == 5 and len(packet) >= 3:
        length = 3
        for _ in range(packet[2]):
            if length >= len(packet): return packet
            flags = packet[length]
            if flags & 0xc0: return packet
            length += 1 + (4 if not flags & 2 or flags & 0x10 else 0)
            if length > len(packet): return packet
    elif cmd == 6 and len(packet) >= 5:
        if packet[4] & 0xf0: return packet
        count = int.from_bytes(packet[2:4], 'little')
        length = 5 if packet[4] & 2 else 5 + count*4
    elif cmd == 0x12 and len(packet) >= 2:
        length = 2 + ((packet[1] or 256) + 7)//8
    elif cmd == 0x1d and len(packet) >= 2:
        length = 2
        for _ in range(packet[1]):
            if length >= len(packet): return packet
            flags = packet[length]
            if flags & 0x40: return packet
            length += 1 + (0 if flags & 0x80 else ((flags & 63 or 64) + 7)//8)
            if length > len(packet): return packet
    elif cmd == 0x80 and len(packet) >= 2:
        length = {0:2, 4:2, 6:2, 7:6, 9:2}.get(packet[1])
    if length is None or length > len(packet) or any(packet[length:]): return packet
    return packet[:length]
