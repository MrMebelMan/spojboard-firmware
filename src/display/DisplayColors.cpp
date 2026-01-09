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

void initColors(MatrixPanel_I2S_DMA* display)
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

uint16_t getLineColor(const char* line)
{
    // Metro lines
    if (strcmp(line, "A") == 0)
        return COLOR_GREEN;
    if (strcmp(line, "B") == 0)
        return COLOR_YELLOW;
    if (strcmp(line, "C") == 0)
        return COLOR_RED;

    // Tram lines
    if (strcmp(line, "8") == 0)
        return COLOR_RED;
    if (strcmp(line, "7") == 0)
        return COLOR_ORANGE;
    if (strcmp(line, "12") == 0)
        return COLOR_PURPLE;
    if (strcmp(line, "31") == 0)
        return COLOR_GREEN;

    // S-trains
    if (line[0] == 'S')
        return COLOR_BLUE;

    // Night lines
    if (line[0] == '9' && strlen(line) <= 2)
        return COLOR_CYAN; // Night trams 91-99

    return COLOR_YELLOW; // Default
}
