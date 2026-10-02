# cmake/HuePackage.cmake
# Explicit component keeps third-party SDK headers/tools out of the game package.
install(TARGETS hue_game RUNTIME DESTINATION . COMPONENT HueRuntime)
foreach(shader triangle.vert triangle.frag mesh.vert skinned_mesh.vert mesh.frag hud.vert hud.frag)
    install(FILES "${PROJECT_BINARY_DIR}/game/shaders/${shader}.spv"
        DESTINATION shaders COMPONENT HueRuntime)
endforeach()
foreach(model player.glb enemy.glb player.events.json enemy.events.json LICENSE-KayKit.txt README.md)
    install(FILES "${PROJECT_SOURCE_DIR}/assets/models/${model}"
        DESTINATION assets/models COMPONENT HueRuntime)
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/assets/config/arena.json" "${PROJECT_SOURCE_DIR}/assets/config/tuning.json"
    DESTINATION assets/config COMPONENT HueRuntime)
install(FILES "${PROJECT_SOURCE_DIR}/docs/package-readme.txt" DESTINATION .
    RENAME README.txt COMPONENT HueRuntime)
install(FILES "${PROJECT_SOURCE_DIR}/docs/third-party-notices.txt"
    DESTINATION . COMPONENT HueRuntime)

# CMake resolves the installed MSVC redistributable set. Package Release only;
# never ship the debug CRT or ASan runtime as part of the review distribution.
if(MSVC AND NOT HUE_SANITIZE)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(WARNING "MSVC runtime DLLs not found; package verification will reject missing CRT")
    endif()
    install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
        DESTINATION . COMPONENT HueRuntime)
endif()

function(hue_install_license dependency filename)
    FetchContent_GetProperties(${dependency})
    if(NOT EXISTS "${${dependency}_SOURCE_DIR}/${filename}")
        message(FATAL_ERROR "Missing ${dependency} notice: ${filename}")
    endif()
    install(FILES "${${dependency}_SOURCE_DIR}/${filename}"
        DESTINATION "licenses/${dependency}" COMPONENT HueRuntime)
endfunction()
hue_install_license(glfw LICENSE.md)
hue_install_license(tracy LICENSE)
# Preserve both Tracy notices (same basename in different source directories).
FetchContent_GetProperties(tracy)
install(FILES "${tracy_SOURCE_DIR}/public/libbacktrace/LICENSE"
    DESTINATION licenses/tracy/libbacktrace COMPONENT HueRuntime)
hue_install_license(cgltf LICENSE)
hue_install_license(stb LICENSE)
hue_install_license(joltphysics LICENSE)
hue_install_license(volk LICENSE.md)
hue_install_license(vma LICENSE.txt)
hue_install_license(vulkan-headers LICENSE.md)
FetchContent_GetProperties(vulkan-headers)
install(DIRECTORY "${vulkan-headers_SOURCE_DIR}/LICENSES/"
    DESTINATION licenses/vulkan-headers COMPONENT HueRuntime)
hue_install_license(spirv-headers LICENSE)
hue_install_license(spirv-tools LICENSE)
hue_install_license(glslang LICENSE.txt)
