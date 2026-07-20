idf_component_register(
    SRCS "boombox_audio.c" "boombox_audio_i2s.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES boombox_board
    PRIV_REQUIRES bt nvs_flash esp_driver_i2s esp_ringbuf
)
