/**
 * @file led_driver.h
 * @brief Hardware driver interface for the on-board addressable RGB LED.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Initialize the WS2812 driver and register it as the LED output sink.
     *
     * Creates the RMT TX channel and copy encoder, enables the channel, and calls
     * led_rgb_set_sink() so that led_rgb_render() drives the physical LED.
     *
     * @param gpio_num  GPIO connected to the LED data input.
     * @return ESP_OK on success, otherwise an error from the RMT driver.
     */
    esp_err_t led_driver_init (int gpio_num);

#ifdef __cplusplus
}
#endif
