/**
 * @file hello_world.c
 * @brief Simple "Hello World" WASM application for AkiraOS
 * 
 * Minimal example demonstrating printfging API.
 * 
 * @copyright Copyright (c) 2026 AkiraOS Contributors  
 * @license Apache-2.0
 */

#include "akira_api.h"

int main(void)
{
    // printf messages at different levels
    printf( "=================================");
    printf( "  Hello from AkiraOS WASM!      ");
    printf( "=================================");
    printf( "");
    
    printf( "[INFO]  This is an info message");
    printf( "[WARN]  This is a warning message");
    printf( "[ERROR] This is an error message");
    
    printf( "");
    printf( "WASM app executed successfully!");
    printf( "SDK Version: 1.0.0");
    printf( "Runtime: AkiraOS WAMR");
    
    return 0;
}
