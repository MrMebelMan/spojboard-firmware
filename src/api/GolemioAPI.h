#ifndef GOLEMIOAPI_H
#define GOLEMIOAPI_H

#include "DepartureData.h"
#include "../config/AppConfig.h"
#include <ArduinoJson.h>

// ============================================================================
// Golemio API Client
// ============================================================================

/**
 * Callback for API status updates (e.g., retry attempts)
 * @param message Status message to display
 */
typedef void (*APIStatusCallback)(const char* message);

/**
 * Client for Prague's Golemio API (api.golemio.cz)
 * Fetches real-time departure information for public transit stops.
 */
class GolemioAPI
{
public:
    struct APIResult
    {
        Departure departures[MAX_DEPARTURES];
        int departureCount;
        char stopName[64];
        bool hasError;
        char errorMsg[64];
    };

    GolemioAPI();

    /**
     * Set callback for API status updates
     * @param callback Function to call with status messages
     */
    void setStatusCallback(APIStatusCallback callback);

    /**
     * Fetch departures from Golemio API
     * @param config Configuration with API key, stop IDs, and filters
     * @return APIResult with departures, count, and error status
     */
    APIResult fetchDepartures(const Config &config);

private:
    APIStatusCallback statusCallback;
    static constexpr int MAX_TEMP_DEPARTURES = MAX_DEPARTURES * 12;  // Buffer for up to 12 stops at full capacity
    static constexpr int JSON_BUFFER_SIZE = 12288;  // 12KB - handles busy stops with many departures
    static constexpr int HTTP_TIMEOUT_MS = 10000;

    /**
     * Query a single stop and add results to temp array
     * @param stopId Stop ID to query
     * @param config Configuration
     * @param tempDepartures Temporary array to fill
     * @param tempCount Current count in temp array
     * @param stopName Output: stop name (from first stop only)
     * @param isFirstStop Whether this is the first stop being queried
     * @return true if query succeeded
     */
    bool querySingleStop(const char *stopId, const Config &config,
                         Departure *tempDepartures, int &tempCount,
                         char *stopName, bool &isFirstStop);

    /**
     * Parse departure JSON object and add to temp array
     * @param depJson JSON object for single departure
     * @param tempDepartures Array to add to
     * @param tempCount Current count (will be incremented)
     */
    void parseDepartureObject(JsonObject depJson, Departure *tempDepartures, int &tempCount);
};

#endif // GOLEMIOAPI_H
