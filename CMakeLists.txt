cmake_minimum_required(VERSION 3.20)

project(NekoMusic
        VERSION 2026.531.26
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 交叉编译时勿用 UNIX/WIN32 混判；以目标系统名为准（Linux→Windows 时 UNIX 仍可能为真）
if(CMAKE_SYSTEM_NAME STREQUAL "Windows"
   OR (CMAKE_CROSSCOMPILING AND CMAKE_CXX_COMPILER MATCHES "mingw"))
    set(NEKO_TARGET_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(NEKO_TARGET_APPLE TRUE)
else()
    set(NEKO_TARGET_UNIX TRUE)
endif()
message(STATUS "NEKO target: WINDOWS=${NEKO_TARGET_WINDOWS} APPLE=${NEKO_TARGET_APPLE} UNIX=${NEKO_TARGET_UNIX} SYSTEM=${CMAKE_SYSTEM_NAME}")

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network SvgWidgets Sql Concurrent)
if(NEKO_TARGET_UNIX)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

# Qt .qrc must be defined before the cross-compile block: that block runs RCC in a
# foreach(${RESOURCES}) and needs this list at configure time. If RESOURCES is still
# empty there, Windows cross-builds link without embedded QSS/icons (styles "gone").
set(RESOURCES
        src/resources/resources.qrc
)

# For cross-compilation, disable AUTOMOC/AUTOUIC/AUTORCC and handle manually
# because Qt6 CMake hardcodes Windows .exe paths for moc/uic/rcc
if(CMAKE_CROSSCOMPILING)
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTORCC OFF)
    set(CMAKE_AUTOUIC OFF)

    # Use Linux Qt tools for code generation
    if(DEFINED ENV{QT_HOST_ROOT})
        set(QT_HOST_MOC "$ENV{QT_HOST_ROOT}/libexec/moc")
        set(QT_HOST_RCC "$ENV{QT_HOST_ROOT}/libexec/rcc")
        set(QT_HOST_UIC "$ENV{QT_HOST_ROOT}/libexec/uic")
    else()
        set(QT_HOST_MOC "/usr/lib/qt6/libexec/moc")
        set(QT_HOST_RCC "/usr/lib/qt6/libexec/rcc")
        set(QT_HOST_UIC "/usr/lib/qt6/libexec/uic")
    endif()

    # Manually run moc on headers that contain Q_OBJECT
    set(MOC_OUTPUT_DIR "${CMAKE_BINARY_DIR}/moc")
    file(MAKE_DIRECTORY ${MOC_OUTPUT_DIR})

    # Find all headers with Q_OBJECT macro
    file(GLOB_RECURSE ALL_HEADERS "${CMAKE_SOURCE_DIR}/src/*.h")
    foreach(header ${ALL_HEADERS})
        # Linux-only Qt headers (DBus/portal) must not be moc'd for Windows targets
        if(NEKO_TARGET_WINDOWS AND header MATCHES "globalshortcutportal_linux|globalshortcutcontroller_linux")
            continue()
        endif()
        file(READ "${header}" header_content)
        if(header_content MATCHES "Q_OBJECT")
            file(RELATIVE_PATH rel_header "${CMAKE_SOURCE_DIR}" "${header}")
            get_filename_component(name "${header}" NAME_WE)
            get_filename_component(subdir "${header}" DIRECTORY)
            file(RELATIVE_PATH rel_subdir "${CMAKE_SOURCE_DIR}/src" "${subdir}")
            set(moc_out "${MOC_OUTPUT_DIR}/${rel_subdir}/moc_${name}.cpp")
            get_filename_component(moc_dir "${moc_out}" DIRECTORY)
            file(MAKE_DIRECTORY "${moc_dir}")
            add_custom_command(
                    OUTPUT "${moc_out}"
                    COMMAND "${QT_HOST_MOC}" "${header}" -o "${moc_out}"
                    DEPENDS "${header}"
                    COMMENT "MOC ${rel_header}"
            )
            list(APPEND MOC_SOURCES "${moc_out}")
        endif()
    endforeach()

    # Manually compile resources (host rcc on Linux; link against Windows Qt).
    # Official Windows MinGW Qt builds often omit zstd (QT_FEATURE_zstd=OFF); host rcc 6.8+
    # defaults to zstd and emits qResourceFeatureZstd() → link error. Match qt_add_resources
    # behaviour: pass --no-zstd when the target stack cannot decompress zstd.
    foreach(qrc ${RESOURCES})
        get_filename_component(name "${qrc}" NAME_WE)
        set(rcc_out "${CMAKE_BINARY_DIR}/qrc_${name}.cpp")
        add_custom_command(
                OUTPUT "${rcc_out}"
                COMMAND "${QT_HOST_RCC}" --no-zstd -o "${rcc_out}" "${CMAKE_SOURCE_DIR}/${qrc}"
                DEPENDS "${CMAKE_SOURCE_DIR}/${qrc}"
                COMMENT "RCC ${qrc}"
        )
        list(APPEND RCC_SOURCES "${rcc_out}")
    endforeach()

    # Add generated sources to the target
    set(GENERATED_SOURCES ${MOC_SOURCES} ${RCC_SOURCES})
