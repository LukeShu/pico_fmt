# Copyright (c) 2020  Raspberry Pi (Trading) Ltd.
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2025  Luke T. Shumaker
# SPDX-License-Identifier: BSD-3-Clause

if (NOT TARGET pico_fmt)
    pico_add_library(pico_fmt)

    target_include_directories(pico_fmt_headers SYSTEM INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

    target_sources(pico_fmt INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/printf.c
    )
    target_link_libraries(pico_fmt INTERFACE pico_fmt_headers)
endif()
