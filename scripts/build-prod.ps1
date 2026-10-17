param([Parameter(ValueFromRemainingArguments=$true)][string[]]$IdfArgs)
$idf='C:\Users\a2592\esp\esp-idf-v5.5.2'; $tools='C:\Users\a2592\.espressif'
$pyd="$tools\python_env\idf5.5_py3.12_env"; $python="$pyd\Scripts\python.exe"
$env:IDF_PATH=$idf; $env:IDF_TOOLS_PATH=$tools; $env:IDF_PYTHON_ENV_PATH=$pyd
foreach($l in (& $python "$idf\tools\idf_tools.py" export --format key-value)){ $p=$l -split '=',2; if($p.Count -ne 2){continue}; $v=$p[1]; if($p[0] -eq 'PATH'){$v=$v.Replace('%PATH%',$env:PATH).Replace('$PATH',$env:PATH)}; [Environment]::SetEnvironmentVariable($p[0],$v,'Process') }
$app='D:\Codex\tmp\xrdap-s3\app-c'
Set-Location $app
& $python "$idf\tools\idf.py" -C $app -B D:\Codex\tmp\xrdap-s3\build-prod -D SDKCONFIG=D:/Codex/tmp/xrdap-s3/sdkconfig.prod -D CMAKE_CXX_FLAGS="-fira-region=one" @IdfArgs 2>&1

