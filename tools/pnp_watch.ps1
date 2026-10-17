$prev = ''
$end = (Get-Date).AddSeconds(100)
while ((Get-Date) -lt $end) {
  $e = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPDeviceID -match 'VID_303A&PID_(4A01|1002)|USB\\VID_0000' -and $_.Present -ne $false }
  $s = ($e | ForEach-Object { "$($_.Name)[$($_.ConfigManagerErrorCode)]" }) -join ' ; '
  if ($s -ne $prev) { "{0:HH:mm:ss.f}  {1}" -f (Get-Date), $(if ($s) { $s } else { '(no DAP device)' }); $prev = $s }
  Start-Sleep -Milliseconds 300
}

