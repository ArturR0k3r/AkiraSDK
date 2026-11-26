/* ESP-IDF heap_caps_*: plain malloc in WASM linear memory. */
#ifndef ESP_HEAP_CAPS_H
#define ESP_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT   0
#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_print_heap_info(caps) ((void)0)
#endif
