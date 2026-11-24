find_package(Qt6 REQUIRED COMPONENTS Test)

if(TARGET neko-glass-shaders)
    add_executable(NekoMusicVulkanGlassTest
        liquidglass_vulkan.cpp "${PROJECT_SOURCE_DIR}/src/ui/liquidglassvulkan.cpp")
    add_dependencies(NekoMusicVulkanGlassTest neko-glass-shaders)
    target_include_directories(NekoMusicVulkanGlassTest PRIVATE
        "${PROJECT_SOURCE_DIR}/src" "${NEKO_VULKAN_HEADERS}" "${NEKO_GLASS_GENERATED_DIR}")
    target_compile_definitions(NekoMusicVulkanGlassTest PRIVATE NEKO_HAS_VULKAN_GLASS=1)
    target_link_libraries(NekoMusicVulkanGlassTest PRIVATE Qt6::Gui)
    add_test(NAME liquidglass-vulkan COMMAND NekoMusicVulkanGlassTest)
    set_tests_properties(liquidglass-vulkan PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
endif()

set(control_glass_sources
    control_glass.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/toggleswitch.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/liquidglass.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/liquidglassvulkan.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/appearancesettings.cpp"
    "${PROJECT_SOURCE_DIR}/src/theme/thememanager.cpp")
add_executable(NekoMusicControlGlassTest ${control_glass_sources})
if(TARGET neko-glass-shaders)
    add_dependencies(NekoMusicControlGlassTest neko-glass-shaders)
    target_compile_definitions(NekoMusicControlGlassTest PRIVATE NEKO_HAS_VULKAN_GLASS=1)
endif()
target_include_directories(NekoMusicControlGlassTest PRIVATE
    "${PROJECT_SOURCE_DIR}/src" "${NEKO_VULKAN_HEADERS}" "${NEKO_GLASS_GENERATED_DIR}")
target_link_libraries(NekoMusicControlGlassTest PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGLWidgets Qt6::Test)
add_test(NAME control-glass COMMAND NekoMusicControlGlassTest)
set_tests_properties(control-glass PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    SKIP_RETURN_CODE 77
    TIMEOUT 30)

add_executable(NekoMusicAmllMotionTest
    amll_motion.cpp
    "${PROJECT_SOURCE_DIR}/src/ui/amllbackgroundwidget.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/amlllyricsview.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/lyriceffectsettings.cpp")
target_include_directories(NekoMusicAmllMotionTest PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(NekoMusicAmllMotionTest PRIVATE Qt6::OpenGLWidgets Qt6::Test)
add_test(NAME amll-motion COMMAND NekoMusicAmllMotionTest)
set_tests_properties(amll-motion PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 15)

set(stem_sources
    stem_separation.cpp
    "${PROJECT_SOURCE_DIR}/src/core/apiclient.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/applog.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/httpmediadevice.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/i18n.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/linuxtmpfscache.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/nekonetworkaccessmanager.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/playerengine.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/stemaudiomixer.cpp"
    "${PROJECT_SOURCE_DIR}/src/core/usermanager.cpp"
    "${PROJECT_SOURCE_DIR}/src/theme/thememanager.cpp"
    "${PROJECT_SOURCE_DIR}/src/ui/stemmixerdialog.cpp")
add_executable(NekoMusicStemTest ${stem_sources})
target_include_directories(NekoMusicStemTest PRIVATE $<TARGET_PROPERTY:NekoMusic,INCLUDE_DIRECTORIES>)
target_link_libraries(NekoMusicStemTest PRIVATE
    Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::Test)
add_test(NAME stem-separation COMMAND NekoMusicStemTest)
set_tests_properties(stem-separation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 25)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ui_smoke.cpp")
    return()
endif()

# 复用生产代码与资源，验证真实控件。测试入口不执行单实例、协议注册或用户协议流程。
set(ui_sources ${SOURCES})
list(REMOVE_ITEM ui_sources src/main.cpp)
list(TRANSFORM ui_sources PREPEND "${PROJECT_SOURCE_DIR}/")
set(ui_headers ${HEADERS})
list(TRANSFORM ui_headers PREPEND "${PROJECT_SOURCE_DIR}/")
add_executable(NekoMusicUiSmoke
    ui_smoke.cpp ${ui_sources} ${ui_headers}
    "${PROJECT_SOURCE_DIR}/src/resources/resources.qrc")
if(TARGET neko-glass-shaders)
    add_dependencies(NekoMusicUiSmoke neko-glass-shaders)
endif()
target_include_directories(NekoMusicUiSmoke PRIVATE
    $<TARGET_PROPERTY:NekoMusic,INCLUDE_DIRECTORIES>)
target_compile_definitions(NekoMusicUiSmoke PRIVATE
    $<TARGET_PROPERTY:NekoMusic,COMPILE_DEFINITIONS>)
target_link_libraries(NekoMusicUiSmoke PRIVATE
    $<TARGET_PROPERTY:NekoMusic,LINK_LIBRARIES> Qt6::Test)
add_test(NAME ui-smoke COMMAND NekoMusicUiSmoke)
set_tests_properties(ui-smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60)
