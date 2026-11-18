add_library(eye_screen eye_screen.c)
target_include_directories(eye_screen PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(eye_screen PUBLIC board eye pico_stdlib pico_multicore)
