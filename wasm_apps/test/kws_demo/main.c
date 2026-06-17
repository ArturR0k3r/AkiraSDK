/*
 * Copyright (c) 2026 PenEngineering S.R.L
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file kws_demo/main.c
 * @brief AkiraClaw keyword-spotting demo — "hey akira" → app_switch("shell")
 *
 * Pipeline:
 *   1. Load the bundled DS-CNN KWS model via aiinfer_load.
 *   2. Poll the microphone channel (SENSOR_CHAN_MICROPHONE or ADC fallback)
 *      to fill a 1 s / 16 kHz / 16-bit mono frame buffer.
 *   3. Compute a 40-band log-mel spectrogram in software (int8 output).
 *   4. Feed the spectrogram to the model; read class probabilities.
 *   5. If class 0 ("hey akira") score > THRESHOLD for N consecutive frames,
 *      call app_switch("shell").
 *
 * Model I/O contract (DS-CNN quantized, INT8):
 *   Input  : [1, 49, 40, 1]  int8   (49 frames × 40 mel bins)
 *   Output : [1, 2]          int8   (scores: [hey_akira, background])
 *
 * Capabilities required: display.write, sensor.read, ai.infer, app.switch,
 *                         timer, memory
 *
 * Build with:
 *   make
 *   akira-cli pack kws_demo.wasm manifest.json --model kws_model.tflite
 *   akira-cli sign kws_demo.akpkg --key privkey.pem
 */

#include "../../../include/akira_api.h"

/* ── Display ────────────────────────────────────────────────────────────── */
#define DISPLAY_W   320
#define DISPLAY_H   240
#define C_BG        0x0000   /* black       */
#define C_TITLE     0x07FF   /* cyan        */
#define C_IDLE      0x4228   /* dark grey   */
#define C_LISTEN    0x001F   /* blue        */
#define C_DETECT    0x07E0   /* green       */
#define C_SCORE_BAR 0x001F

/* ── Audio / model parameters ───────────────────────────────────────────── */
#define SAMPLE_RATE_HZ     16000
#define FRAME_LEN_MS       1000    /* 1-second capture window */
#define SAMPLES_PER_FRAME  (SAMPLE_RATE_HZ)   /* 16 000 samples */
#define MEL_FRAMES         49
#define MEL_BINS           40
#define INPUT_LEN          (MEL_FRAMES * MEL_BINS)   /* 1960 int8 bytes */
#define OUTPUT_CLASSES     2

/* Detection thresholds */
#define SCORE_THRESHOLD    100     /* int8 score > 100 → candidate detect */
#define CONFIRM_FRAMES     2       /* 2 consecutive detections → trigger   */

/* Sensor channel for microphone (platform-defined; 0 = channel 0) */
#define SENSOR_CHAN_MIC    0

/* ── Model storage ──────────────────────────────────────────────────────── */
/*
 * The model is loaded from the akpkg's model.tflite entry at runtime.
 * On Zephyr/AkiraOS the runtime makes the model bytes available via
 * the storage API (mounted at /lfs/apps/<app_name>/model.tflite) after
 * installation.  We read it into a heap buffer and pass it to aiinfer_load.
 */
#define MODEL_PATH  "/lfs/apps/kws_demo/model.tflite"
#define MODEL_MAX_BYTES  (64 * 1024)   /* 64 KiB upper bound for KWS model */

static int8_t  g_input_buf[INPUT_LEN];
static int8_t  g_output_buf[OUTPUT_CLASSES];

/* ── UI helpers ─────────────────────────────────────────────────────────── */
static void ui_header(void)
{
    display_rect(0, 0, DISPLAY_W, 28, C_TITLE);
    display_text(8, 6, "AkiraClaw KWS Demo", C_BG);
}

static void ui_status(const char *msg, int color)
{
    display_rect(0, 30, DISPLAY_W, 30, C_BG);
    display_text(8, 36, msg, color);
}

