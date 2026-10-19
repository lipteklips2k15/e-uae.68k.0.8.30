# E-UAE's emulated graphics card, tested the PiStorm way: WinUAE (68040 JIT,
# RTG, OS 3.2) runs our E-UAE build, whose Amiga boots share/guest/sys and
# runs rtgtest on the uaegfx card.
#   .\run.ps1 [-Seconds 420] [-Card old|os32] [-Keep]
# The first run copies a guest system out of the OS 3.2 HDF (C, L, Libs, Devs,
# Fonts, Env-Archive) into share/guest/sys; later runs reuse it.
#   .\run.ps1 -Setup     boots OS 3.2 and leaves it open, E-UAE not started -
#                        for setting the Workbench's ScreenMode (an RTG one: E-UAE
#                        draws the card's picture with cybergraphics) and saving it
#   .\run.ps1 -Workbench [-WbMode 0x50041303 -WbWidth 800 -WbHeight 600 -WbDepth 24]
#                        the emulated Amiga boots to Workbench on that uaegfx mode
#                        (written as its screenmode.prefs) instead of running rtgtest
param([int]$Seconds = 420, [ValidateSet('old', 'os32')][string]$Card = 'old', [switch]$Keep, [switch]$Setup,
      [switch]$Workbench, [uint32]$WbMode = 0x50041303, [int]$WbWidth = 800, [int]$WbHeight = 600, [int]$WbDepth = 24)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$share = "$root\share"
$tree = Split-Path -Parent $root

if (Get-Process winuae64 -ErrorAction SilentlyContinue) { 'WinUAE is running already - close it first'; return }
Copy-Item "$tree\uae" "$share\uae" -Force
if (-not (Test-Path "$share\guest\kick.rom")) { Copy-Item "U:\Amiga OS's\ROMs\A1200.47.111.rom" "$share\guest\kick.rom" }
Remove-Item "$share\done.txt", "$share\run.log", "$share\euae.log", "$share\boot.txt", "$share\guest\sys\guest.log" -ErrorAction SilentlyContinue

$g = 'DIZ:guest/sys'
$lines = @('Echo autorun-start >DIZ:boot.txt', 'FailAt 30', 'Wait 10',
    "IF NOT EXISTS $g/C",
    "  MakeDir $g $g/Prefs $g/S",
    "  Echo copying-guest-system >>DIZ:boot.txt")
foreach ($d in 'C', 'L', 'Libs', 'Devs', 'Fonts') { $lines += "  Copy SYS:$d $g/$d ALL CLONE QUIET" }
$lines += "  Copy SYS:Prefs/Env-Archive $g/Prefs/Env-Archive ALL CLONE QUIET",
    'ENDIF',
    "IF NOT EXISTS $g/Locale",          # Workbench asks for LOCALE: without it
    "  Copy SYS:Locale $g/Locale ALL CLONE QUIET",
    'ENDIF',
    "IF EXISTS $g/Libs/Picasso96/uaegfx.card",
    "  IF NOT EXISTS $g/Libs/Picasso96/uaegfx.card.os32",
    "    Copy $g/Libs/Picasso96/uaegfx.card $g/Libs/Picasso96/uaegfx.card.os32",
    '  ENDIF',
    'ENDIF'
