#!/bin/bash
# usage: ocd_tcp_test.sh SPEED_KHZ N [quirk]   (runs on the Pi)
O=/tmp/ocd2/openocd-esp32/bin/openocd
S=/tmp/ocd2/openocd-esp32/share/openocd/scripts
SPEED=$1; N=$2; QUIRK=$3
ok=0
for i in $(seq 0 $((N-1))); do
  img=/tmp/ocd2/rnd1.bin; [ $((i%2)) -eq 1 ] && img=/tmp/ocd2/rnd2.bin
  s=$(date +%s%N)
  out=$($O -s $S -c "adapter driver cmsis-dap" -c "cmsis-dap backend tcp" -c "cmsis-dap tcp host 10.43.0.168" -c "cmsis-dap tcp port 3241" ${QUIRK:+-c "cmsis-dap quirk enable"} -c "transport select swd" -f target/stm32g4x.cfg -c "adapter speed $SPEED" -c "init" -c "catch {halt}" -c "mmw 0xE0042008 0x1800 0" -c "program $img 0x08000000 verify" -c "shutdown" 2>&1)
  e=$(date +%s%N)
  if echo "$out" | grep -q "Verified OK"; then ok=$((ok+1)); r=OK; else r="FAIL: $(echo "$out" | grep -a "Error" | head -2 | tr '\n' '|' | cut -c1-160)"; fi
  printf "run %d %s: %s (%d ms)
" $i "$(basename $img)" "$r" $(( (e - s) / 1000000 ))
done
echo "program+verify $SPEED kHz: $ok/$N"
