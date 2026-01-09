#ifndef DISPLAYCOLORS_H
#define DISPLAYCOLORS_H

#include <stdint.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ============================================================================
// Color Definitions (RGB565 format)
// ============================================================================

extern uint16_t COLOR_WHITE;
extern uint16_t COLOR_YELLOW;
extern uint16_t COLOR_RED;
extern uint16_t COLOR_GREEN;
extern uint16_t COLOR_BLUE;
extern uint16_t COLOR_ORANGE;
extern uint16_t COLOR_PURPLE;
extern uint16_t COLOR_BLACK;
extern uint16_t COLOR_CYAN;

// ============================================================================
// Color Management Functions
// ============================================================================

/**
 * Initialize color constants from display panel
 * Must be called after display is initialized
 * @param display Pointer to initialized HUB75 display
 */
void initColors(MatrixPanel_I2S_DMA* display);

/**
 * Get color for transit line number
 * Returns appropriate color based on line type (Metro, Tram, S-train, Night)
 * @param line Line number/code (e.g., "A", "31", "S9", "91")
 * @return RGB565 color code
 */
uint16_t getLineColor(const char* line);

#endif // DISPLAYCOLORS_H
