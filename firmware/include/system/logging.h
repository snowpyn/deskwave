#pragma once

#include <Arduino.h>

#define DW_LOG_ERROR(component, format, ...) \
    Serial.printf("[ERROR][%s] " format "\n", component, ##__VA_ARGS__)
#define DW_LOG_WARN(component, format, ...) \
    Serial.printf("[WARN][%s] " format "\n", component, ##__VA_ARGS__)
#define DW_LOG_INFO(component, format, ...) \
    Serial.printf("[INFO][%s] " format "\n", component, ##__VA_ARGS__)

#ifdef DESKWAVE_DEBUG
#define DW_LOG_DEBUG(component, format, ...) \
    Serial.printf("[DEBUG][%s] " format "\n", component, ##__VA_ARGS__)
#else
#define DW_LOG_DEBUG(component, format, ...) \
    do {                                      \
    } while (false)
#endif
