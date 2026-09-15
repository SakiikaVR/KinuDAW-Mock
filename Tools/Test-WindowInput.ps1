# Copyright (c) 2026 KinuDAW contributors. MIT License.
param([int]$ProcessId,[ValidateSet('click','double','drag','wheel','hold','release')][string]$Action,[int]$X,[int]$Y,[int]$EndX=0,[int]$EndY=0)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KinuTestInput {
 public delegate bool EnumProc(IntPtr h,IntPtr param);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc,IntPtr param);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h,uint msg,IntPtr w,IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h,ref Point p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")] public static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
 public static IntPtr Find(int pid) { IntPtr result=IntPtr.Zero; EnumWindows((h,p)=> { uint owner; GetWindowThreadProcessId(h,out owner); if(owner==pid && IsWindowVisible(h)) { result=h; return false; } return true; },IntPtr.Zero); return result; }
 public static void Send(IntPtr h,uint msg,int w,int x,int y) { PostMessageW(h,msg,new IntPtr(w),new IntPtr((y<<16)|(x&65535))); }
 public static void Move(IntPtr h,int x,int y) { Point p=new Point { X=x,Y=y }; ClientToScreen(h,ref p); SetCursorPos(p.X,p.Y); }
}
'@
$window=[KinuTestInput]::Find($ProcessId)
if($window -eq [IntPtr]::Zero) { throw 'No visible test window for this process' }
[KinuTestInput]::SetWindowPos($window,[IntPtr](-1),0,0,0,0,3) | Out-Null
[KinuTestInput]::SetForegroundWindow($window) | Out-Null
[KinuTestInput]::Move($window,$X,$Y)
Start-Sleep -Milliseconds 80
if($Action -eq 'wheel') { [KinuTestInput]::Send($window,0x20a,120 -shl 16,$X,$Y); return }
if($Action -eq 'release') { [KinuTestInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero); [KinuTestInput]::Move($window,($X+80),($Y+40)); [KinuTestInput]::SetWindowPos($window,[IntPtr](-2),0,0,0,0,3) | Out-Null; return }
[KinuTestInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
if($Action -eq 'hold') { return }
if($Action -eq 'drag') {
    for($step=1;$step -le 8;$step++) { [KinuTestInput]::Move($window,($X+($EndX-$X)*$step/8),($Y+($EndY-$Y)*$step/8)); Start-Sleep -Milliseconds 20 }
    [KinuTestInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    [KinuTestInput]::Move($window,($EndX+80),($EndY+40))
} else {
    [KinuTestInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    if($Action -eq 'double') { Start-Sleep -Milliseconds 80; [KinuTestInput]::mouse_event(2,0,0,0,[UIntPtr]::Zero); [KinuTestInput]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
}
Start-Sleep -Milliseconds 300
[KinuTestInput]::SetWindowPos($window,[IntPtr](-2),0,0,0,0,3) | Out-Null