else()
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)
    set(GENERATED_SOURCES "")
endif()

# Platform-specific settings
if(WIN32)
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_SOURCE_DIR}/src/resources/windows/app.rc")
endif()

if(APPLE)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER com.nekomusic.app)
    set(MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
    set(APP_ICON_RESOURCE_MACOS "${CMAKE_SOURCE_DIR}/src/resources/macos/AppIcon.icns")
endif()

# Sources
set(SOURCES
        src/main.cpp
        src/ui/mainwindow.cpp
        src/ui/glasswidget.cpp
        src/ui/glasspaint.cpp
        src/ui/titlebar.cpp
        src/ui/sidebar.cpp
        src/ui/carousel.cpp
        src/ui/homepage.cpp
        src/ui/playlistcard.cpp
        src/ui/playerbar.cpp
        src/ui/playerprogressslider.cpp
        src/ui/svgicon.cpp
        src/ui/settingspage.cpp
        src/ui/shortcutcapturebutton.cpp
        src/ui/toast.cpp
        src/ui/recentpage.cpp
        src/ui/logindialog.cpp
        src/ui/slidercaptchadialog.cpp
        src/ui/captchasliderrail.cpp
        src/ui/forgotpassworddialog.cpp
        src/ui/musiclistpage.cpp
        src/ui/playerpage.cpp
        src/ui/videorenderdialog.cpp
        src/ui/vippage.cpp
        src/ui/vippillbutton.cpp
        src/ui/desktoplrc.cpp
        src/ui/desktoplrcplatform.cpp
        src/ui/favoritespage.cpp
        src/ui/songcardwidget.cpp
        src/ui/localmusicbadgelabel.cpp
        src/ui/songlistwidget.cpp
        src/ui/songcontextmenu.cpp
        src/ui/coverlistcard.cpp
        src/ui/artistdetailpage.cpp
        src/ui/roundcoverlabel.cpp
        src/core/playerengine.cpp
        src/core/apiclient.cpp
        src/core/i18n.cpp
        src/core/appshortcuts.cpp
        src/core/globalshortcutcontroller.cpp
        src/core/listmetaformat.cpp
        src/core/covercache.cpp
        src/core/linuxtmpfscache.cpp
        src/core/usermanager.cpp
        src/core/qrcodegen.cpp
        src/core/vipqrcode.cpp
        src/core/musicdownloader.cpp
        src/core/playlistdb.cpp
        src/core/localmusicmeta.cpp
        src/core/audioquality.cpp
        src/core/embeddedlyrics.cpp
        src/core/playlistmanager.cpp
        src/core/updatechecker.cpp
        src/core/linuxinstallbackend.cpp
        src/ui/playlistlistitem.cpp
        src/ui/playlistdetailpage.cpp
        src/ui/addtoplaylistdialog.cpp
        src/ui/lineinputdialog.cpp
        src/ui/playlistpanel.cpp
        src/ui/updatedialog.cpp
        src/ui/defaultmusicplayerdialog.cpp
        src/ui/searchpage.cpp
        src/ui/neteaseimportdialog.cpp
        src/theme/thememanager.cpp
)

set(HEADERS
        src/ui/mainwindow.h
        src/ui/glasswidget.h
        src/ui/glasspaint.h
        src/ui/scrollareafix.h
        src/ui/titlebar.h
        src/ui/sidebar.h
        src/ui/carousel.h
        src/ui/homepage.h
        src/ui/playlistcard.h
        src/ui/playerbar.h
        src/ui/playerprogressslider.h
        src/ui/svgicon.h
        src/ui/settingspage.h
        src/ui/shortcutcapturebutton.h
        src/ui/recentpage.h
        src/ui/logindialog.h
        src/ui/slidercaptchadialog.h
        src/ui/captchasliderrail.h
        src/ui/forgotpassworddialog.h
        src/ui/musiclistpage.h
        src/ui/playerpage.h
        src/ui/videorenderdialog.h
        src/ui/vippage.h
        src/ui/vippillbutton.h
        src/ui/desktoplrc.h
        src/ui/desktoplrcplatform.h
        src/ui/favoritespage.h
        src/ui/songcardwidget.h
        src/ui/localmusicbadgelabel.h
        src/ui/songlistwidget.h
        src/ui/songcontextmenu.h
        src/ui/coverlistcard.h
        src/ui/roundcoverlabel.h
        src/core/playerengine.h
        src/core/apiclient.h
        src/core/httpprotocollabel.h
        src/core/i18n.h
        src/core/appshortcuts.h
        src/core/globalshortcutcontroller.h
        src/core/covercache.h
        src/core/linuxtmpfscache.h
        src/core/usermanager.h
        src/core/vipqrcode.h
        src/theme/theme.h
        src/theme/thememanager.h
        src/core/musicinfo.h
        src/core/listmetaformat.h
        src/core/localmusicmeta.h
        src/core/audioquality.h
        src/core/embeddedlyrics.h
        src/core/playlistdb.h
        src/core/playlistmanager.h
        src/core/updatechecker.h
        src/ui/playlistlistitem.h
        src/ui/playlistdetailpage.h
        src/ui/addtoplaylistdialog.h
        src/ui/lineinputdialog.h
        src/ui/playlistpanel.h
        src/ui/updatedialog.h
        src/ui/defaultmusicplayerdialog.h
        src/ui/searchpage.h
        src/ui/artistdetailpage.h
        src/ui/neteaseimportdialog.h
        src/core/systemmediacontroller.h
        src/core/defaultmusicappchecker.h
)

if(NEKO_TARGET_WINDOWS)
    list(APPEND SOURCES src/core/systemmediacontroller_win.cpp)
    list(APPEND SOURCES src/core/defaultmusicappchecker_win.cpp)
    set_source_files_properties(
        src/core/globalshortcutportal_linux.h
        src/core/globalshortcutportal_linux.cpp
        src/core/globalshortcutcontroller_linux.h
        src/core/globalshortcutcontroller_linux.cpp
        PROPERTIES SKIP_AUTOMOC ON)
elseif(NEKO_TARGET_APPLE)
    list(APPEND SOURCES src/core/systemmediacontroller_mac.mm)
    list(APPEND SOURCES src/core/defaultmusicappchecker_mac.mm)
elseif(NEKO_TARGET_UNIX)
    list(APPEND SOURCES src/core/systemmediacontroller_linux.cpp)
    list(APPEND SOURCES src/core/defaultmusicappchecker_linux.cpp)
    list(APPEND SOURCES src/core/globalshortcutportal_linux.cpp)
    list(APPEND SOURCES src/core/globalshortcutcontroller_linux.cpp)
else()
    list(APPEND SOURCES src/core/systemmediacontroller_stub.cpp)
    list(APPEND SOURCES src/core/defaultmusicappchecker_stub.cpp)
endif()

if(WIN32)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES} ${APP_ICON_RESOURCE_WINDOWS} ${GENERATED_SOURCES})
elseif(APPLE)
    # Defensive: only include macOS icon if it exists; otherwise warn and build without it.
    if(EXISTS "${APP_ICON_RESOURCE_MACOS}")
        message(STATUS "Using macOS icon: ${APP_ICON_RESOURCE_MACOS}")
        add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${RESOURCES} ${APP_ICON_RESOURCE_MACOS} ${GENERATED_SOURCES})
        set_source_files_properties(${APP_ICON_RESOURCE_MACOS} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    else()
        message(WARNING "AppIcon.icns not found at ${APP_ICON_RESOURCE_MACOS}; building without macOS icon")
        add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS} ${RESOURCES} ${GENERATED_SOURCES})
    endif()
