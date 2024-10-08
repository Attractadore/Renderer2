find_package(assimp REQUIRED)
find_package(Boost REQUIRED)
find_package(SDL2 REQUIRED)
find_package(cxxopts REQUIRED)
find_package(fmt REQUIRED)
find_package(glm REQUIRED)
find_package(imgui REQUIRED)

find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h" REQUIRED)
add_library(tinygltf tinygltf.cpp)
target_include_directories(tinygltf PUBLIC ${TINYGLTF_INCLUDE_DIRS})
target_compile_definitions(
  tinygltf PUBLIC TINYGLTF_USE_CPP14 TINYGLTF_NO_STB_IMAGE_WRITE
                  TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE)

function(copy_dlls target)
  if(WIN32)
    # copy will fail if target doesn't depend on any dlls
    set(dummy_dll ${CMAKE_CURRENT_BINARY_DIR}/dummy-dll)
    file(TOUCH ${dummy_dll})
    add_custom_command(
      TARGET ${target}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${dummy_dll}
              $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
      COMMAND_EXPAND_LISTS)
  endif()
endfunction()

add_library(app-base AppBase.cpp)
target_link_libraries(app-base PUBLIC ren::sdl2 fmt::fmt)
target_compile_definitions(app-base PUBLIC SDL_MAIN_HANDLED)

add_library(imgui-app ImGuiApp.cpp)
target_link_libraries(
  imgui-app
  PUBLIC app-base imgui::imgui
  PRIVATE ren::imgui)

add_executable(create-device create-device.cpp)
target_link_libraries(create-device app-base)
copy_dlls(create-device)

add_executable(draw-triangle draw-triangle.cpp)
target_link_libraries(draw-triangle app-base)
copy_dlls(draw-triangle)

add_executable(view-gltf view-gltf.cpp)
target_link_libraries(
  view-gltf
  imgui-app
  Boost::boost
  cxxopts::cxxopts
  tinygltf)
copy_dlls(view-gltf)

add_executable(entity-stress-test entity-stress-test.cpp)
target_link_libraries(entity-stress-test imgui-app assimp::assimp cxxopts::cxxopts)
