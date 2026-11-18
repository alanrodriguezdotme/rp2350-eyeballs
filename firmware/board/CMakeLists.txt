# Board support for the Waveshare RP2350-Touch-AMOLED-1.8.
# Everything that knows about pins, the panel and the sensors lives here.
add_library(board
  amoled/AMOLED_1in8.c
  qspi_pio/qspi_pio.c
  config/DEV_Config.c
  qmi8658/qmi8658.c
  touch/FT3168.c
  pwr_button/pwr_button.c
  pcf85063/pcf85063.c
  gui/GUI_Paint.c
  fonts/font8.c fonts/font12.c fonts/font16.c fonts/font20.c fonts/font24.c
)
pico_generate_pio_header(board ${CMAKE_CURRENT_LIST_DIR}/qspi_pio/qspi.pio)
target_include_directories(board PUBLIC
  amoled qspi_pio config qmi8658 touch pwr_button pcf85063 gui fonts)
target_link_libraries(board PUBLIC
  pico_stdlib pico_sync hardware_pio hardware_dma hardware_i2c hardware_spi
  hardware_pwm hardware_adc hardware_irq hardware_gpio)
target_compile_options(board PRIVATE -w)
