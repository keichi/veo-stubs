cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)

project(veo-stubs)

find_package(PkgConfig)

include_directories(include)

pkg_check_modules(LIBFFI REQUIRED libffi)
add_subdirectory(thirdparty/json)
add_subdirectory(thirdparty/readerwriterqueue)

add_executable(stub-veorun src/stub_veorun.cpp)
target_include_directories(stub-veorun PRIVATE ${LIBFFI_INCLUDE_DIRS})
target_link_libraries(stub-veorun PRIVATE ${LIBFFI_LIBRARIES})
target_link_libraries(stub-veorun PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(stub-veorun PRIVATE readerwriterqueue)

add_library(veo SHARED src/libveo.cpp)
target_link_libraries(veo PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(veo PRIVATE readerwriterqueue)

add_executable(hello test/hello.c)
target_link_libraries(hello PRIVATE veo)
add_library(vehello SHARED test/libvehello.c include/ve_offload.h)
