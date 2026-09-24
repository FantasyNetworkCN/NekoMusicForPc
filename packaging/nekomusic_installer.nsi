; Neko歌姬计划 Windows Installer Script
; NSIS 3.0+

!include "MUI2.nsh"

; Version (passed via -DVERSION= on command line)
!ifndef VERSION
  !define VERSION "0.0.0"
!endif

!define APP_DISPLAY_NAME "Neko歌姬计划"
!define APP_EXE "NekoMusic.exe"
!define APP_REGISTERED_NAME "NekoMusic"
!define APP_PROGID "NekoMusic.AudioFile"
!define APP_CAPABILITIES "Software\NekoMusic\Capabilities"

; 待打包的部署目录。默认相对本脚本所在目录（packaging\..\build）；
; 相对路径在不同环境下的解析基准不一致，CI 会用 -DBUILD_DIR=<绝对路径> 覆盖。
!ifndef BUILD_DIR
  !define BUILD_DIR "${__FILEDIR__}\..\build"
!endif

; Repository-root third-party payload. Override with -DVB_CABLE_SETUP=<path>
; in CI if the binary is staged elsewhere.
!ifndef VB_CABLE_SETUP
  !define VB_CABLE_SETUP "${__FILEDIR__}\..\src\resources\drivers\vb-cable\VBCABLE_Setup_x64.exe"
!endif

!macro RegisterMediaAssociation EXT MIME
    WriteRegStr HKLM "${APP_CAPABILITIES}\FileAssociations" "${EXT}" "${APP_PROGID}"
    WriteRegStr HKLM "${APP_CAPABILITIES}\MIMEAssociations" "${MIME}" "${APP_PROGID}"
    WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\SupportedTypes" "${EXT}" ""
    WriteRegStr HKLM "Software\Classes\${EXT}\OpenWithProgids" "${APP_PROGID}" ""
!macroend

!macro UnregisterMediaAssociation EXT
    DeleteRegValue HKLM "Software\Classes\${EXT}\OpenWithProgids" "${APP_PROGID}"
!macroend

; General
Name "${APP_DISPLAY_NAME}"
; OutFile 须用 ${VERSION}（编译期）；$VERSION 在 NSIS 中为运行时变量，展开为空会导致文件名与 build_windows.sh 不一致
OutFile "..\Neko歌姬计划-${VERSION}-win.exe"
InstallDir "$PROGRAMFILES64\Neko歌姬计划"
SetCompressor lzma

; Manifest
ManifestSupportedOS all
RequestExecutionLevel admin

