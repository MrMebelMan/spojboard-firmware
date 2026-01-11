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

/**
 * Parse color name to RGB565 value
 * @param colorName Color name (RED, GREEN, BLUE, YELLOW, ORANGE, PURPLE, CYAN, WHITE)
 * @return RGB565 color value, or 0 if invalid
 */
uint16_t parseColorName(const char* colorName);

/**
 * Get line color with user configuration override
 * Checks config first (exact match, then pattern match), falls back to hardcoded defaults
 *
 * Pattern format: PREFIX*** where asterisk count determines wildcard positions
 * - 9* matches 2-digit lines starting with 9 (91-99)
 * - 95* matches 3-digit lines starting with 95 (950-959)
 * - 4** matches 3-digit lines starting with 4 (400-499)
 * - C*** matches 4-digit lines starting with C (C000-C999)
 *
 * Invalid patterns: leading asterisks (***), non-trailing asterisks (9*1)
 *
 * @param line Line number/code
 * @param configMap User configuration string (format: "A=GREEN,B=YELLOW,9*=CYAN,95*=BLUE,...")
 * @return RGB565 color value
 */
uint16_t getLineColorWithConfig(const char* line, const char* configMap);

#endif // DISPLAYCOLORS_H
