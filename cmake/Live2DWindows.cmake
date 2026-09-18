include_guard(GLOBAL)

add_library(spinelove_qt_live2d STATIC
    "${PROJECT_SOURCE_DIR}/main/qt/core/live2d_bridge.cpp")
target_compile_features(spinelove_qt_live2d PUBLIC cxx_std_17)
target_link_libraries(spinelove_qt_live2d PUBLIC Qt6::Core Qt6::Qml)
target_include_directories(spinelove_qt_live2d PUBLIC "${PROJECT_SOURCE_DIR}/main/qt/core")
set_target_properties(spinelove_qt_live2d PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(SpineLove::QtLive2D ALIAS spinelove_qt_live2d)

if(WIN32)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The bundled Live2D Core requires the MSVC x64 ABI; provide a matching Core before using another Windows toolchain.")
    endif()
    set(cubism_root "${PROJECT_SOURCE_DIR}/third_party/live2d")
    set(cubism_sources)
    foreach(directory "" Effect Id Math Model Motion Physics Rendering Rendering/D3D11 Type Utils)
        file(GLOB group CONFIGURE_DEPENDS "${cubism_root}/Framework/src/${directory}/*.cpp")
        list(APPEND cubism_sources ${group})
    endforeach()
    add_library(spinelove_live2d_windows STATIC ${cubism_sources}
        "${PROJECT_SOURCE_DIR}/main/live2d/live2d_module.cpp"
        "${PROJECT_SOURCE_DIR}/main/audio/model_audio.cpp"
        "${PROJECT_SOURCE_DIR}/main/render_d3d11/d3d11_renderer.cpp"
        "${PROJECT_SOURCE_DIR}/main/render_d3d11/d3d11_texture.cpp"
        "${PROJECT_SOURCE_DIR}/main/sl_text_codec.cpp")
    target_compile_features(spinelove_live2d_windows PUBLIC cxx_std_17)
    target_compile_options(spinelove_live2d_windows PRIVATE /utf-8)
    target_compile_definitions(spinelove_live2d_windows PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_include_directories(spinelove_live2d_windows PRIVATE
        "${PROJECT_SOURCE_DIR}/main" "${PROJECT_SOURCE_DIR}/third_party/stb"
        "${cubism_root}/Framework/src" "${cubism_root}/Core/include")
    target_link_libraries(spinelove_live2d_windows PUBLIC
        d3d11 dxgi d3dcompiler windowscodecs ole32 xaudio2 mfplat mfreadwrite mfuuid winmm
        "$<$<CONFIG:Debug>:${cubism_root}/Core/lib/windows/x86_64/143/Live2DCubismCore_MDd.lib>"
        "$<$<NOT:$<CONFIG:Debug>>:${cubism_root}/Core/lib/windows/x86_64/143/Live2DCubismCore_MD.lib>")
    target_compile_options(spinelove_qt_live2d PRIVATE /utf-8)
    target_compile_definitions(spinelove_qt_live2d PRIVATE NOMINMAX)
    target_link_libraries(spinelove_qt_live2d PUBLIC spinelove_live2d_windows)

endif()