else()
    add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES} ${GENERATED_SOURCES})
endif()

# 交叉编译 Windows 时 moc 在 Linux 主机上运行，须用目标平台宏而非主机 Q_OS_LINUX
if(CMAKE_CROSSCOMPILING)
    set_target_properties(${PROJECT_NAME} PROPERTIES AUTOMOC_COMPILER_PREDEFINES OFF)
    if(NEKO_TARGET_WINDOWS)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            AUTOMOC_MOC_OPTIONS "-DWIN32;-DWIN64;-D_WIN32")
    endif()
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::Network
        Qt6::SvgWidgets
        Qt6::Sql
        Qt6::Concurrent
)
if(NEKO_TARGET_UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::DBus)
    find_package(X11)
    if(X11_FOUND)
        target_link_libraries(${PROJECT_NAME} PRIVATE X11::X11)
    endif()
    # KDE Plasma：Wayland 下通过 KWin 请求 KeepAbove（构建需 libkf6windowsystem-dev）
    find_package(KF6WindowSystem CONFIG QUIET)
    if(NOT TARGET KF6::WindowSystem)
        find_package(KF6WindowSystem CONFIG QUIET
            PATHS "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/KF6WindowSystem")
    endif()
    if(TARGET KF6::WindowSystem)
        message(STATUS "KF6::WindowSystem found — KDE Plasma desktop lyrics keep-above enabled")
        target_link_libraries(${PROJECT_NAME} PRIVATE KF6::WindowSystem)
        target_compile_definitions(${PROJECT_NAME} PRIVATE NEKO_HAS_KDE_WINDOWSYSTEM=1)
    else()
        message(STATUS "KF6::WindowSystem not found — build without KDE Plasma keep-above")
    endif()
    # KDE Plasma Wayland：layer-shell 置顶（需 liblayershellqtinterface-dev）
    find_package(LayerShellQt CONFIG QUIET)
    if(NOT TARGET LayerShellQt::Interface)
        find_package(LayerShellQt CONFIG QUIET
            PATHS "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/cmake/LayerShellQt")
    endif()
    if(TARGET LayerShellQt::Interface)
        message(STATUS "LayerShellQt found — KDE Wayland desktop lyrics overlay enabled")
        target_link_libraries(${PROJECT_NAME} PRIVATE LayerShellQt::Interface)
        target_compile_definitions(${PROJECT_NAME} PRIVATE NEKO_HAS_LAYERSHELL_QT=1)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(WAYLAND_CLIENT QUIET wayland-client)
        endif()
        if(WAYLAND_CLIENT_FOUND)
            message(STATUS "wayland-client found — desktop lyrics click-through input region enabled")
            target_link_libraries(${PROJECT_NAME} PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
            target_include_directories(${PROJECT_NAME} PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
            target_compile_definitions(${PROJECT_NAME} PRIVATE NEKO_DESKTOP_LRC_WAYLAND_INPUT=1)
        else()
            message(STATUS "wayland-client not found — desktop lyrics without Wayland input region")
        endif()
        find_package(Qt6 COMPONENTS GuiPrivate QUIET)
        if(TARGET Qt6::GuiPrivate)
            target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::GuiPrivate)
            target_compile_definitions(${PROJECT_NAME} PRIVATE NEKO_HAS_QT_GUI_PRIVATE=1)
        endif()
    else()
        message(STATUS "LayerShellQt not found — build without KDE Wayland layer-shell")
    endif()
endif()
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            "-framework Foundation"
            "-framework MediaPlayer"
            "-framework CoreServices"
    )
