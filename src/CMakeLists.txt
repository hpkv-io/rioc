cmake_minimum_required(VERSION 3.10)
project(rioc C)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Enable LTO (Link Time Optimization)
include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)
if(supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Set optimization flags
if(APPLE)
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -fPIC -fvisibility=default")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -Wl,-exported_symbols_list,${CMAKE_SOURCE_DIR}/exported_symbols.txt")
    add_definitions(-DRIOC_PLATFORM_MACOS)
else()
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -flto -fPIC")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -flto -Wl,--version-script=${CMAKE_SOURCE_DIR}/exported_symbols.txt")
    add_definitions(-DRIOC_PLATFORM_LINUX)
endif()

# Platform detection
if(WIN32)
    set(PLATFORM_SOURCES rioc_platform_windows.c)
    set(PLATFORM_LIBS ws2_32 winmm)
    add_definitions(-DRIOC_PLATFORM_WINDOWS)
else()
    set(PLATFORM_SOURCES rioc_platform_unix.c)
    set(PLATFORM_LIBS pthread)
endif()

# Common sources (client-side, cross-platform)
set(COMMON_SOURCES
    rioc_client.c
    rioc_tls.c
    ${PLATFORM_SOURCES}
)

# Server sources (Linux-only)
if(UNIX AND NOT APPLE)
    set(SERVER_SOURCES
        rioc_server.c
    )
endif()

# Library sources
if(UNIX AND NOT APPLE)
    # Full sources for Linux
    set(RIOC_SOURCES
        ${COMMON_SOURCES}
        ${SERVER_SOURCES}
    )
else()
    # Client-only sources for other platforms
    set(RIOC_SOURCES
        ${COMMON_SOURCES}
    )
endif()

# Shared library
add_library(rioc SHARED ${RIOC_SOURCES})

# Static library
add_library(rioc_static STATIC ${RIOC_SOURCES})
set_target_properties(rioc_static PROPERTIES OUTPUT_NAME rioc)

# Set version for shared library
set_target_properties(rioc PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
)

if(NOT APPLE)
    set_target_properties(rioc PROPERTIES
        C_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

target_include_directories(rioc
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OPENSSL_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(rioc_static
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${OPENSSL_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rioc PRIVATE ${PLATFORM_LIBS} ${OPENSSL_LIBRARIES})
target_link_libraries(rioc_static PRIVATE ${PLATFORM_LIBS} ${OPENSSL_LIBRARIES})

# Server executable (Linux only)
if(UNIX AND NOT APPLE)
    add_executable(rioc_server rioc_server_main.c)
    target_link_libraries(rioc_server PRIVATE rioc_static ${PLATFORM_LIBS} ${OPENSSL_LIBRARIES})
endif()

# Test executable (cross-platform)
add_executable(rioc_test rioc_test.c)
target_link_libraries(rioc_test PRIVATE rioc_static ${PLATFORM_LIBS} ${OPENSSL_LIBRARIES})

# Benchmark executable (cross-platform)
add_executable(rioc_bench rioc_bench.c)
target_link_libraries(rioc_bench PRIVATE rioc_static ${PLATFORM_LIBS} ${OPENSSL_LIBRARIES})

# Installation
if(UNIX AND NOT APPLE)
    # Install all components on Linux
    install(TARGETS rioc rioc_static rioc_server rioc_test rioc_bench
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
else()
    # Install only client components on other platforms
    install(TARGETS rioc rioc_static rioc_test rioc_bench
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

install(FILES
    rioc.h
    rioc_platform.h
    DESTINATION include/rioc
) 