; UI Settings
!define MUI_ICON "app.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\nsis3-branding.bmp"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; Installer
Section "Neko歌姬计划" SecMain
    SetRegView 64
    SetOutPath "$INSTDIR"

    ; Install main executable
    File "${BUILD_DIR}\${APP_EXE}"

    ; Install Qt dependencies
    File /nonfatal /r "${BUILD_DIR}\platforms"
    File /nonfatal /r "${BUILD_DIR}\multimedia"
    File /nonfatal /r "${BUILD_DIR}\iconengines"
    File /nonfatal /r "${BUILD_DIR}\imageformats"
    File /nonfatal /r "${BUILD_DIR}\styles"
    File /nonfatal /r "${BUILD_DIR}\translations"
    File /nonfatal /r "${BUILD_DIR}\sqldrivers"
    File /nonfatal /r "${BUILD_DIR}\tls"
    File /nonfatal /r "${BUILD_DIR}\networkinformation"
    File /nonfatal /r "${BUILD_DIR}\generic"

    ; Qt DLLs
    File "${BUILD_DIR}\Qt6Core.dll"
    File "${BUILD_DIR}\Qt6Gui.dll"
    File "${BUILD_DIR}\Qt6Widgets.dll"
    File "${BUILD_DIR}\Qt6Multimedia.dll"
    File "${BUILD_DIR}\Qt6Network.dll"
    File "${BUILD_DIR}\Qt6Sql.dll"
    File "${BUILD_DIR}\Qt6Svg.dll"
    File "${BUILD_DIR}\Qt6SvgWidgets.dll"
    File "${BUILD_DIR}\Qt6Concurrent.dll"

    ; Qt Multimedia -> FFmpeg（版本号随 Qt 变化，用通配）
    File /nonfatal "${BUILD_DIR}\avcodec-*.dll"
    File /nonfatal "${BUILD_DIR}\avformat-*.dll"
    File /nonfatal "${BUILD_DIR}\avutil-*.dll"
    File /nonfatal "${BUILD_DIR}\swresample-*.dll"
    File /nonfatal "${BUILD_DIR}\swscale-*.dll"

    ; MinGW / 图形依赖（与 build_windows.sh 部署目录一致）
    File "${BUILD_DIR}\libstdc++-6.dll"
    File "${BUILD_DIR}\libgcc_s_seh-1.dll"
    File "${BUILD_DIR}\libwinpthread-1.dll"
    File /nonfatal "${BUILD_DIR}\d3dcompiler_47.dll"
    File /nonfatal "${BUILD_DIR}\opengl32sw.dll"

    ; Optional: OpenSSL DLLs
    File /nonfatal "${BUILD_DIR}\libssl*.dll"
    File /nonfatal "${BUILD_DIR}\libcrypto*.dll"

    ; Bundled VB-CABLE installer (redistribution authorized by the copyright
    ; holder). Installed separately and launched on first microphone-sync use.
    SetOutPath "$INSTDIR\drivers\nekomic"
    File "${VB_CABLE_SETUP}"

    ; Create Start Menu Shortcut
    CreateDirectory "$SMPROGRAMS\Neko歌姬计划"
    CreateShortCut "$SMPROGRAMS\Neko歌姬计划\Neko歌姬计划.lnk" "$INSTDIR\${APP_EXE}"
    CreateShortCut "$SMPROGRAMS\Neko歌姬计划\卸载 Neko歌姬计划.lnk" "$INSTDIR\uninst.exe"

    ; Create Desktop Shortcut
    CreateShortCut "$DESKTOP\Neko歌姬计划.lnk" "$INSTDIR\${APP_EXE}"

    ; Registry for uninstall
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "DisplayName" "Neko歌姬计划"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "UninstallString" "$INSTDIR\uninst.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "QuietUninstallString" "$\"$INSTDIR\uninst.exe$\" /S"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "DisplayIcon" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "Publisher" "Neko"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划" \
                     "DisplayVersion" "${VERSION}"

    ; Register as an Open With / Default apps candidate for every supported local media type.
    WriteRegStr HKLM "Software\RegisteredApplications" "${APP_REGISTERED_NAME}" "${APP_CAPABILITIES}"
    WriteRegStr HKLM "${APP_CAPABILITIES}" "ApplicationName" "${APP_DISPLAY_NAME}"
    WriteRegStr HKLM "${APP_CAPABILITIES}" "ApplicationDescription" "NekoMusic audio player"
    WriteRegStr HKLM "${APP_CAPABILITIES}" "ApplicationIcon" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}" "" "$INSTDIR\${APP_EXE}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}" "Path" "$INSTDIR"

    WriteRegStr HKLM "Software\Classes\${APP_PROGID}" "" "NekoMusic media file"
    WriteRegStr HKLM "Software\Classes\${APP_PROGID}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKLM "Software\Classes\${APP_PROGID}\shell\open\command" "" "$\"$INSTDIR\${APP_EXE}$\" $\"%1$\""

    WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}" "FriendlyAppName" "${APP_DISPLAY_NAME}"
    WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\DefaultIcon" "" "$INSTDIR\${APP_EXE},0"
    WriteRegStr HKLM "Software\Classes\Applications\${APP_EXE}\shell\open\command" "" "$\"$INSTDIR\${APP_EXE}$\" $\"%1$\""

    !insertmacro RegisterMediaAssociation ".mp3" "audio/mpeg"
    !insertmacro RegisterMediaAssociation ".flac" "audio/flac"
    !insertmacro RegisterMediaAssociation ".wav" "audio/x-wav"
    !insertmacro RegisterMediaAssociation ".m4a" "audio/mp4"
    !insertmacro RegisterMediaAssociation ".aac" "audio/aac"
    !insertmacro RegisterMediaAssociation ".ogg" "audio/ogg"
    !insertmacro RegisterMediaAssociation ".oga" "audio/ogg"
    !insertmacro RegisterMediaAssociation ".opus" "audio/opus"
    !insertmacro RegisterMediaAssociation ".mp4" "audio/mp4"
    !insertmacro RegisterMediaAssociation ".wma" "audio/x-ms-wma"
    !insertmacro RegisterMediaAssociation ".mpc" "audio/x-musepack"
    !insertmacro RegisterMediaAssociation ".spx" "audio/x-speex"
    !insertmacro RegisterMediaAssociation ".ra" "audio/vnd.rn-realaudio"
    !insertmacro RegisterMediaAssociation ".ram" "audio/vnd.rn-realaudio"
    !insertmacro RegisterMediaAssociation ".m3u" "audio/mpegurl"
    !insertmacro RegisterMediaAssociation ".m3u8" "audio/x-mpegurl"
    !insertmacro RegisterMediaAssociation ".pls" "audio/x-scpls"

    System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'

    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninst.exe"
SectionEnd

; Uninstaller
Section "Uninstall"
    SetRegView 64
    ; Remove registry keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Neko歌姬计划"
    DeleteRegValue HKLM "Software\RegisteredApplications" "${APP_REGISTERED_NAME}"
    DeleteRegKey HKLM "Software\NekoMusic"
    DeleteRegKey HKLM "Software\Classes\${APP_PROGID}"
    DeleteRegKey HKLM "Software\Classes\Applications\${APP_EXE}"
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\App Paths\${APP_EXE}"
    !insertmacro UnregisterMediaAssociation ".mp3"
    !insertmacro UnregisterMediaAssociation ".flac"
    !insertmacro UnregisterMediaAssociation ".wav"
    !insertmacro UnregisterMediaAssociation ".m4a"
    !insertmacro UnregisterMediaAssociation ".aac"
    !insertmacro UnregisterMediaAssociation ".ogg"
    !insertmacro UnregisterMediaAssociation ".oga"
    !insertmacro UnregisterMediaAssociation ".opus"
    !insertmacro UnregisterMediaAssociation ".mp4"
    !insertmacro UnregisterMediaAssociation ".wma"
    !insertmacro UnregisterMediaAssociation ".mpc"
    !insertmacro UnregisterMediaAssociation ".spx"
    !insertmacro UnregisterMediaAssociation ".ra"
    !insertmacro UnregisterMediaAssociation ".ram"
    !insertmacro UnregisterMediaAssociation ".m3u"
    !insertmacro UnregisterMediaAssociation ".m3u8"
    !insertmacro UnregisterMediaAssociation ".pls"

    ; Remove shortcuts
    Delete "$DESKTOP\Neko歌姬计划.lnk"
    RMDir /r "$SMPROGRAMS\Neko歌姬计划"

    ; Remove installed files
    RMDir /r "$INSTDIR"
    System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
