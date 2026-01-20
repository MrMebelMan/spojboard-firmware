#include "WeatherAPI.h"
#include "../utils/Logger.h"
#include <ArduinoJson.h>

// Platform-specific HTTP client includes
#if defined(MATRIX_PORTAL_M4)
    #include <WiFiNINA.h>
    #include <ArduinoHttpClient.h>
#else
    #include <HTTPClient.h>
#endif

WeatherAPI::WeatherAPI()
{
}

WeatherData WeatherAPI::fetchWeather(float latitude, float longitude)
{
    WeatherData result = {};
    result.hasError = false;
    result.temperature = 0;
    result.weatherCode = 0;
    time(&result.timestamp);

    logTimestamp();
    debugPrintln("Weather: Starting fetch...");
    logMemory("weather_start");

    // Validate coordinates
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "Invalid coordinates", sizeof(result.errorMsg));
        logTimestamp();
        debugPrintln("Weather: Invalid coordinates provided");
        return result;
    }

    // Build API path
    char path[256];
    snprintf(path, sizeof(path),
             "/v1/forecast?latitude=%.4f&longitude=%.4f&hourly=temperature_2m,weathercode&forecast_hours=3&timezone=auto",
             latitude, longitude);

    logTimestamp();
    debugPrint("Weather: Path: ");
    debugPrintln(path);

    int httpCode = -1;
    String payload;

#if defined(MATRIX_PORTAL_M4)
    // M4 uses ArduinoHttpClient with plain HTTP (WiFiNINA SSL to open-meteo times out)
    WiFiClient client;
    HttpClient http(client, "api.open-meteo.com", 80);
    http.setTimeout(HTTP_TIMEOUT_MS);

    logTimestamp();
    debugPrintln("Weather: Starting HTTP request (M4)...");

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++)
    {
        if (attempt > 1)
        {
            int delayMs = 2000 * (attempt - 1);
            logTimestamp();
            char retryMsg[64];
            snprintf(retryMsg, sizeof(retryMsg), "Weather: Retry %d/%d after %dms", attempt, MAX_RETRIES, delayMs);
            debugPrintln(retryMsg);
            delay(delayMs);
        }

        logTimestamp();
        debugPrintln("Weather: Sending request...");

        http.beginRequest();
        http.get(path);
        http.endRequest();

        logTimestamp();
        debugPrintln("Weather: Waiting for response...");

        httpCode = http.responseStatusCode();

        logTimestamp();
        char httpMsg[64];
        snprintf(httpMsg, sizeof(httpMsg), "Weather: HTTP response code: %d", httpCode);
        debugPrintln(httpMsg);

        if (httpCode == 200)
        {
            logTimestamp();
            debugPrintln("Weather: Reading response body...");

            // Get content length and pre-allocate
            int contentLen = http.contentLength();
            if (contentLen > 0 && contentLen < JSON_BUFFER_SIZE)
            {
                payload.reserve(contentLen + 1);
            }

            // Read in chunks (much faster than responseBody()'s character-by-character)
            char buffer[512];
            while (http.available())
            {
                int bytesRead = http.read((uint8_t *)buffer, sizeof(buffer) - 1);
                if (bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    payload += buffer;
                }
            }

            // Close connection to free socket
            http.stop();

            logTimestamp();
            char bodyMsg[64];
            snprintf(bodyMsg, sizeof(bodyMsg), "Weather: Response body length: %d", payload.length());
            debugPrintln(bodyMsg);
            break;
        }

        // Close connection before retry to free socket
        http.stop();

        // Don't retry on 4xx client errors
        if (httpCode >= 400 && httpCode < 500)
        {
            break;
        }
    }

    // Ensure connection is closed after all attempts
    http.stop();

#else
    // ESP32 uses native HTTPClient
    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url), "https://api.open-meteo.com%s", path);

    // Retry logic (up to MAX_RETRIES attempts)
    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++)
    {
        if (attempt > 1)
        {
            int delayMs = 2000 * (attempt - 1); // 2s, 4s...
            logTimestamp();
            debugPrint("Weather: Retry ");
            debugPrint(String(attempt - 1).c_str());
            debugPrint("/");
            debugPrintln(String(MAX_RETRIES - 1).c_str());
            delay(delayMs);
        }

        http.begin(url);
        http.setTimeout(HTTP_TIMEOUT_MS);

        logTimestamp();
        debugPrintln("Weather: Sending HTTP GET...");

        httpCode = http.GET();

        logTimestamp();
        debugPrint("Weather: HTTP code: ");
        debugPrintln(String(httpCode).c_str());

        if (httpCode == HTTP_CODE_OK)
        {
            payload = http.getString();
            http.end();
            break;
        }

        http.end();

        // Don't retry on 4xx client errors
        if (httpCode >= 400 && httpCode < 500)
        {
            break;
        }
    }
#endif

#if defined(MATRIX_PORTAL_M4)
    if (httpCode != 200)
#else
    if (httpCode != HTTP_CODE_OK)
#endif
    {
        result.hasError = true;
        snprintf(result.errorMsg, sizeof(result.errorMsg), "HTTP error: %d", httpCode);
        logTimestamp();
        debugPrint("Weather: Fetch failed with HTTP code: ");
        debugPrintln(String(httpCode).c_str());
        return result;
    }

    logTimestamp();
    debugPrint("Weather: Response size: ");
    debugPrint(String(payload.length()).c_str());
    debugPrintln(" bytes");

    // Parse JSON response
    DynamicJsonDocument doc(JSON_BUFFER_SIZE);
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "JSON parse error", sizeof(result.errorMsg));
        logTimestamp();
        debugPrint("Weather: JSON parse error: ");
        debugPrintln(error.c_str());
        return result;
    }

    // Extract hourly data (first entry = current/3-hour forecast)
    if (!doc.containsKey("hourly") || !doc["hourly"].containsKey("temperature_2m") ||
        !doc["hourly"].containsKey("weathercode"))
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "Missing hourly data", sizeof(result.errorMsg));
        logTimestamp();
        debugPrintln("Weather: Response missing expected fields");
        return result;
    }

    JsonArray temps = doc["hourly"]["temperature_2m"];
    JsonArray codes = doc["hourly"]["weathercode"];

    if (temps.size() == 0 || codes.size() == 0)
    {
        result.hasError = true;
        strlcpy(result.errorMsg, "Empty hourly arrays", sizeof(result.errorMsg));
        logTimestamp();
        debugPrintln("Weather: Empty hourly data arrays");
        return result;
    }

    // Get first entry (3-hour forecast)
    float tempFloat = temps[0];
    result.temperature = (int)round(tempFloat);
    result.weatherCode = codes[0];

    logTimestamp();
    char msg[64];
    snprintf(msg, sizeof(msg), "Weather: Success - %d°C, WMO code %d", result.temperature, result.weatherCode);
    debugPrintln(msg);
    logMemory("weather_complete");

    return result;
}
