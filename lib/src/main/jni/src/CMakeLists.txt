cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_VERBOSE_MAKEFILE on)
set(LOCAL_PATH ${CMAKE_CURRENT_SOURCE_DIR})
set(distribution_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../distribution)
set(UNICORN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/unicorn)

include_directories(
        .
        lib
        ${UNICORN_DIR}/include
)

# Unicorn Engine prebuilt shared library
add_library(unicorn SHARED IMPORTED)
set_target_properties(unicorn PROPERTIES IMPORTED_LOCATION
        ${UNICORN_DIR}/lib/${ANDROID_ABI}/libunicorn.so)

add_library(
        mrpoid
        SHARED

        emulator.c
        dsm.c
        network.c
        utils.c
        encode.c
        register_natives.c
        font/font_sky16_2.c
        lib/TimeUtils.c
        lib/JniUtils.c
        # Unicorn VM integration
        uc_vm.c
        uc_bridge.c
        uc_memory.c
        uc_fileLib.c
        rbtree.c
)

target_include_directories(mrpoid PRIVATE
        ${distribution_DIR}/mr_pre/include)

target_link_libraries(
        mrpoid
        log
        jnigraphics
        m
        c
        z
        unicorn
)
