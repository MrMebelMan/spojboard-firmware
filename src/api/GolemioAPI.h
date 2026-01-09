#ifndef GOLEMIOAPI_H
#define GOLEMIOAPI_H

#include "DepartureData.h"
#include "../config/AppConfig.h"
#include <ArduinoJson.h>

// ============================================================================
// Golemio API Client
// ============================================================================

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
     * Fetch departures from Golemio API
     * @param config Configuration with API key, stop IDs, and filters
     * @return APIResult with departures, count, and error status
     */
    APIResult fetchDepartures(const Config& config);

private:
    static constexpr int MAX_TEMP_DEPARTURES = 30;
    static constexpr int JSON_BUFFER_SIZE = 8192;
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
    bool querySingleStop(const char* stopId, const Config& config,
                        Departure* tempDepartures, int& tempCount,
                        char* stopName, bool& isFirstStop);

    /**
     * Parse departure JSON object and add to temp array
     * @param depJson JSON object for single departure
     * @param tempDepartures Array to add to
     * @param tempCount Current count (will be incremented)
     */
    void parseDepartureObject(JsonObject depJson, Departure* tempDepartures, int& tempCount);
};

#endif // GOLEMIOAPI_H
