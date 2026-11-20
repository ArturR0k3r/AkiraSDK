/**
 * @file akira_abi.h
 * @brief WASM ABI version this SDK targets
 *
 * An app built with this SDK targets a version of the AkiraOS import interface
 * (the native functions and signatures declared in akira_api.h). Stamp it into
 * the app manifest so the runtime can refuse an app built for an incompatible
 * firmware:
 *
 *     { "abi": "1.0", ... }
 *
 * Keep these constants in sync with the firmware's include/akira_abi.h; the
 * firmware CI check scripts/check_wasm_abi.py enforces that the import lists
 * match.
 *
 * @stability experimental
 * @since 1.6
 */

#ifndef AKIRA_SDK_ABI_H
#define AKIRA_SDK_ABI_H

#define AKIRA_WASM_ABI_VERSION_MAJOR 1
#define AKIRA_WASM_ABI_VERSION_MINOR 0
#define AKIRA_WASM_ABI_VERSION_STRING "1.0"

#endif /* AKIRA_SDK_ABI_H */
