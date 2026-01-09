#include "DisplayColors.h"
#include <string.h>

// ============================================================================
// Color Constants (initialized by initColors)
// ============================================================================

uint16_t COLOR_WHITE;
uint16_t COLOR_YELLOW;
uint16_t COLOR_RED;
uint16_t COLOR_GREEN;
uint16_t COLOR_BLUE;
uint16_t COLOR_ORANGE;
uint16_t COLOR_PURPLE;
uint16_t COLOR_BLACK;
uint16_t COLOR_CYAN;

// ============================================================================
// Color Initialization
// ============================================================================

void initColors(MatrixPanel_I2S_DMA *display)
{
    COLOR_WHITE = display->color565(255, 255, 255);
    COLOR_YELLOW = display->color565(255, 255, 0);
    COLOR_RED = display->color565(255, 0, 0);
    COLOR_GREEN = display->color565(0, 255, 0);
    COLOR_BLUE = display->color565(0, 0, 255);
    COLOR_ORANGE = display->color565(255, 165, 0);
    COLOR_PURPLE = display->color565(128, 0, 128);
    COLOR_BLACK = display->color565(0, 0, 0);
    COLOR_CYAN = display->color565(0, 255, 255);
}

// ============================================================================
// Line Color Helper
// ============================================================================

uint16_t getLineColor(const char *line)
{
    // Metro lines
    if (strcmp(line, "A") == 0)
        return COLOR_GREEN;
    if (strcmp(line, "B") == 0)
        return COLOR_YELLOW;
    if (strcmp(line, "C") == 0)
        return COLOR_RED;

    // Tram lines
    if (line[0] == '1' || line[0] == '2' && strlen(line) <= 2)
        return COLOR_WHITE;
    if (line[0] == '1' || line[0] == '2' && strlen(line) == 3)
        return COLOR_PURPLE;

    // S-trains
    if (line[0] == 'S')
        return COLOR_BLUE;

    // Night lines
    if (line[0] == '9' && strlen(line) <= 2)
        return COLOR_CYAN; // Night trams 91-99

    return COLOR_YELLOW; // Default
}

// ============================================================================
// Color Name Parsing
// ============================================================================

uint16_t parseColorName(const char *colorName)
{
    if (!colorName)
        return 0;

    // Case-insensitive comparison
    if (strcasecmp(colorName, "RED") == 0)
        return COLOR_RED;
    if (strcasecmp(colorName, "GREEN") == 0)
        return COLOR_GREEN;
    if (strcasecmp(colorName, "BLUE") == 0)
        return COLOR_BLUE;
    if (strcasecmp(colorName, "YELLOW") == 0)
        return COLOR_YELLOW;
    if (strcasecmp(colorName, "ORANGE") == 0)
        return COLOR_ORANGE;
    if (strcasecmp(colorName, "PURPLE") == 0)
        return COLOR_PURPLE;
    if (strcasecmp(colorName, "CYAN") == 0)
        return COLOR_CYAN;
    if (strcasecmp(colorName, "WHITE") == 0)
        return COLOR_WHITE;

    return 0; // Invalid color name
}

// ============================================================================
// Configurable Line Colors with Pattern Matching
// ============================================================================

uint16_t getLineColorWithConfig(const char *line, const char *configMap)
{
    if (!line)
        return COLOR_WHITE;

    // Check user configuration first
    if (configMap && strlen(configMap) > 0)
    {
        // Make a mutable copy for strtok
        char mapCopy[256];
        strlcpy(mapCopy, configMap, sizeof(mapCopy));

        // Two-pass approach: exact matches first, then patterns
        // Pass 1: Check for exact match
        char *token = strtok(mapCopy, ",");
        while (token != nullptr)
        {
            char *equals = strchr(token, '=');
            if (equals)
            {
                *equals = '\0'; // Split into line and color
                const char *configLine = token;
                const char *colorName = equals + 1;

                // Skip patterns in first pass
                size_t configLineLen = strlen(configLine);
                bool isPattern = (configLineLen > 0 && configLine[configLineLen - 1] == '*');

                if (!isPattern)
                {
                    // Exact match comparison (case-insensitive)
                    if (strcasecmp(line, configLine) == 0)
                    {
                        uint16_t color = parseColorName(colorName);
                        if (color != 0)
                        {
                            return color; // Found exact match
                        }
                    }
                }
            }
            token = strtok(nullptr, ",");
        }

        // Pass 2: Check pattern matches (copy again for second strtok pass)
        strlcpy(mapCopy, configMap, sizeof(mapCopy));
        token = strtok(mapCopy, ",");
        while (token != nullptr)
        {
            char *equals = strchr(token, '=');
            if (equals)
            {
                *equals = '\0';
                const char *configLine = token;
                const char *colorName = equals + 1;

                // Only process patterns in second pass
                size_t configLineLen = strlen(configLine);
                if (configLineLen > 0 && configLine[configLineLen - 1] == '*')
                {
                    // Pattern match: check if line starts with prefix (excluding *)
                    size_t prefixLen = configLineLen - 1;
                    if (strncasecmp(line, configLine, prefixLen) == 0)
                    {
                        uint16_t color = parseColorName(colorName);
                        if (color != 0)
                        {
                            return color; // Found pattern match
                        }
                    }
                }
            }
            token = strtok(nullptr, ",");
        }
    }

    // Fall back to existing hardcoded defaults
    return getLineColor(line);
}
