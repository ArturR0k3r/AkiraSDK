/*
 * aiinfer_test — AkiraClaw end-to-end inference test
 *
 * Loads the hello_world_float TFLite Micro model (sine approximation) and
 * runs it at several known angles.  Expected output (approximate):
 *   x=0.000  y=0.000   (sin 0)
 *   x=0.785  y=0.707   (sin π/4)
 *   x=1.571  y=1.000   (sin π/2)
 *   x=3.142  y=0.000   (sin π)
 */

#include "akira_api.h"
#include "model_data.h"

/* Format a float as "±D.DDD" into buf[8]; buf must be >= 8 bytes */
static void ftoa3(float f, char *buf)
{
    char *p = buf;
    if (f < 0.0f) { *p++ = '-'; f = -f; }
    int whole = (int)f;
    int frac  = (int)((f - (float)whole) * 1000.0f + 0.5f);
    if (frac >= 1000) { whole++; frac -= 1000; }
    if (whole >= 10) *p++ = '0' + whole / 10;
    *p++ = '0' + whole % 10;
    *p++ = '.';
    *p++ = '0' + frac / 100;
    *p++ = '0' + (frac / 10) % 10;
    *p++ = '0' + frac % 10;
    *p = '\0';
}

int main(void)
{
    char xbuf[10], ybuf[10], nbuf[10];
    itoa((int)hello_world_model_len, nbuf);
    printf("[aiinfer_test] loading hello_world model (%s bytes)...", nbuf);

    int handle = aiinfer_load(hello_world_model, (int)hello_world_model_len);
    if (handle < 0) {
        itoa(handle, nbuf);
        printf("[aiinfer_test] aiinfer_load FAILED: %s", nbuf);
        return 1;
    }
    itoa(handle, nbuf);
    printf("[aiinfer_test] loaded on slot %s", nbuf);

    float inputs[] = { 0.0f, 0.7854f, 1.5708f, 3.1416f };
    int n = (int)(sizeof(inputs) / sizeof(inputs[0]));

    for (int i = 0; i < n; i++) {
        float x = inputs[i];
        float y = 0.0f;

        int ret = aiinfer_run(handle,
                              &x, (int)sizeof(float),
                              &y, (int)sizeof(float));
        if (ret != 0) {
            itoa(ret, nbuf);
            printf("[aiinfer_test] aiinfer_run FAILED: %s", nbuf);
            aiinfer_unload(handle);
            return 1;
        }
        ftoa3(x, xbuf);
        ftoa3(y, ybuf);
        printf("  x=%s  y=%s", xbuf, ybuf);
    }

    aiinfer_unload(handle);
    printf("[aiinfer_test] PASS");
    return 0;
}
