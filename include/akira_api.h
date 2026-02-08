/**
 * @file akira_api.h
 * @brief AkiraOS WASM API Exports
 *
 * All functions exported to WASM applications.
 * Each API requires specific capabilities granted in app manifest.
 */

// TBD!!!!

#ifndef AKIRA_API_H
#define AKIRA_API_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Log message (for debugging)
     * @param level Log level (0=error, 1=warn, 2=info, 3=debug)
     * @param message Log message
     */
    int log(int level, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* AKIRA_API_H */
