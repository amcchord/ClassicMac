param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class QAInput {
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left,top,right,bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct MI { public int dx,dy; public uint data,flags,time; public UIntPtr extra; }
 [StructLayout(LayoutKind.Sequential)] public struct KI { public ushort vk,scan; public uint flags,time; public UIntPtr extra; }
 [StructLayout(LayoutKind.Explicit)] public struct U { [FieldOffset(0)] public MI mouse; [FieldOffset(0)] public KI key; }
 [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public U value; }
 [DllImport("user32.dll",EntryPoint="FindWindowW",CharSet=CharSet.Unicode,ExactSpelling=true)] public static extern IntPtr FindWindow(IntPtr c,string title);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr w);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr w);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll",SetLastError=true)] public static extern uint SendInput(uint count,INPUT[] data,int size);
 public static void Mouse(uint flag) { INPUT i=new INPUT();i.type=0;i.value.mouse.flags=flag;SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT))); }
 public static void Key(ushort code,bool up) { INPUT i=new INPUT();i.type=1;i.value.key.vk=code;i.value.key.flags=up?2u:0u;SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT))); }
 public static void Text(string text) { foreach(char ch in text) { INPUT i=new INPUT();i.type=1;i.value.key.scan=ch;i.value.key.flags=4;SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT)));i.value.key.flags=6;SendInput(1,new[]{i},Marshal.SizeOf(typeof(INPUT))); } }
}
'@
[QAInput]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$deadline=(Get-Date).AddSeconds(90);$main=[IntPtr]::Zero
while ((Get-Date) -lt $deadline) { $main=[QAInput]::FindWindow([IntPtr]::Zero,'ClassicMac');if($main -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100 }
if($main -eq [IntPtr]::Zero){throw 'No main window for native input'}
Start-Sleep -Milliseconds 200
$mr=New-Object QAInput+RECT
[QAInput]::GetWindowRect($main,[ref]$mr) | Out-Null
$mz=[Math]::Max(1,[Math]::Round([QAInput]::GetDpiForWindow($main)/96.0,[MidpointRounding]::AwayFromZero))
[QAInput]::SetCursorPos(($mr.left+($mr.right-$mr.left)/2),($mr.top+10*$mz)) | Out-Null
[QAInput]::Mouse(2);[QAInput]::Mouse(4)
$deadline=(Get-Date).AddSeconds(90);$w=[IntPtr]::Zero
while ((Get-Date) -lt $deadline) { $w=[QAInput]::FindWindow([IntPtr]::Zero,'Get Info');if($w -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100 }
if ($w -eq [IntPtr]::Zero) { throw 'No settings window for native input' }
Start-Sleep -Milliseconds 200
$r=New-Object QAInput+RECT
[QAInput]::GetWindowRect($w,[ref]$r) | Out-Null
$dpi=[QAInput]::GetDpiForWindow($w);$zoom=[Math]::Max(1,[Math]::Round($dpi/96.0,[MidpointRounding]::AwayFromZero))
[QAInput]::SetForegroundWindow($w) | Out-Null
[QAInput]::SetCursorPos(($r.left+300*$zoom),($r.top+47*$zoom)) | Out-Null
[QAInput]::Mouse(2);[QAInput]::Mouse(4)
Start-Sleep -Milliseconds 100
[QAInput]::Key(0x11,$false);[QAInput]::Key(0x41,$false);[QAInput]::Key(0x41,$true);[QAInput]::Key(0x11,$true)
$typed='Windows QA '+[char]0xE9+' '+[char]0xF1
[QAInput]::Text($typed)
$json=@{method='Windows SendInput';expected=$typed;windowsDpi=$dpi;session=[Diagnostics.Process]::GetCurrentProcess().SessionId} | ConvertTo-Json
[IO.File]::WriteAllText((Join-Path $Output 'native-input.json'),$json,(New-Object Text.UTF8Encoding($false)))

$first=Join-Path $Output 'guest-started.json'
$deadline=(Get-Date).AddSeconds(180)
while (-not (Test-Path $first)) { if((Get-Date)-gt $deadline){throw 'No guest capture for native mouse input'};Start-Sleep -Milliseconds 100 }
$state=Get-Content $first -Raw -Encoding UTF8 | ConvertFrom-Json
$view=$state.displays[0]
$w=[QAInput]::FindWindow([IntPtr]::Zero,$typed)
if($w -eq [IntPtr]::Zero){throw 'No guest window for native mouse input'}
[QAInput]::GetWindowRect($w,[ref]$r) | Out-Null
$x=$r.left+[Math]::Round(($view.viewX+$view.imageX+14.5*$view.imageWidth/$view.frameWidth)*$view.scale)
$y=$r.top+[Math]::Round(($view.viewY+$view.imageY+10.5*$view.imageHeight/$view.frameHeight)*$view.scale)
[QAInput]::SetForegroundWindow($w) | Out-Null
[QAInput]::SetCursorPos($x,$y) | Out-Null
[QAInput]::Mouse(2);[QAInput]::Mouse(4)
@{method='Windows SendInput';x=$x;y=$y;guestX=14;guestY=10;session=[Diagnostics.Process]::GetCurrentProcess().SessionId} | ConvertTo-Json | Set-Content (Join-Path $Output 'native-mouse.json')
$shot=Join-Path $Output 'guest-native-menu.png'
while (-not (Test-Path $shot)) { if((Get-Date)-gt $deadline){throw 'No native guest menu capture'};Start-Sleep -Milliseconds 100 }
[QAInput]::Key(0x1B,$false);[QAInput]::Key(0x1B,$true)

@{completed=$true;session=[Diagnostics.Process]::GetCurrentProcess().SessionId} | ConvertTo-Json | Set-Content (Join-Path $Output 'native-input-complete.json')
exit 0
