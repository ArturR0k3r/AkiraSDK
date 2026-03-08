/**
 * @file gpio.c
 * @brief GPIO control WASM application for AkiraOS
 * 
 * This application demonstrates GPIO functionality:
 * - Configuring GPIO pins as input and output
 * - Reading from input pins
 * - Writing to output pins
 * - Creating a simple input-to-output mirror application
 * 
 * The app reads from GPIO 5 (input) and mirrors the state to GPIO 40 (output).
 * 
 * @copyright Copyright (c) 2026 AkiraOS Contributors
 * @license Apache-2.0
 */

#include "akira_api.h"

// GPIO pin definitions
#define GPIO_INPUT_PIN   5  /**< Input pin number */
#define GPIO_OUTPUT_PIN  40  /**< Output pin number */

/**
 * @brief Main entry point for the GPIO WASM application
 * 
 * Configures GPIO pins and runs a loop that reads from an input pin
 * and mirrors the state to an output pin.
 * 
 * @return 0 on success, negative error code on failure
 */
int main(void)
{
    int ret;
    
    printf( "=================================");
    printf( "  AkiraOS GPIO Test Application ");
    printf( "=================================");
    
    // Configure GPIO 5 as input
    printf( "Configuring GPIO 5 as INPUT...");
    ret = gpio_configure(GPIO_INPUT_PIN, GPIO_INPUT);
    if (ret < 0) {
        printf( "Failed to configure GPIO input pin!");
        return ret;
    }
    printf( "GPIO 5 configured successfully");
    
    printf( "Starting GPIO monitoring...");
    printf( "Reading GPIO 5 state (100 iterations)");
    
    int last_state = -1;
    
    // Main loop: just read the input - reduced to 100 iterations, no delay
    while(1) {
        // Read the input pin state
        int input_state = gpio_read(GPIO_INPUT_PIN);
        if (input_state < 0) {
            printf( "Failed to read GPIO input!");
            return input_state;
        }
        
        // Only log when state changes
        if (input_state != last_state) {
            if (input_state == 1) {
                printf( "Input HIGH detected");
            } else {
                printf( "Input LOW detected");
            }
            last_state = input_state;
        }
    }
    
    printf( "GPIO monitoring completed");
    return 0;
}
