cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)

project(veo-stubs)

include_directories(include)

# dependencies
find_package(PkgConfig)
pkg_check_modules(LIBFFI REQUIRED libffi)
add_subdirectory(thirdparty/json EXCLUDE_FROM_ALL)
add_subdirectory(thirdparty/spdlog EXCLUDE_FROM_ALL)
set_property(TARGET spdlog PROPERTY POSITION_INDEPENDENT_CODE ON)
add_subdirectory(thirdparty/doctest EXCLUDE_FROM_ALL)

# stub-veorun
add_executable(stub-veorun src/stub_veorun.cpp)
target_include_directories(stub-veorun PRIVATE ${LIBFFI_INCLUDE_DIRS})
target_link_libraries(stub-veorun PRIVATE ${LIBFFI_LIBRARIES})
target_link_libraries(stub-veorun PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(stub-veorun PRIVATE spdlog::spdlog)
target_link_libraries(stub-veorun PRIVATE dl)

# libveo
add_library(veo SHARED src/libveo.cpp)
set_target_properties(veo PROPERTIES SUFFIX ".so")
set_target_properties(veo PROPERTIES PUBLIC_HEADER "include/ve_offload.h;include/veo_hmem.h")
target_link_libraries(veo PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(veo PRIVATE spdlog::spdlog)

# installation rules
install(TARGETS stub-veorun)
install(TARGETS veo)

# tests
add_executable(veo-test test/test.cpp)
target_link_libraries(veo-test PRIVATE veo)
target_link_libraries(veo-test PRIVATE doctest::doctest)

add_library(vetest SHARED test/libvetest.c include/ve_offload.h)
set_target_properties(vetest PROPERTIES SUFFIX ".so")

enable_testing()
include(thirdparty/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(veo-test)
