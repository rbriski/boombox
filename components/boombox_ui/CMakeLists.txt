idf_component_register(
    SRCS "boombox_ui.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES boombox_board
    PRIV_REQUIRES driver esp_lcd esp_driver_ledc
)
