#ifndef PLATFORM_H
#define PLATFORM_H

// ============================================================================
// Platform Detection and Abstraction
// ============================================================================
// This header provides conditional compilation for ESP32-S3 and Matrix Portal M4

#if defined(MATRIX_PORTAL_M4)
    // ========================================================================
    // Adafruit Matrix Portal M4 (SAMD51 + ESP32 WiFi coprocessor)
    // ========================================================================
    #define PLATFORM_M4

    // WiFi via ESP32 coprocessor (SPI)
    #include <WiFiNINA.h>
    #include <WiFiUdp.h>

    // Storage via flash emulation
    #include <FlashStorage_SAMD.h>

    // Display via Protomatter
    #include <Adafruit_Protomatter.h>

    // HTTP client for M4
    #include <ArduinoHttpClient.h>

    // System functions
    #define SYSTEM_RESTART() NVIC_SystemReset()
    #define GET_FREE_HEAP() 0  // Not easily available on SAMD51

    // Random number generation
    #define RANDOM_SEED() randomSeed(analogRead(A0))
    #define GET_RANDOM() random(0xFFFFFFFF)

#else
    // ========================================================================
    // Adafruit Matrix Portal ESP32-S3
    // ========================================================================
    #define PLATFORM_ESP32

    // Native WiFi
    #include <WiFi.h>
    #include <esp_random.h>

    // NVS storage
    #include <Preferences.h>

    // Display via I2S DMA
    #include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

    // HTTP client for ESP32
    #include <HTTPClient.h>

    // System functions
    #define SYSTEM_RESTART() ESP.restart()
    #define GET_FREE_HEAP() ESP.getFreeHeap()

    // Random number generation
    #define RANDOM_SEED() randomSeed(esp_random())
    #define GET_RANDOM() esp_random()

#endif

// ============================================================================
// Common Includes
// ============================================================================
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <ArduinoJson.h>

#endif // PLATFORM_H
