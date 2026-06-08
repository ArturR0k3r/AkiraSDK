/*
 * audio.c — Non-blocking PWM buzzer sequencer for GR4V.
 * Tones are queued as (freq, frames) pairs and played one per frame.
 */
#include "game.h"
#include "akira_api.h"

/* Internal helper — append one tone to the ring buffer */
static void enqueue(uint16_t freq, uint8_t frames)
{
    uint8_t next = (g.aq_tail + 1u) & 15u;
    if (next == g.aq_head) return;  /* queue full, drop */
    g.aq[g.aq_tail].freq   = freq;
    g.aq[g.aq_tail].frames = frames;
    g.aq_tail = next;
}

/* Predefined sequences (freq Hz, duration frames @ 30fps) */
static void seq_step(void)
{
    uint16_t f = ((g.frame >> 3u) & 1u) ? 900u : 800u;
    enqueue(f, 1);
}
static void seq_shard(void)
{
    enqueue(660, 1); enqueue(0, 1);
    enqueue(880, 1); enqueue(0, 1);
    enqueue(1320, 2);
}
static void seq_death(void)
{
    enqueue(400, 2); enqueue(0, 1);
    enqueue(220, 2); enqueue(0, 1);
    enqueue(110, 3);
}
static void seq_anchor(void)
{
    enqueue(440, 1); enqueue(554, 1); enqueue(440, 1);
}
static void seq_win(void)
{
    enqueue(523, 3); enqueue(0, 1);
    enqueue(659, 3); enqueue(0, 1);
    enqueue(784, 3); enqueue(0, 1);
    enqueue(1047, 4);
}
static void seq_dissolve_warn(void)
{
    enqueue(200, 1);
}
static void seq_glitch_amb(void)
{
    /* random freq 100-600 Hz, 1 frame — use frame counter as LCG seed */
    uint32_t r = (uint32_t)g.frame * 1664525u + 1013904223u;
    uint16_t f = (uint16_t)(100u + (r & 0x1FFu) % 500u);
    enqueue(f, 1);
}
static void seq_exit(void)
{
    enqueue(880, 1);
}

void audio_init(void)
{
    g.aq_head    = 0;
    g.aq_tail    = 0;
    g.audio_timer = 0;
}

void audio_play(int seq_id)
{
    switch (seq_id) {
    case SFX_STEP:       seq_step();          break;
    case SFX_SHARD:      seq_shard();         break;
    case SFX_DEATH:      seq_death();         break;
    case SFX_ANCHOR:     seq_anchor();        break;
    case SFX_WIN:        seq_win();           break;
    case SFX_DISSOLVE:   seq_dissolve_warn(); break;
    case SFX_GLITCH_AMB: seq_glitch_amb();    break;
    case SFX_EXIT:       seq_exit();          break;
    default: break;
    }
}

void audio_update(void)
{
    if (g.audio_timer > 0) {
        g.audio_timer--;
        return;
    }
    /* Current tone expired — silence and start next */
    pwm_disable(0);
    if (g.aq_head == g.aq_tail) return;  /* queue empty */

    AudioTone t = g.aq[g.aq_head];
    g.aq_head = (g.aq_head + 1u) & 15u;

    if (t.freq > 0) pwm_set(0, t.freq, 50);
    g.audio_timer = t.frames;
}
