/*
 * @copyright Copyright © contributors to Project Ocre,
 * which has been established as Project Ocre a Series of LF Projects, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * AkiraOS Hello World WASM App
 */

int log(int level, const char *message);

/* Log levels */
#define LOG_ERROR 0
#define LOG_WARN 1
#define LOG_INFO 2
#define LOG_DEBUG 3

int main(void)
{
    log(LOG_INFO, "Hello, World from AkiraOS WASM App!");

    return 0;
}
