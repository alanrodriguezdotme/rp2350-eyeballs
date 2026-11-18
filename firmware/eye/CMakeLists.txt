# The eye renderer and the built-in eye styles. Portable C; no board code.
file(GLOB STYLE_SOURCES ${CMAKE_CURRENT_LIST_DIR}/styles/*.c)
add_library(eye eye.c behave.c motion.c ${STYLE_SOURCES})
target_include_directories(eye PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(eye PUBLIC pico_stdlib)
target_compile_options(eye PRIVATE -O3)
