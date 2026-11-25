/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * AkiraOS platform layer for the OpenLara fixed-point engine (Tomb Raider 1).
 * Renders 8bpp into fb[] (GBA MODE4 rasterizer), converts to RGB565 and sends
 * it to the panel. Level files are packed GBA levels (*.PKD) in the app's SD
 * directory: /SD:/apps/tombraider/data/.
 */
#include "game.h"
#include "akira_natives.h"

#define SCREEN_W  320
#define SCREEN_H  240
#define FB_X      ((SCREEN_W - FRAME_WIDTH) / 2)
#define FB_Y      ((SCREEN_H - FRAME_HEIGHT) / 2)

#define BTN_UP    (1u << 2)
#define BTN_DOWN  (1u << 3)
#define BTN_LEFT  (1u << 4)
#define BTN_RIGHT (1u << 5)
#define BTN_A     (1u << 6)
#define BTN_Y     (1u << 7)
#define BTN_B     (1u << 8)
#define BTN_X     (1u << 9)
#define BTN_HOME  (1u << 1)

int32 fps;

static uint16 pal565[256];
static uint16 frame[FRAME_WIDTH * FRAME_HEIGHT];
static bool frameInFlight;

static const void* levelData;
static const void* titleScr;

static void logLine(const char* what, const char* path)
{
    char line[96];
    strcpy(line, "tr: ");
    strcat(line, what);
    strncat(line, path, sizeof(line) - strlen(line) - 1);
    printf_native(line);
}

// ---- C++ runtime without libc++abi -----------------------------------------

