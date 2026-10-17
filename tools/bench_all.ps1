param([string]$Label = 'run')
# One compact benchmark pass: USB block read / OUT / block write, OpenOCD dump over USB, TCP block read/write (Pi).
$ErrorActionPreference = 'Continue'
Set-Location D:\Codex\tmp\rml
"=== $Label"
python warmup.py | Out-Null
$o = python xrblock_pipe.py 12000000 2>&1 | Select-String "in flight"
foreach ($l in $o) { if ($l -match '(\d+) commands in flight:\s+([\d.]+) KiB/s') { "USB block read 12 MHz, $($Matches[1]) in flight: $($Matches[2]) KiB/s" } }
$o = python xrout2_pipe.py 2>&1 | Select-String "depth 4"
"USB pure OUT depth 4: " + (($o -replace '\s+', ' ') -join '')
$o = python xrwonly.py 2>&1 | Select-String "depth 4"
foreach ($l in $o) { "USB " + ($l -replace '\s+', ' ') }
$o = python D:\Codex\tmp\xrdap-s3\hi_test.py 12000 1 2>&1 | Select-String "dump"
"OpenOCD (USB) 12 MHz: " + ($o -join ' ')
Start-Sleep 2
foreach ($hz in 8000000, 12000000) {
  $r = ssh -o BatchMode=yes lab3b "cd /tmp; python3 tcp_block.py 10.43.0.168 $hz 2>&1 | grep -E 'depth 4'"
  foreach ($l in $r) { "TCP " + ($l -replace '\s+', ' ') }
  Start-Sleep 3
}
