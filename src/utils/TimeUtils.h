#ifndef TIMEUTILS_H
#define TIMEUTILS_H

#include <time.h>

// ============================================================================
// Time Configuration
// ============================================================================

// NTP Server
#define NTP_SERVER "pool.ntp.org"

// Timezone: CET/CEST (Prague)
#define GMT_OFFSET_SEC 3600      // CET = UTC+1
#define DAYLIGHT_OFFSET_SEC 3600 // CEST = UTC+2

// ============================================================================
// Time Synchronization Functions
// ============================================================================

/**
 * Initialize NTP time synchronization
 * Configures NTP client with Prague timezone (CET/CEST)
 */
void initTimeSync();

/**
 * Wait for NTP time synchronization to complete
 * @param maxAttempts Maximum number of sync attempts (default: 10)
 * @param delayMs Delay between attempts in milliseconds (default: 500)
 * @return true if sync succeeded, false if timeout
 */
bool syncTime(int maxAttempts = 10, int delayMs = 500);

/**
 * Get formatted time string
 * @param buffer Buffer to write formatted time string
 * @param size Size of buffer
 * @param format strftime format string (default: "%Y-%m-%d %H:%M:%S")
 * @return true if successful, false if time not set
 */
bool getFormattedTime(char* buffer, size_t size, const char* format = "%Y-%m-%d %H:%M:%S");

/**
 * Get current local time
 * @param timeinfo Pointer to tm struct to fill
 * @return true if successful, false if time not set
 */
bool getCurrentTime(struct tm* timeinfo);

#endif // TIMEUTILS_H