if ($Card -eq 'old') { $lines += "Copy DIZ:guest/p96/Picasso96Install/Libs/Picasso96/uaegfx.card $g/Libs/Picasso96/uaegfx.card" }
else { $lines += "Copy $g/Libs/Picasso96/uaegfx.card.os32 $g/Libs/Picasso96/uaegfx.card" }
if ($Workbench) {
    # ENVARC:Sys/screenmode.prefs: FORM PREF, PRHD (6 bytes), SCRM (16 reserved, DisplayID,
    # Width, Height, Depth, Control = SMF_AUTOSCROLL), big-endian
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter($ms)
    function BE32($v) { $b = [BitConverter]::GetBytes([uint32]$v); [Array]::Reverse($b); $bw.Write($b) }
    function BE16($v) { $b = [BitConverter]::GetBytes([uint16]$v); [Array]::Reverse($b); $bw.Write($b) }
    $bw.Write([Text.Encoding]::ASCII.GetBytes('FORM')); BE32 54; $bw.Write([Text.Encoding]::ASCII.GetBytes('PREF'))
    $bw.Write([Text.Encoding]::ASCII.GetBytes('PRHD')); BE32 6; $bw.Write([byte[]](0, 0, 0, 0, 0, 0))
    $bw.Write([Text.Encoding]::ASCII.GetBytes('SCRM')); BE32 28; $bw.Write((New-Object byte[] 16))
    BE32 $WbMode; BE16 $WbWidth; BE16 $WbHeight; BE16 $WbDepth; BE16 1
    $bw.Flush(); [IO.File]::WriteAllBytes("$share\guest\screenmode.prefs", $ms.ToArray())
    $lines += "Copy DIZ:guest/Startup-Sequence.wb $g/S/Startup-Sequence",
        "MakeDir $g/Prefs/Env-Archive/Sys >NIL:",
        "Copy DIZ:guest/screenmode.prefs $g/Prefs/Env-Archive/Sys/screenmode.prefs"
} else {
    $lines += "Copy DIZ:guest/Startup-Sequence $g/S/Startup-Sequence"
}
$lines += "Copy DIZ:guest/rtgtest $g/C/rtgtest",
    "Copy DIZ:guest/inputmon $g/C/inputmon",
    "List $g/Devs/Monitors $g/Libs/Picasso96 >DIZ:monitors.txt",
    'Echo e-uae-start >>DIZ:boot.txt',
    'SetEnv EUAE_LOG DIZ:euae.log',
    'CD DIZ:', 'Stack 65536',
    'DIZ:uae -f DIZ:guest/euae.uaerc >DIZ:run.log',
    'Echo uae-exited >DIZ:done.txt'
if ($Setup) { $lines = @('Echo autorun-start-setup >DIZ:boot.txt') }
New-Item -ItemType Directory "$share\S" -Force | Out-Null
[IO.File]::WriteAllText("$share\S\autorun", (($lines -join "`n") + "`n"))
# DIZ: boots first (priority 5): hand the system over to the HDF's Workbench:
# partition, start the autorun beside it, then the HDF's own startup
$ss = @('Echo startup-seq-reached >DIZ:boot0.txt',
    'Workbench:C/Assign SYS: Workbench:', 'Workbench:C/Assign C: Workbench:C',
    'Workbench:C/Assign S: Workbench:S', 'Workbench:C/Assign L: Workbench:L',
    'Workbench:C/Assign LIBS: Workbench:Libs', 'Workbench:C/Assign DEVS: Workbench:Devs',
    'Workbench:C/Assign FONTS: Workbench:Fonts',
    'Run >NIL: Execute DIZ:S/autorun', 'Execute Workbench:S/Startup-Sequence')
[IO.File]::WriteAllText("$share\S\Startup-Sequence", (($ss -join "`n") + "`n"))

$p = Start-Process "C:\Program Files\WinUAE\winuae64.exe" -ArgumentList "-f `"$root\euae-test.uae`"" -PassThru
if ($Setup) { 'WinUAE is up for the ScreenMode - close it yourself when done'; return }
$t0 = Get-Date
$guestLog = "$share\guest\sys\guest.log"
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    Start-Sleep -Seconds 5
    if ((Test-Path $guestLog) -and (Select-String $guestLog -Pattern 'rtgtest returned|workbench up' -Quiet)) { Start-Sleep -Seconds 3; break }
    if (Test-Path "$share\done.txt") { break }
}
"elapsed $([int]((Get-Date) - $t0).TotalSeconds) s"
if (-not $Keep) { Stop-Process $p -Force -ErrorAction SilentlyContinue; Start-Sleep -Seconds 3 }
foreach ($f in 'boot.txt', 'monitors.txt', 'run.log', 'done.txt') { if (Test-Path "$share\$f") { "--- $f"; Get-Content "$share\$f" -Tail 40 } }
"--- euae.log (P96, errors)"; if (Test-Path "$share\euae.log") { Select-String "$share\euae.log" -Pattern 'P96|picasso|gfx|card|error|fail|Kickstart' | Select-Object -Last 40 | ForEach-Object Line }
"--- guest.log"; if (Test-Path $guestLog) { Get-Content $guestLog }
