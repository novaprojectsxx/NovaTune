# Windows implementation references

The executable is native C++ and does not depend on these projects or install external utilities.

- WASAPI capture model: https://learn.microsoft.com/en-us/windows/win32/coreaudio/capturing-a-stream
- Display mode validation/application: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-changedisplaysettingsexw
- Default-audio compatibility ABI: https://github.com/frgnca/AudioDeviceCmdlets/blob/master/SOURCE/IPolicyConfig.cs and https://github.com/frgnca/AudioDeviceCmdlets/blob/master/SOURCE/PolicyConfigClient.cs

Default-device switching uses the isolated IPolicyConfig compatibility interface; Microsoft does not publish it as a supported default-endpoint setter. Failure or disconnected endpoints are reported and any already-applied profile settings are restored. Enumeration and microphone capture use documented Core Audio interfaces. No audio-switching dependency is bundled.

## Desktop Studio

- DWM thumbnails: https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmregisterthumbnail
- Playback loopback: https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
- Window parenting and DPI: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent
- Shell broker dispatch: https://learn.microsoft.com/en-us/windows/win32/shell/ishelldispatch2-shellexecute
- Desktop shell discovery: https://learn.microsoft.com/en-us/windows/win32/api/exdisp/nf-exdisp-ishellwindows-findwindowsw
- Optional weather provider: https://open-meteo.com/en/docs
- Explorer wallpaper hosting is undocumented compatibility behavior, not a supported Windows wallpaper API. Modern Win11 layout observations: https://github.com/lively-community/lively/issues/2074

Taskbar replacement: https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shappbarmessage

Native taskbar state capture/restore: https://learn.microsoft.com/en-us/windows/win32/shell/abm-getstate and https://learn.microsoft.com/en-us/windows/win32/shell/abm-setstate

Alt+Tab interception: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw

Plank appearance reference (inspiration only; no theme assets/code imported): https://github.com/zquestz/plank-reloaded

Output-volume widget: https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-setmastervolumelevelscalar

Native notification/tray shortcuts: https://support.microsoft.com/en-us/accessibility/windows/navigate-and-explore-the-windows-taskbar

PlayStation analog input normalization and report capability handling:
- HIDP_VALUE_CAPS (ReportID, IsRange, BitSize, LogicalMin/Max): https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/hidpi/ns-hidpi-_hidp_value_caps
