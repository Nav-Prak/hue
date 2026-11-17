# tools/fuzz_wsl.ps1
#
# Starts, resumes or reports the fuzz campaign in WSL from Windows.
#
#   powershell -File tools\fuzz_wsl.ps1            start or resume, detached
#   powershell -File tools\fuzz_wsl.ps1 -Status    print each target's progress
#   powershell -File tools\fuzz_wsl.ps1 -Update    git pull in the WSL clone first
#
# The WSL clone is ~/hue-fuzz and the output ~/hue-fuzz-out. The runner is
# created through WMI so it does not belong to the calling window or tool and
# keeps running when that closes. Starting it twice is harmless: the second
# runner finds the output locked and exits.
param(
    [switch]$Status,
    [switch]$Update,
    [string]$Distribution = "Ubuntu"
)
$ErrorActionPreference = "Stop"

if ($Status) {
    wsl.exe -d $Distribution -- bash -lc "python3 ~/hue-fuzz/tools/fuzz_campaign.py --output ~/hue-fuzz-out --status; tail -n 3 ~/hue-fuzz-out.log"
    exit $LASTEXITCODE
}
if ($Update) {
    wsl.exe -d $Distribution -- bash -lc "git -C ~/hue-fuzz pull --ff-only"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$command = "wsl.exe -d $Distribution -- bash -lc `"exec ~/hue-fuzz/tools/fuzz_local.sh`""
$result = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $command }
if ($result.ReturnValue -ne 0) {
    Write-Error "could not start the runner (Win32_Process.Create returned $($result.ReturnValue))"
}
"runner started (Windows process $($result.ProcessId)); check it with: powershell -File tools\fuzz_wsl.ps1 -Status"
