cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(veo-stubs VERSION 0.1.0)
include_directories(include)

# Dependencies
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

# Installation rules
set(CMAKE_INSTALL_LIBDIR lib64)
install(TARGETS stub-veorun DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
install(TARGETS veo LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# CPack
set(CPACK_PACKAGING_INSTALL_PREFIX /opt/nec/ve/veos)
set(CPACK_PACKAGE_CONTACT "Keichi Takahashi <hello@keichi.dev>")
set(CPACK_PACKAGE_DESCRIPTION "VE Offloading API stubs")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libffi-dev")
set(CPACK_RPM_PACKAGE_REQUIRES "libffi-devel")
include(CPack)

# Tests
add_executable(veo-test test/test.cpp)
target_link_libraries(veo-test PRIVATE veo)
target_link_libraries(veo-test PRIVATE doctest::doctest)

add_library(vetest SHARED test/libvetest.c include/ve_offload.h)
set_target_properties(vetest PROPERTIES SUFFIX ".so")

enable_testing()
include(thirdparty/doctest/scripts/cmake/doctest.cmake)
doctest_discover_tests(veo-test PROPERTIES ENVIRONMENT "SPDLOG_LEVEL=debug"
                       ENVIRONMENT "VEORUN_BIN=./stub-veorun")
