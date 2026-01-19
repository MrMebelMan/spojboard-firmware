#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>

// ============================================================================
// Debug Logging Utilities
// ============================================================================

/**
 * Print timestamp in format [milliseconds]
 */
void logTimestamp();

/**
 * Log memory usage with location label
 * @param location Label for this memory checkpoint
 */
void logMemory(const char* location);

/**
 * Initialize logger with config reference
 * Must be called before using conditional logging functions
 * @param cfg Pointer to config structure
 */
void initLogger(const struct Config* cfg);

/**
 * Print message to Serial and telnet (if debug enabled)
 * @param message Message to print
 */
void debugPrint(const char* message);

/**
 * Print message with newline to Serial and telnet (if debug enabled)
 * @param message Message to print
 */
void debugPrintln(const char* message);

/**
 * Convert HTTP/ESP32 error code to human-readable string
 * @param httpCode HTTP status code or negative ESP32 error code
 * @return Static string describing the error
 */
const char* httpErrorToString(int httpCode);

/**
 * Log network diagnostics: WiFi status, RSSI, and heap memory
 * Useful for debugging connection issues
 */
void logNetworkDiagnostics();

#endif // LOGGER_H
