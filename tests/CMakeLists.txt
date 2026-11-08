find_package(Qt6 REQUIRED COMPONENTS Test)

# 复用生产代码与资源，验证真实控件。测试入口不执行单实例、协议注册或用户协议流程。
set(ui_sources ${SOURCES})
list(REMOVE_ITEM ui_sources src/main.cpp)
list(TRANSFORM ui_sources PREPEND "${PROJECT_SOURCE_DIR}/")
set(ui_headers ${HEADERS})
list(TRANSFORM ui_headers PREPEND "${PROJECT_SOURCE_DIR}/")
add_executable(NekoMusicUiSmoke
    ui_smoke.cpp ${ui_sources} ${ui_headers}
    "${PROJECT_SOURCE_DIR}/src/resources/resources.qrc")
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
