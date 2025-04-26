cmake_minimum_required(VERSION 3.12)
if (NOT TARGET pio_spi)
    add_library(pio_spi INTERFACE)

    pico_generate_pio_header(pio_spi ${CMAKE_CURRENT_LIST_DIR}/spi.pio)

    target_sources(pio_spi INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/pio_spi.c
    )

    target_include_directories(pio_spi INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
    )

    target_link_libraries(pio_spi INTERFACE
        pico_stdlib
        hardware_pio
    )
endif()