endif()
if(WIN32)
    # MSVC 使用 Shlwapi.lib；MinGW 交叉编译使用 libshlwapi.a（小写 -lshlwapi）
    if(MINGW)
        target_link_libraries(${PROJECT_NAME} PRIVATE shlwapi)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE Shlwapi)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

# Generate version header from CMake project version
configure_file(
        ${CMAKE_SOURCE_DIR}/src/core/version.h.in
        ${CMAKE_BINARY_DIR}/generated/version.h
)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/generated)

# Install - platform-specific rules
if(LINUX)
    # Linux: install to /opt/Neko云音乐 (matches auto-update code)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION /opt/Neko云音乐
    )

    # Desktop entry file
    install(FILES "${CMAKE_SOURCE_DIR}/packaging/nekomusic.desktop"
            DESTINATION share/applications
    )

    # Symlink in /usr/local/bin for terminal access
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{DESTDIR}/usr/local/bin\")
        execute_process(
            COMMAND \${CMAKE_COMMAND} -E create_symlink
            \"/opt/Neko云音乐/NekoMusic\"
            \"\$ENV{DESTDIR}/usr/local/bin/nekomusic\"
        )
    ")

    # Icon theme (hicolor)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/packaging/icons/hicolor/"
            DESTINATION share/icons/hicolor
            PATTERN ".*" EXCLUDE
    )
