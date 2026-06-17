/**
 * weather_bridge.h — ESP32 UART 天气帧解析
 *
 * 解析格式: $YYYY-MM-DD HH:MM:SS|temp|hum|desc|XX\n
 */

#ifndef WEATHER_BRIDGE_H
#define WEATHER_BRIDGE_H

#include <stdint.h>
#include <stdbool.h>

void weather_bridge_poll_dma(void);
void weather_frame_parse(const char *frame);

#endif /* WEATHER_BRIDGE_H */
