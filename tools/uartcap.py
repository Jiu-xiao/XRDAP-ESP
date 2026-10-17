import serial, time, sys
s = serial.Serial(); s.port = 'COM108'; s.baudrate = 115200; s.dtr = False; s.rts = False; s.timeout = 0.2
s.open(); s.dtr = False; s.rts = False
end = time.time() + float(sys.argv[1]); f = open(sys.argv[2], 'wb')
while time.time() < end:
    d = s.read(4096)
    if d: f.write(d); f.flush()
