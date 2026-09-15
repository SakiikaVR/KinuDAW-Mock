# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([int]$ProcessId, [string]$OutputPath, [int]$Width=0, [int]$Height=0,[switch]$CloseOnly,[string]$WindowTitle='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class KinuCapture {
 public delegate bool EnumProc(IntPtr window,IntPtr param);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window,out Rect rect);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window,int mode);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc,IntPtr param);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr window,StringBuilder text,int count);
 public static IntPtr Find(int pid,string title) {
   IntPtr result=IntPtr.Zero; int area=0;
   EnumWindows((window,param)=> { uint owner; GetWindowThreadProcessId(window,out owner); Rect r;
     StringBuilder text=new StringBuilder(2048); GetWindowTextW(window,text,2048);
     if(owner==pid && (title.Length==0 || text.ToString().Contains(title)) && GetWindowRect(window,out r) && (r.Right-r.Left)*(r.Bottom-r.Top)>area) { result=window; area=(r.Right-r.Left)*(r.Bottom-r.Top); } return true;
   },IntPtr.Zero); return result;
 }
}
'@
$application=Get-Process -Id $ProcessId
$window=[KinuCapture]::Find($ProcessId,$WindowTitle)
for($attempt=0;$window -eq 0 -and $attempt -lt 30;$attempt++) { Start-Sleep -Milliseconds 100; $window=[KinuCapture]::Find($ProcessId,$WindowTitle) }
if($window -eq 0 -and -not $WindowTitle) { $window=$application.MainWindowHandle }
if($window -eq 0) { throw 'Application has no native window' }
if($CloseOnly) { [KinuCapture]::PostMessageW($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null; $application.WaitForExit(10000) | Out-Null; return }
[KinuCapture]::ShowWindow($window,9) | Out-Null
if($Width -gt 0 -and $Height -gt 0) { [KinuCapture]::SetWindowPos($window,[IntPtr]::Zero,0,0,$Width,$Height,0x14) | Out-Null }
[KinuCapture]::SetForegroundWindow($window) | Out-Null
[KinuCapture]::SetWindowPos($window,[IntPtr](-1),0,0,0,0,3) | Out-Null
Start-Sleep -Milliseconds 500
$rect=[KinuCapture+Rect]::new()
[KinuCapture]::GetWindowRect($window,[ref]$rect) | Out-Null
$bitmap=[System.Drawing.Bitmap]::new($rect.Right-$rect.Left,$rect.Bottom-$rect.Top)
$graphics=[System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size)
$bitmap.Save($OutputPath,[System.Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
[KinuCapture]::SetWindowPos($window,[IntPtr](-2),0,0,0,0,3) | Out-Null
