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

#endif // LOGGER_H
