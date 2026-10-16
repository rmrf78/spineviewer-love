include_guard(GLOBAL)

set(SPINELOVE_CUBISM_ROOT "${PROJECT_SOURCE_DIR}/third_party/live2d")

# ---------------------------------------------------------------------------
# Qt bridge. This target always exists so the viewer can report "backend
# unavailable" on platforms where the Cubism renderer is not compiled in.
# ---------------------------------------------------------------------------
add_library(spinelove_qt_live2d STATIC
    "${PROJECT_SOURCE_DIR}/main/qt/core/live2d_bridge.cpp")
target_compile_features(spinelove_qt_live2d PUBLIC cxx_std_17)
target_link_libraries(spinelove_qt_live2d PUBLIC Qt6::Core Qt6::Qml)
target_include_directories(spinelove_qt_live2d PUBLIC "${PROJECT_SOURCE_DIR}/main/qt/core")
set_target_properties(spinelove_qt_live2d PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(SpineLove::QtLive2D ALIAS spinelove_qt_live2d)

# Collects the platform independent Cubism Framework translation units plus the
# ones for a single rendering backend. `renderer_dir` is relative to
# Framework/src (for example "Rendering/D3D11" or "Rendering/Vulkan").
function(spinelove_cubism_framework_sources out_var renderer_dir)
    set(directories "" Effect Id Math Model Motion Physics Rendering "${renderer_dir}" Type Utils)
    set(sources)
    foreach(directory IN LISTS directories)
        file(GLOB group CONFIGURE_DEPENDS "${SPINELOVE_CUBISM_ROOT}/Framework/src/${directory}/*.cpp")
        list(APPEND sources ${group})
    endforeach()
    set(${out_var} ${sources} PARENT_SCOPE)
endfunction()

# Compiles the Cubism Vulkan shaders that the renderer loads from
# "FrameworkShaders/<name>.spv" at runtime. Returns the generated file list.
function(spinelove_cubism_vulkan_shaders out_var)
    find_program(SPINELOVE_GLSLANG_VALIDATOR NAMES glslangValidator glslang)
    if(NOT SPINELOVE_GLSLANG_VALIDATOR)
        message(FATAL_ERROR
            "glslangValidator is required to build the Cubism Vulkan shaders. "
            "Install glslang (Debian/Ubuntu: glslang-tools).")
    endif()

    set(shader_source_dir "${SPINELOVE_CUBISM_ROOT}/Framework/src/Rendering/Vulkan/Shaders/src")
    set(shader_output_dir "${CMAKE_BINARY_DIR}/FrameworkShaders")
    file(MAKE_DIRECTORY "${shader_output_dir}")

    file(GLOB shader_sources CONFIGURE_DEPENDS
        "${shader_source_dir}/*.vert" "${shader_source_dir}/*.frag")
    set(outputs)
    foreach(shader IN LISTS shader_sources)
        get_filename_component(name "${shader}" NAME_WE)
        set(output "${shader_output_dir}/${name}.spv")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${SPINELOVE_GLSLANG_VALIDATOR}" -V "${shader}"
                    -I"${shader_source_dir}" -o "${output}"
            DEPENDS "${shader}" "${shader_source_dir}/common.glsl"
            COMMENT "Compiling Cubism Vulkan shader ${name}.spv"
            VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()

    add_custom_target(spinelove_live2d_shaders ALL DEPENDS ${outputs})
    set(${out_var} ${outputs} PARENT_SCOPE)
endfunction()

if(WIN32)
    if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The bundled Live2D Core requires the MSVC x64 ABI; provide a matching Core before using another Windows toolchain.")
    endif()
    spinelove_cubism_framework_sources(cubism_sources "Rendering/D3D11")
    set(cubism_root "${SPINELOVE_CUBISM_ROOT}")
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
elseif(ANDROID)
    # Termux / Android: Bionic, OpenGL renderer (the Android target has no
    # Vulkan Qt backend in Termux's x11-packages).
    message(STATUS "Live2D: Android/Bionic backend is not wired up yet.")
elseif(UNIX)
    spinelove_cubism_framework_sources(cubism_sources "Rendering/Vulkan")
    add_library(spinelove_live2d_cubism STATIC ${cubism_sources})
    target_compile_features(spinelove_live2d_cubism PUBLIC cxx_std_17)
    set_target_properties(spinelove_live2d_cubism PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(spinelove_live2d_cubism PUBLIC
        "${SPINELOVE_CUBISM_ROOT}/Framework/src"
        "${SPINELOVE_CUBISM_ROOT}/Core/include")

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(SPINELOVE_CUBISM_CORE_LIB
            "${SPINELOVE_CUBISM_ROOT}/Core/lib/experimental/linux/arm64/libLive2DCubismCore.a")
    else()
        set(SPINELOVE_CUBISM_CORE_LIB
            "${SPINELOVE_CUBISM_ROOT}/Core/lib/linux/x86_64/libLive2DCubismCore.a")
    endif()
    if(NOT EXISTS "${SPINELOVE_CUBISM_CORE_LIB}")
        message(FATAL_ERROR "Bundled Live2D Core not found at ${SPINELOVE_CUBISM_CORE_LIB}")
    endif()
    target_link_libraries(spinelove_live2d_cubism PUBLIC "${SPINELOVE_CUBISM_CORE_LIB}")

    find_package(Vulkan REQUIRED)
    target_link_libraries(spinelove_live2d_cubism PUBLIC Vulkan::Vulkan)

    spinelove_cubism_vulkan_shaders(SPINELOVE_CUBISM_VULKAN_SHADERS)

    add_library(spinelove_live2d_vulkan STATIC
        "${PROJECT_SOURCE_DIR}/main/live2d/live2d_module.cpp"
        "${PROJECT_SOURCE_DIR}/main/render_vulkan/vk_renderer.cpp"
        "${PROJECT_SOURCE_DIR}/main/audio/model_audio_stub.cpp"
        "${PROJECT_SOURCE_DIR}/main/sl_text_codec.cpp")
    target_compile_features(spinelove_live2d_vulkan PUBLIC cxx_std_17)
    set_target_properties(spinelove_live2d_vulkan PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(spinelove_live2d_vulkan
        PUBLIC "${PROJECT_SOURCE_DIR}/main"
        PRIVATE "${PROJECT_SOURCE_DIR}/third_party/stb")
    target_link_libraries(spinelove_live2d_vulkan PUBLIC spinelove_live2d_cubism)
    # The Qt bridge needs the module, and static link order matters.
    target_link_libraries(spinelove_qt_live2d PUBLIC spinelove_live2d_vulkan)

    option(SPINELOVE_BUILD_LIVE2D_SMOKE "Build the offscreen Cubism Vulkan smoke test" OFF)
    if(SPINELOVE_BUILD_LIVE2D_SMOKE)
        add_executable(spinelove_live2d_smoke "${PROJECT_SOURCE_DIR}/tools/live2d_vk_smoke/main.cpp")
        target_compile_features(spinelove_live2d_smoke PRIVATE cxx_std_17)
        target_include_directories(spinelove_live2d_smoke PRIVATE "${PROJECT_SOURCE_DIR}/third_party/stb")
        target_link_libraries(spinelove_live2d_smoke PRIVATE spinelove_live2d_cubism spinelove_live2d_vulkan)
        add_dependencies(spinelove_live2d_smoke spinelove_live2d_shaders)
        add_custom_command(TARGET spinelove_live2d_smoke POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:spinelove_live2d_smoke>/FrameworkShaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SPINELOVE_CUBISM_VULKAN_SHADERS}
                    "$<TARGET_FILE_DIR:spinelove_live2d_smoke>/FrameworkShaders")
    endif()
endif()
