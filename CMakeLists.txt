if (NOT TARGET pico_fatfs)

    add_subdirectory(pio/spi pio_spi)

    add_library(pico_fatfs INTERFACE)

    target_sources(pico_fatfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/fatfs/ff.c
        ${CMAKE_CURRENT_LIST_DIR}/fatfs/ffsystem.c
        ${CMAKE_CURRENT_LIST_DIR}/fatfs/ffunicode.c
        ${CMAKE_CURRENT_LIST_DIR}/tf_card.c
    )

    target_include_directories(pico_fatfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/fatfs
        ${CMAKE_CURRENT_LIST_DIR}/fatfs/conf
    )

    target_link_libraries(pico_fatfs INTERFACE
        pico_stdlib
        hardware_clocks
        hardware_spi
        pio_spi
    )
endif()
