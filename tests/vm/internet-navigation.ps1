$ErrorActionPreference = 'Stop'
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NetworkNavigation {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string name,string title);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h,int msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr ItemRect(IntPtr h,int msg,IntPtr w,ref Rect r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int width,int height,int flags);
}
"@
$app=[NetworkNavigation]::FindWindow('HeadroomNativeWindow','Headroom')
if($app -eq [IntPtr]::Zero){throw 'App missing'}
$nav=[NetworkNavigation]::GetDlgItem($app,100)
$count=[NetworkNavigation]::SendMessage($nav,0x18B,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
$index=-1
for($i=0;$i -lt $count;$i++){if([NetworkNavigation]::SendMessage($nav,0x199,[IntPtr]$i,[IntPtr]::Zero).ToInt32() -eq 32){$index=$i}}
if($index -lt 0){throw 'Internet speed entry missing'}
$results=@()
foreach($size in @(@(1200,830),@(1060,760),@(1200,830))){
 [void][NetworkNavigation]::SetWindowPos($app,[IntPtr]::Zero,0,0,$size[0],$size[1],6)
 [void][NetworkNavigation]::SendMessage($nav,0x186,[IntPtr]$index,[IntPtr]::Zero)
 [void][NetworkNavigation]::SendMessage($app,0x111,[IntPtr]65636,$nav)
 $row=New-Object NetworkNavigation+Rect
 $client=New-Object NetworkNavigation+Rect
 [void][NetworkNavigation]::ItemRect($nav,0x198,[IntPtr]$index,[ref]$row)
 [void][NetworkNavigation]::GetClientRect($nav,[ref]$client)
 if($row.Top -lt 0 -or $row.Bottom -gt $client.Bottom){throw "Selected network entry outside sidebar: $($row.Top),$($row.Bottom) in $($client.Bottom)"}
 $results+="PASS selected Internet speed entry visible at $($size[0]) x $($size[1]): row $($row.Top)..$($row.Bottom), viewport $($client.Bottom)"
}
$results | Set-Content (Join-Path $PSScriptRoot 'internet-navigation.txt')
