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