elseif(WIN32)
    # Windows: install to root (NSIS handles packaging)
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION .
    )
else()
    install(TARGETS ${PROJECT_NAME}
            RUNTIME DESTINATION bin
            BUNDLE DESTINATION .
    )
endif()

# CPack configuration
set(CPACK_PACKAGE_NAME "Neko云音乐")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION}${PROJECT_VERSION_SUFFIX})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "梦幻网络CN免费云音乐平台的官方客户端")
set(CPACK_PACKAGE_VENDOR "FantasyNetwork")
set(CPACK_PACKAGE_CONTACT "support@cnmsb.xin")
set(CPACK_PACKAGE_FILE_NAME "Neko云音乐-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

if(LINUX)
    # Debian package settings
    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_NAME "nekomusic")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "FantasyNetwork <support@cnmsb.xin>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "multimedia")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(_neko_debian_depends
            libqt6core6
            libqt6gui6
            libqt6widgets6
            libqt6multimedia6
            libqt6network6
            libqt6sql6
            libqt6dbus6
    )
    if(TARGET KF6::WindowSystem)
        list(APPEND _neko_debian_depends libkf6windowsystem6)
    endif()
    if(TARGET LayerShellQt::Interface)
        list(APPEND _neko_debian_depends liblayershellqtinterface6)
    endif()
    list(JOIN _neko_debian_depends ", " CPACK_DEBIAN_PACKAGE_DEPENDS)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    set(CPACK_DEBIAN_PACKAGE_POSTINST "${CMAKE_SOURCE_DIR}/packaging/debian/postinst")
    set(CPACK_DEBIAN_PACKAGE_POSTRM "${CMAKE_SOURCE_DIR}/packaging/debian/postrm")

    # Output filename format matching version.json URL pattern
    set(CPACK_PACKAGE_FILE_NAME "Neko云音乐-${CPACK_PACKAGE_VERSION}-linux-amd64")

elseif(WIN32)
    # Windows uses NSIS (external script, not CPack)
    # CPack not used for Windows - build_windows.ps1 handles NSIS directly

elseif(APPLE)
    set(CPACK_GENERATOR "productbuild")
    set(CPACK_PACKAGE_FILE_NAME "Neko云音乐-${CPACK_PACKAGE_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}")
endif()

include(CPack)

# Tests
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
