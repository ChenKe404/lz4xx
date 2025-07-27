cmake_minimum_required(VERSION 3.10)

project(lz4++ VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ENABLE_TEST_LZ4XX "Enable lz4++ test target." OFF)
option(ENABLE_BIO_SUPPORT "Enable bio support." ON)
option(BUILD_STATIC_LZ4XX "Build lz4++ static library." ON)

add_subdirectory(lz4/build/cmake)

if(MSVC)
    add_compile_options(/utf-8)
endif()
if("${CMAKE_BUILD_TYPE}" STREQUAL Debug)
    add_definitions(-D_DEBUG)
endif()

configure_file(lz4xx.h.in ${CMAKE_CURRENT_SOURCE_DIR}/lz4xx.h)

add_library(lz4++ SHARED lz4xx.h lz4xx.cpp)
target_link_libraries(lz4++ PRIVATE lz4_static)

if(BUILD_STATIC_LZ4XX)
    add_library(lz4++_static STATIC lz4xx.h lz4xx.cpp)
    target_link_libraries(lz4++_static PRIVATE lz4_static)
endif()

if(ENABLE_TEST_LZ4XX)
    add_executable(test_lz4++ test.cpp)
    target_link_libraries(test_lz4++ PRIVATE lz4++)
    target_include_directories(test_lz4++ PUBLIC .)
endif()

# install header
install(
    FILES
        lz4xx.h
    DESTINATION ${CMAKE_INSTALL_PREFIX}/include
)
if(ENABLE_BIO_SUPPORT)
    install(
        FILES
            bio/bio.hpp
        DESTINATION ${CMAKE_INSTALL_PREFIX}/include/bio
    )
endif()

# install library
# shared install
install(
    TARGETS
    lz4++
    RUNTIME
    COMPONENT Runtime
    DESTINATION ${CMAKE_INSTALL_PREFIX}/bin
    ARCHIVE
    COMPONENT Development
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

# static install
if(BUILD_STATIC_LZ4XX)
    if(WIN32)
        add_custom_command(OUTPUT built-in.lib
            COMMAND ${CMAKE_AR} /OUT:built-in.lib lz4++_static.lib lz4/build/cmake/lz4.lib
            DEPENDS lz4++_static.lib lz4.lib
        )
        add_custom_target(built-in ALL DEPENDS built-in.lib)
        set(STATIC_FILE ${CMAKE_CURRENT_BINARY_DIR}/built-in.lib)
        set(STATIC_NAME lz4++_static.lib)
    else()
        add_custom_command(OUTPUT built-in.a
            COMMAND ${CMAKE_AR} rcsT built-in.a lz4++_static.a lz4/build/cmake/lz4.a
            DEPENDS lz4++_static.a lz4.a
        )
        add_custom_target(built-in ALL DEPENDS built-in.a)
        set(STATIC_FILE ${CMAKE_CURRENT_BINARY_DIR}/built-in.a)
        set(STATIC_NAME lz4++_static.a)
    endif()

    # static install
    install(
        FILES
            ${STATIC_FILE}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
        RENAME ${STATIC_NAME}
    )
endif()

set(Lz4++_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX})
# install config
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/Lz4++Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Lz4++Config.cmake
    INSTALL_DESTINATION lib/cmake/Lz4++Config.cmake
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/Lz4++ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/Lz4++Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/Lz4++ConfigVersion.cmake
    cmake/Lz4++Targets.cmake
    DESTINATION lib/cmake/Lz4++
)