static void ui_score(int score)
{
    int bar_w = (score < 0) ? 0 : (score > 127) ? DISPLAY_W - 20 : (score * (DISPLAY_W - 20)) / 127;
    display_rect(10, 70, DISPLAY_W - 20, 18, C_BG);
    if (bar_w > 0)
        display_rect(10, 70, bar_w, 18, C_SCORE_BAR);
    display_text(8, 94, "  'hey akira' score  ", C_IDLE);
}

/* ── Minimal log-mel front-end (software, integer arithmetic) ───────────── */
/*
 * Real deployment: replace this stub with a proper MFCC/log-mel pipeline
 * (e.g. the reference implementation from tflite/experimental/microfrontend).
 * This stub fills the input buffer with the raw ADC samples scaled to int8,
 * which is sufficient to exercise the aiinfer_* API end-to-end on hardware
 * without a proper DSP library.
 */
static void compute_mel_stub(const int *adc_samples, int8_t *mel_out)
{
    for (int i = 0; i < INPUT_LEN; i++) {
        int sample_idx = (i * SAMPLES_PER_FRAME) / INPUT_LEN;
        int v = adc_samples[sample_idx] >> 8;   /* scale 16→8 bit */
        mel_out[i] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
    }
}

/* ── Entry point ────────────────────────────────────────────────────────── */
int main(void)
{
    display_clear(C_BG);
    ui_header();
    ui_status("Loading model...", C_IDLE);

    /* Read model bytes from storage */
    void *model_buf = mem_alloc(MODEL_MAX_BYTES);
    if (!model_buf) {
        ui_status("OOM: model buffer", 0xF800);
        return -1;
    }

    int fd = storage_open(MODEL_PATH, 0 /* O_RDONLY */);
    if (fd < 0) {
        ui_status("Model not found — pack with --model", 0xF800);
        mem_free(model_buf);
        return -1;
    }
    int model_size = storage_read(fd, model_buf, MODEL_MAX_BYTES);
    storage_close(fd);

    if (model_size <= 0) {
        ui_status("Model read error", 0xF800);
        mem_free(model_buf);
        return -1;
    }

    int handle = aiinfer_load(model_buf, model_size);
    mem_free(model_buf);

    if (handle < 0) {
        ui_status("aiinfer_load failed", 0xF800);
        return -1;
    }

    ui_status("Listening...", C_LISTEN);

    /* ADC capture buffer — reused every frame */
    int *adc_buf = (int *)mem_alloc(SAMPLES_PER_FRAME * sizeof(int));
    if (!adc_buf) {
        ui_status("OOM: ADC buffer", 0xF800);
        aiinfer_unload(handle);
        return -1;
    }

    int confirm_count = 0;

    for (;;) {
        /* Collect one second of microphone data via sensor_read */
        for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
            adc_buf[i] = sensor_read(SENSOR_CHAN_MIC);
            /* 16 kHz → ~62.5 µs between samples */
            delay(62);
        }

        /* Feature extraction */
        compute_mel_stub(adc_buf, g_input_buf);

        /* Run inference */
        int ret = aiinfer_run(handle,
                              g_input_buf, INPUT_LEN,
                              g_output_buf, OUTPUT_CLASSES);
        if (ret < 0) {
            ui_status("Inference error", 0xF800);
            delay(500000);
            ui_status("Listening...", C_LISTEN);
            confirm_count = 0;
            continue;
        }

        int8_t score_hey = g_output_buf[0];   /* class 0: "hey akira" */
        ui_score((int)score_hey + 128);        /* shift to 0..255 range */

        if (score_hey > SCORE_THRESHOLD) {
            confirm_count++;
            ui_status("Detected! Confirming...", C_DETECT);
        } else {
            confirm_count = 0;
            ui_status("Listening...", C_LISTEN);
        }

        if (confirm_count >= CONFIRM_FRAMES) {
            /* Wake word confirmed — hand off to shell */
            display_clear(C_BG);
            display_text(80, 110, "Hey Akira!", C_DETECT);
            delay(800000);
            aiinfer_unload(handle);
            mem_free(adc_buf);
            app_switch("shell");
            return 0;
        }
    }
}
