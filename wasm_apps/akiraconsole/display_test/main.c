/**
 * @file display_test.c
 * @brief Display graphics test WASM application for AkiraOS
 * 
 * This application demonstrates all display API functionality:
 * - Clearing the screen with different colors
 * - Drawing individual pixels
 * - Drawing filled rectangles
 * - Rendering text with small and large fonts
 * - Creating visual patterns and demonstrations
 * 
 * @copyright Copyright (c) 2026 AkiraOS Contributors
 * @license Apache-2.0
 */

#include "akira_api.h"



/**
 * @brief Test 1: Clear screen with different colors
 */
static void test_clear_screen(void)
{
    printf("Test 1: Clear screen with colors");
    
    display_clear(COLOR_BLACK);
    display_text(10, 10, "BLACK", COLOR_WHITE);
    display_flush();
    delay(500000);
    
    display_clear(COLOR_RED);
    display_text(10, 10, "RED", COLOR_WHITE);
    display_flush();
    delay(500000);
    
    display_clear(COLOR_GREEN);
    display_text(10, 10, "GREEN", COLOR_WHITE);
    display_flush();
    delay(500000);
    
    display_clear(COLOR_BLUE);
    display_text(10, 10, "BLUE", COLOR_WHITE);
    display_flush();
    delay(500000);
    
    display_clear(COLOR_WHITE);
    display_text(10, 10, "WHITE", COLOR_BLACK);
    display_flush();
    delay(500000);
}

/**
 * @brief Test 2: Draw rectangles of various sizes and colors
 */
static void test_rectangles(void)
{
    printf("Test 2: Drawing rectangles");
    
    display_clear(COLOR_BLACK);
    display_text(10, 5, "Rectangle Test", COLOR_WHITE);
    
    // Draw a rainbow of rectangles
    display_rect(10, 30, 40, 30, COLOR_RED);
    display_rect(60, 30, 40, 30, COLOR_ORANGE);
    display_rect(110, 30, 40, 30, COLOR_YELLOW);
    display_rect(160, 30, 40, 30, COLOR_GREEN);
    display_rect(210, 30, 40, 30, COLOR_CYAN);
    
    display_rect(10, 70, 40, 30, COLOR_BLUE);
    display_rect(60, 70, 40, 30, COLOR_PURPLE);
    display_rect(110, 70, 40, 30, COLOR_MAGENTA);
    display_rect(160, 70, 40, 30, COLOR_GRAY);
    display_rect(210, 70, 40, 30, COLOR_WHITE);
    
    // Draw nested squares
    display_rect(10, 120, 100, 100, COLOR_RED);
    display_rect(20, 130, 80, 80, COLOR_ORANGE);
    display_rect(30, 140, 60, 60, COLOR_YELLOW);
    display_rect(40, 150, 40, 40, COLOR_GREEN);
    display_rect(50, 160, 20, 20, COLOR_BLUE);
    
    // Draw border frame
    display_rect(0, 0, 320, 5, COLOR_WHITE);     // Top
    display_rect(0, 235, 320, 5, COLOR_WHITE);   // Bottom
    display_rect(0, 0, 5, 240, COLOR_WHITE);     // Left
    display_rect(315, 0, 5, 240, COLOR_WHITE);   // Right

    display_flush();
    delay(1000000);
}

/**
 * @brief Test 3: Draw pixel patterns
 */
static void test_pixels(void)
{
    printf("Test 3: Drawing pixels");
    
    display_clear(COLOR_BLACK);
    display_text(10, 5, "Pixel Test", COLOR_WHITE);
    
    // Draw diagonal lines
    for (int i = 0; i < 200; i++) {
        display_pixel(i, i, COLOR_RED);
        display_pixel(i, 200 - i, COLOR_GREEN);
    }
    
    // Draw a grid pattern
    for (int x = 20; x < 300; x += 10) {
        for (int y = 30; y < 220; y += 10) {
            display_pixel(x, y, COLOR_CYAN);
            display_pixel(x + 1, y, COLOR_CYAN);
            display_pixel(x, y + 1, COLOR_CYAN);
            display_pixel(x + 1, y + 1, COLOR_CYAN);
        }
    }

    display_flush();
    delay(1000000);
}

/**
 * @brief Test 4: Text rendering with different fonts
 */
static void test_text(void)
{
    printf("Test 4: Text rendering");
    
    display_clear(COLOR_BLACK);
    
    // Title with large font
    display_text_large(10, 10, "AkiraOS", COLOR_CYAN);
    display_text_large(10, 35, "Display", COLOR_GREEN);
    display_text_large(10, 60, "Test", COLOR_MAGENTA);
    
    // Small font text
    display_text(10, 100, "Small font text rendering", COLOR_WHITE);
    display_text(10, 115, "RGB565 color format", COLOR_YELLOW);
    display_text(10, 130, "320x240 pixel display", COLOR_ORANGE);
    
    // Color palette demonstration
    display_text(10, 155, "Color Palette:", COLOR_WHITE);
    display_text(10, 170, "RED", COLOR_RED);
    display_text(50, 170, "GREEN", COLOR_GREEN);
    display_text(100, 170, "BLUE", COLOR_BLUE);
    display_text(10, 185, "CYAN", COLOR_CYAN);
    display_text(50, 185, "MAGENTA", COLOR_MAGENTA);
    display_text(110, 185, "YELLOW", COLOR_YELLOW);
    
    display_text(10, 210, "WASM Graphics Demo v1.0", COLOR_LIGHT_GRAY);
    display_text(10, 225, "Press any key...", COLOR_DARK_GRAY);

    display_flush();
    delay(1500000);
}

/**
 * @brief Test 5: Complex graphics demonstration
 */
static void test_complex_graphics(void)
{
    printf("Test 5: Complex graphics");
    
    display_clear(COLOR_BLACK);
    
    // Draw a bar chart
    display_text(10, 5, "Performance Chart", COLOR_WHITE);
    
    int bar_heights[] = {30, 50, 80, 40, 70, 60, 90};
    int bar_colors[] = {COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, 
                        COLOR_GREEN, COLOR_CYAN, COLOR_BLUE, COLOR_MAGENTA};
    
    for (int i = 0; i < 7; i++) {
        int x = 20 + (i * 40);
        int y = 200 - bar_heights[i];
        display_rect(x, y, 30, bar_heights[i], bar_colors[i]);
    }
    
    // Draw baseline
    display_rect(10, 200, 290, 2, COLOR_WHITE);

    display_flush();
    delay(1000000);
}

/**
 * @brief Main entry point for the display test application
 * 
 * Runs through all display tests sequentially.
 * 
 * @return 0 on success
 */
int main(void)
{
    printf("=====================================");
    printf( "  AkiraOS Display Test Application  ");
    printf( "=====================================");

    
    // Run all tests
    test_clear_screen();
    test_rectangles();
    test_pixels();
    test_text();
    test_complex_graphics();
    
    // Final screen
    display_clear(COLOR_BLUE);
    display_text_large(30, 80, "DISPLAY", COLOR_WHITE);
    display_text_large(30, 110, "TEST", COLOR_WHITE);
    display_text_large(30, 140, "COMPLETE", COLOR_WHITE);
    display_text(60, 190, "All tests passed!", COLOR_YELLOW);
    display_flush();

    printf("Display test completed successfully!");
    printf( "All graphics primitives tested");
    
    return 0;
}
