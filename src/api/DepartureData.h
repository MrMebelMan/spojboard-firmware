#ifndef DEPARTUREDATA_H
#define DEPARTUREDATA_H

// ============================================================================
// Departure Data Structures
// ============================================================================

#define MAX_DEPARTURES 6

struct Departure
{
    char line[8];         // Line number (e.g., "31", "A", "S9")
    char destination[32]; // Destination/headsign
    int eta;              // Minutes until departure
    bool hasAC;           // Air conditioning
    bool isDelayed;       // Has delay
    int delayMinutes;     // Delay in minutes
};

// ============================================================================
// Departure Processing Functions
// ============================================================================

/**
 * Comparison function for qsort - sorts departures by ETA ascending
 * @param a First departure
 * @param b Second departure
 * @return Negative if a < b, positive if a > b, zero if equal
 */
int compareDepartures(const void* a, const void* b);

/**
 * Shorten long destination names to fit on display
 * Modifies the string in-place
 * @param destination UTF-8 string to shorten (will be modified in-place)
 */
void shortenDestination(char* destination);

#endif // DEPARTUREDATA_H