void* operator new(size_t size) { return malloc(size); }
void* operator new[](size_t size) { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

#ifdef __wasm__
// wasi-libc internals (stdio behind sprintf, malloc's random seed) still
// reference these WASI syscalls. The app never reaches them; defining them
// keeps them from becoming imports AkiraOS cannot link.
extern "C" {
int32_t __imported_wasi_snapshot_preview1_fd_write(int32_t, int32_t, int32_t, int32_t) { return 8; /* EBADF */ }
int32_t __imported_wasi_snapshot_preview1_fd_close(int32_t) { return 8; }
int32_t __imported_wasi_snapshot_preview1_fd_seek(int32_t, int64_t, int32_t, int32_t) { return 8; }
int32_t __imported_wasi_snapshot_preview1_random_get(int32_t, int32_t) { return 52; /* ENOSYS */ }
}
#endif

// ---- files -----------------------------------------------------------------

static void* loadFile(const char* path, int32* outSize)
{
    int fd = fs_open(path, AKIRA_FS_O_READ);
    if (fd < 0) {
        logLine("can't open ", path);
        return NULL;
    }

    int32 size = fs_seek(fd, 0, 2) < 0 ? -1 : fs_tell(fd);
    fs_seek(fd, 0, 0);

    uint8* data = size > 0 ? (uint8*)malloc(size) : NULL;
    if (!data) {
        logLine("no memory for ", path);
        fs_close(fd);
        return NULL;
    }

    int32 got = 0;
    while (got < size) {
        int r = fs_read(fd, data + got, size - got);
        if (r <= 0) break;
        got += r;
    }
    fs_close(fd);

    if (got != size) {
        free(data);
        return NULL;
    }
    if (outSize) *outSize = size;
    return data;
}

static bool saveFile(const char* path, const void* a, int32 aSize, const void* b, int32 bSize)
{
    int fd = fs_open(path, AKIRA_FS_O_WRITE | AKIRA_FS_O_CREATE | AKIRA_FS_O_TRUNC);
    if (fd < 0) return false;
    bool ok = fs_write(fd, a, aSize) == aSize && (bSize == 0 || fs_write(fd, b, bSize) == bSize);
    fs_close(fd);
    return ok;
}

// ---- OpenLara OS hooks -----------------------------------------------------

static uint32 nowMS()
{
    // Monotonic: the firmware uptime can step back.
    static uint32 last;
    uint32 t = (uint32)rtc_get_uptime_ms();
    if ((int32)(t - last) > 0) last = t;
    return last;
}

int32 osGetSystemTimeMS()
{
    return (int32)nowMS();
}

void osSetPalette(const uint16* palette)
{
    // GBA BGR555 -> RGB565. The display natives take RGB565 as-is (no swap).
    for (int32 i = 0; i < 256; i++)
    {
        uint16 c = palette[i];
        uint16 r = c & 0x1F;
        uint16 g = (c >> 5) & 0x1F;
        uint16 b = (c >> 10) & 0x1F;
        pal565[i] = (r << 11) | (g << 6) | (g >> 4) | b;
    }
}

bool osSaveSettings()
{
    return saveFile("settings.dat", &gSettings, sizeof(gSettings), NULL, 0);
}

bool osLoadSettings()
{
    int32 size;
    uint8* data = (uint8*)loadFile("settings.dat", &size);
    if (!data) return false;
    bool ok = size == sizeof(gSettings) && data[0] == gSettings.version;
    if (ok) memcpy(&gSettings, data, sizeof(gSettings));
    free(data);
    return ok;
}

bool osCheckSave()
{
    int fd = fs_open("savegame.dat", AKIRA_FS_O_READ);
    if (fd < 0) return false;
    fs_close(fd);
    return true;
}

bool osSaveGame()
{
    return saveFile("savegame.dat", &gSaveGame, sizeof(gSaveGame), &gSaveData, gSaveGame.dataSize);
}

bool osLoadGame()
{
    int32 size;
    uint8* data = (uint8*)loadFile("savegame.dat", &size);
    if (!data) return false;

    bool ok = size >= (int32)sizeof(gSaveGame) && *(uint32*)data == SAVEGAME_VER;
    if (ok)
    {
        memcpy(&gSaveGame, data, sizeof(gSaveGame));
        int32 extra = size - (int32)sizeof(gSaveGame);
        if (extra > gSaveGame.dataSize) extra = gSaveGame.dataSize;
        memcpy(&gSaveData, data + sizeof(gSaveGame), extra);
    }
    free(data);
    return ok;
}

void osJoyVibrate(int32 index, int32 L, int32 R) {}

const void* osLoadScreen(LevelID id)
{
    return titleScr;
}

const void* osLoadLevel(LevelID id)
{
    char path[48];

    free((void*)levelData);
    levelData = NULL;

    strcpy(path, "data/");
    strcat(path, (const char*)gLevelInfo[id].data);
    strcat(path, ".PKD");

    levelData = loadFile(path, NULL);

    if (!titleScr) {
        // TITLE.SCR is a 240x160 8bpp picture; stretch it to the frame size.
        uint8* src = (uint8*)loadFile("data/TITLE.SCR", NULL);
        uint8* dst = src ? (uint8*)malloc(FRAME_WIDTH * FRAME_HEIGHT) : NULL;
        if (dst) {
            for (int32 y = 0; y < FRAME_HEIGHT; y++) {
                const uint8* row = src + (y * 160 / FRAME_HEIGHT) * 240;
                for (int32 x = 0; x < FRAME_WIDTH; x++) {
                    dst[y * FRAME_WIDTH + x] = row[x * 240 / FRAME_WIDTH];
                }
            }
        }
        free(src);
        titleScr = dst;
    }

    return levelData;
}

// ---- sound: no audio hardware on AkiraConsole ------------------------------

void sndInit() {}
void sndInitSamples() {}
void sndFreeSamples() {}
void* sndPlaySample(int32 index, int32 volume, int32 pitch, int32 mode) { return NULL; }
void sndPlayTrack(int32 track) {}
void sndStopTrack() {}
bool sndTrackIsPlaying() { return false; }
void sndStopSample(int32 index) {}
void sndStop() {}
void sndFill(uint8* buffer, int32 count) {}

// ---- input -----------------------------------------------------------------

static void inputUpdate()
{
    static uint32 prev, homeDown;
    uint32 b = (uint32)input_get_buttons();
    uint32 now = nowMS();

    keys = 0;
    if (b & BTN_UP)    keys |= IK_UP;
    if (b & BTN_DOWN)  keys |= IK_DOWN;
    if (b & BTN_LEFT)  keys |= IK_LEFT;
    if (b & BTN_RIGHT) keys |= IK_RIGHT;
    if (b & BTN_A)     keys |= IK_A;
    if (b & BTN_B)     keys |= IK_B;
    if (b & BTN_X)     keys |= IK_X;
    if (b & BTN_Y)     keys |= IK_Y;

    // HOME tap = START (inventory/menu); a long hold exits to AkiraOS.
    static int32 startFrames;
    if ((b & BTN_HOME) && !(prev & BTN_HOME)) homeDown = now;
    if (!(b & BTN_HOME) && (prev & BTN_HOME) && now - homeDown < 1000) startFrames = 2;
    if (startFrames > 0) {
        keys |= IK_START;
        startFrames--;
    }
    prev = b;
}

// ---- video -----------------------------------------------------------------

static void blit()
{
    if (frameInFlight) {
        display_raw_wait();
    }

    const uint8* src = (const uint8*)fb;
    for (int32 i = 0; i < FRAME_WIDTH * FRAME_HEIGHT; i++) {
        frame[i] = pal565[src[i]];
    }

    frameInFlight = display_raw_write_async(FB_X, FB_Y, FRAME_WIDTH, FRAME_HEIGHT,
                                            frame, sizeof(frame)) == 0;
}

static void clearScreen()
{
    // Black borders around a frame smaller than the panel.
    memset(frame, 0, sizeof(frame));
    for (int32 y = 0; y < SCREEN_H; y += FRAME_HEIGHT) {
        for (int32 x = 0; x < SCREEN_W; x += FRAME_WIDTH) {
            int32 w = X_MIN(FRAME_WIDTH, SCREEN_W - x);
            int32 h = X_MIN(FRAME_HEIGHT, SCREEN_H - y);
            display_raw_write(x, y, w, h, frame, w * h * 2);
        }
    }
}

// ---- main ------------------------------------------------------------------

extern "C" int main(void)
{
    clearScreen();
    gameInit();

    // Game logic runs at a fixed 30 Hz; render as fast as we can.
    uint32 start = nowMS();
    int32 lastTick = 0;
    uint32 fpsStart = start;
    int32 fpsFrames = 0;

    while (1)
    {
        inputUpdate();

        int32 tick = (int32)((nowMS() - start) / 33);
        gameUpdate(tick - lastTick);
        lastTick = tick;

        gameRender();
        blit();

        fpsFrames++;
        uint32 now = nowMS();
        if (now - fpsStart >= 1000) {
            fps = fpsFrames * 1000 / (int32)(now - fpsStart);
            fpsFrames = 0;
            fpsStart = now;
        }
    }

    return 0;
}
