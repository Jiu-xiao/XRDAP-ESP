"""SWD scattered-read benchmark over CMSIS-DAP TCP. Runs on the lab Pi.

usage: swdbench.py HOST CLOCK_HZ [rounds]
Reads static Flash words, checks scattered results against single-word reads,
then times 8 pipelined max-size scattered packets.
"""
import struct
import sys
import time

sys.path.insert(0, '/tmp')
import swdtcp as T


def main():
    host, clock = sys.argv[1], int(sys.argv[2])
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    c = T.Tcp(host)
    d = T.Dap(c)
    pk = struct.unpack('<H', c.exchange(b'\x00\xff')[2:4])[0]
    words = (pk - 3) // 6
    words = min(words, 84)
    d.connect(clock)
    d.bringup()
    addrs = [0x08000000 + 64 * i for i in range(words)]
    truth = [d.read_word(a) for a in addrs]
    pkt = d.scatter_pkt(addrs)
    n, ack, w = d.parse(c.exchange(pkt))
    ok = (ack == 1 and n == 2 * words and w[:words - 1] == truth[:words - 1])
    # a scattered packet returns words-1 values in-line plus the final RDBUFF value
    ok_all = (len(w) == words and w == truth)
    best = 0.0
    errs = 0
    for _ in range(3):
        t0 = time.perf_counter()
        done = 0
        for _r in range(rounds):
            res = c.send_many([pkt] * 8)
            for r in res:
                n, ack, w = d.parse(r)
                if ack != 1 or w != truth:
                    errs += 1
                done += words
        dt = time.perf_counter() - t0
        best = max(best, done / dt)
    print('packet=%d words/pkt=%d clock_req=%d  match_first=%s  errors=%d  best=%.0f words/s' % (
        pk, words, clock, ok_all, errs, best))
    c.exchange(b'\x03')
    c.close()


if __name__ == '__main__':
    main()
