/**
 * @file main.c
 * @brief File Viewer — browse the app sandbox and view files as hex or text.
 *
 * Apps are jailed to their own sandbox directory, so this browses (and can
 * only see) files placed in the file_viewer sandbox: side-loaded files, or
 * anything another tool was pointed at this app's dir to drop.  Directories
 * below the sandbox root can be entered; ".." above it is rejected by the OS.
 *
 * Controls (BROWSE):
 *   UP/DOWN = move cursor   A = open (descend dir / view file)
 *   B       = up a level, or exit at the root
 *
 * Controls (VIEW):
 *   UP/DOWN = scroll a line   LEFT/RIGHT = page   X = toggle hex/text
 *   B       = back to the file list
 *
 * @copyright Copyright (c) 2026 PenEngineering S.R.L
 * @license Apache-2.0
 */

#include "akira_api.h"

/* ── Display ─────────────────────────────────────────────────────────── */
static int32_t SCR_W = 320;
static int32_t SCR_H = 240;

#define HDR_H 18
#define FTR_H 14

/* ── Palette ─────────────────────────────────────────────────────────── */
#define C_BG      0x0000
#define C_HDR     0x001F
#define C_HDR_TXT 0xFFFF
#define C_TEXT    0xFFFF
#define C_DIM     0x8410
#define C_SEL_BG  0x001F
#define C_DIR     0x07FF   /* cyan directories   */
#define C_FILE    0xFFFF
#define C_OFF     0x07E0   /* green hex offsets  */
#define C_ASCII   0xFFE0   /* yellow ascii gutter */
#define C_FOOT_BG 0x2104
#define C_ERR     0xF800

/* ── Browser state ───────────────────────────────────────────────────── */
#define MAX_ENTRIES  64
#define NAME_MAX     40
#define PATH_MAX     128

static char cur_dir[PATH_MAX] = "";          /* "" = sandbox root */
static char entries[MAX_ENTRIES][NAME_MAX];
static uint8_t is_dir[MAX_ENTRIES];
static int  n_entries = 0;
static int  sel = 0;
static int  top = 0;                          /* first visible row */

/* ── Viewer state ────────────────────────────────────────────────────── */
#define FILE_CAP  8192                         /* view window cap in bytes */
static uint8_t fbuf[FILE_CAP];
static int  fsize = 0;                         /* bytes actually loaded */
static int  ftrunc = 0;                        /* file was larger than cap */
static char fname[PATH_MAX];
static int  view_off = 0;                      /* scroll offset in bytes */
static int  hex_mode = 1;

enum { S_BROWSE, S_VIEW };
static int state = S_BROWSE;

/* ── Tiny helpers (no libc) ──────────────────────────────────────────── */
static char hexd(int n){ n&=0xF; return (char)(n<10?'0'+n:'A'+n-10); }
static int  slen(const char *s){ int n=0; while(s&&s[n]) n++; return n; }

static void scopy(char *d, const char *s, int cap) {
    int i=0; for (; s[i] && i<cap-1; i++) d[i]=s[i]; d[i]='\0';
}

/* Join cur_dir + name into out ("" root → just name). */
static void join(char *out, const char *dir, const char *name, int cap) {
    int p=0;
    for (int i=0; dir[i] && p<cap-1; i++) out[p++]=dir[i];
    if (p>0 && p<cap-1) out[p++]='/';
    for (int i=0; name[i] && p<cap-1; i++) out[p++]=name[i];
    out[p]='\0';
}

/* ── Directory load ──────────────────────────────────────────────────── */
static void load_dir(void) {
    char buf[1536];
    n_entries = 0; sel = 0; top = 0;

    int n = fs_readdir(cur_dir[0] ? cur_dir : ".", buf, sizeof(buf));
    if (n <= 0) return;                       /* empty or error */

    int pos = 0;
    while (pos < n && n_entries < MAX_ENTRIES) {
        int start = pos;
        while (pos < n && buf[pos] != '\n' && buf[pos] != '\0') pos++;
        int len = pos - start;
        if (len > 0) {
            if (len > NAME_MAX-1) len = NAME_MAX-1;
            for (int i=0;i<len;i++) entries[n_entries][i]=buf[start+i];
            entries[n_entries][len]='\0';

            char full[PATH_MAX];
            join(full, cur_dir, entries[n_entries], sizeof(full));
            akira_stat_t st;
            is_dir[n_entries] = (fs_stat(full, &st)==0 && st.type==1) ? 1 : 0;
            n_entries++;
        }
        if (pos < n && (buf[pos]=='\n')) pos++;
        else if (pos < n && buf[pos]=='\0') break;
    }
}

/* ── File load ───────────────────────────────────────────────────────── */
static void load_file(const char *path) {
    scopy(fname, path, sizeof(fname));
    fsize = 0; ftrunc = 0; view_off = 0;

    int fd = fs_open(path, AKIRA_FS_O_READ);
    if (fd < 0) { fsize = -1; return; }        /* mark error */

    while (fsize < FILE_CAP) {
        int r = fs_read(fd, fbuf + fsize, FILE_CAP - fsize);
        if (r <= 0) break;
        fsize += r;
    }
    /* Anything left over means the file exceeds our window. */
    uint8_t probe;
    if (fsize == FILE_CAP && fs_read(fd, &probe, 1) > 0) ftrunc = 1;
    fs_close(fd);
}

/* ── Chrome ──────────────────────────────────────────────────────────── */
static int text_px(const char *s){ return slen(s)*6; }

static void header(const char *title, const char *right) {
    display_rect(0, 0, SCR_W, HDR_H, C_HDR);
    display_text(6, 5, title, C_HDR_TXT);
    if (right) display_text(SCR_W - 6 - text_px(right), 5, right, C_HDR_TXT);
}
static void footer(const char *hint) {
    display_rect(0, SCR_H-FTR_H, SCR_W, FTR_H, C_FOOT_BG);
    display_text(4, SCR_H-FTR_H+2, hint, C_DIM);
}

/* ── Browse render ───────────────────────────────────────────────────── */
static void draw_browse(void) {
    display_clear(C_BG);
    header("FILES", cur_dir[0] ? cur_dir : "/");

    int row_h = 14;
    int area_y = HDR_H + 4;
    int rows = (SCR_H - area_y - FTR_H - 2) / row_h;

    if (sel < top) top = sel;
    if (sel >= top + rows) top = sel - rows + 1;

    if (n_entries == 0) {
        display_text(10, area_y + 10, "(empty sandbox)", C_DIM);
        display_text(10, area_y + 26, "Side-load files into this", C_DIM);
        display_text(10, area_y + 40, "app's directory to view.", C_DIM);
    }

    for (int i = 0; i < rows && top+i < n_entries; i++) {
        int idx = top + i;
        int y = area_y + i*row_h;
        if (idx == sel) display_rect(0, y-1, SCR_W, row_h, C_SEL_BG);
        uint16_t col = is_dir[idx] ? C_DIR : C_FILE;
        display_text(10, y+2, is_dir[idx] ? "/" : " ", col);
        display_text(20, y+2, entries[idx], col);
    }

    footer("UP/DN move  A open  B up/exit");
    display_flush();
}

/* ── Hex render ──────────────────────────────────────────────────────── */
static void draw_hex(void) {
    int area_y = HDR_H + 4;
    int row_h = 12;
    int rows = (SCR_H - area_y - FTR_H - 2) / row_h;

    for (int r = 0; r < rows; r++) {
        int base = view_off + r*16;
        if (base >= fsize) break;
        int y = area_y + r*row_h;

        /* Offset column (4 hex digits) */
        char off[6];
        off[0]=hexd(base>>12); off[1]=hexd(base>>8);
        off[2]=hexd(base>>4);  off[3]=hexd(base); off[4]=':'; off[5]='\0';
        display_text(2, y, off, C_OFF);

        /* Hex bytes + ASCII gutter, packed to fit 320px */
        char hexs[3] = {0,0,0};
        char asc[17]; int ac=0;
        for (int i=0;i<16;i++) {
            int p = base+i;
            int x = 34 + i*15 + (i>=8?4:0);     /* small mid gap */
            if (p < fsize) {
                uint8_t b = fbuf[p];
                hexs[0]=hexd(b>>4); hexs[1]=hexd(b);
                display_text(x, y, hexs, C_TEXT);
                asc[ac++] = (b>=32 && b<127) ? (char)b : '.';
            } else {
                display_text(x, y, "  ", C_DIM);
                asc[ac++]=' ';
            }
        }
        asc[ac]='\0';
        /* ASCII gutter only fits on the wider Sharp panel; show it there */
        if (SCR_W >= 400) display_text(34 + 16*15 + 12, y, asc, C_ASCII);
    }
}

/* ── Text render ─────────────────────────────────────────────────────── */
static void draw_text(void) {
    int area_y = HDR_H + 4;
    int row_h = 12;
    int rows = (SCR_H - area_y - FTR_H - 2) / row_h;
    int cols = (SCR_W - 8) / 6;                 /* 6px per glyph */
    if (cols > 78) cols = 78;

    char line[80];
    int p = view_off;
    for (int r = 0; r < rows && p < fsize; r++) {
        int c = 0;
        while (c < cols && p < fsize) {
            uint8_t b = fbuf[p++];
            if (b == '\n') break;
            line[c++] = (b == '\t') ? ' '
                      : (b >= 32 && b < 127) ? (char)b : '.';
        }
        line[c] = '\0';
        display_text(4, area_y + r*row_h, line, C_TEXT);
        /* A logical line longer than the row is clipped here; line-based
         * scrolling still lands on the next '\n'. Fine for logs/configs. */
    }
}

/* Advance/rewind the text offset by whole lines for smoother scrolling. */
static int next_line(int off) {
    while (off < fsize && fbuf[off] != '\n') off++;
    return (off < fsize) ? off+1 : fsize;
}
static int prev_line(int off) {
    if (off <= 0) return 0;
    off--;                                      /* step over the preceding \n */
    if (off > 0 && fbuf[off]=='\n') off--;
    while (off > 0 && fbuf[off-1] != '\n') off--;
    return off;
}

static void draw_view(void) {
    display_clear(C_BG);

    char right[20];
    int rp=0;
    if (ftrunc) { const char*t="8K+ "; for(const char*s=t;*s;s++) right[rp++]=*s; }
    const char *m = hex_mode ? "HEX" : "TXT";
    for (const char *s=m; *s; s++) right[rp++]=*s;
    right[rp]='\0';

    header(fname, right);

    if (fsize < 0) {
        display_text(10, HDR_H+20, "Could not open file.", C_ERR);
        footer("B back");
        display_flush();
        return;
    }
    if (fsize == 0) {
        display_text(10, HDR_H+20, "(empty file)", C_DIM);
    } else if (hex_mode) {
        draw_hex();
    } else {
        draw_text();
    }

    footer("UP/DN scroll  L/R page  X hex/txt  B back");
    display_flush();
}

/* ── Input: browse ───────────────────────────────────────────────────── */
static int browse_input(int edge) {
    int row_h = 14;
    int rows = (SCR_H - (HDR_H+4) - FTR_H - 2) / row_h;

    if (edge & AKIRA_BTN_UP)   sel = (sel>0) ? sel-1 : (n_entries? n_entries-1:0);
    if (edge & AKIRA_BTN_DOWN) sel = (n_entries && sel<n_entries-1) ? sel+1 : 0;
    if (edge & AKIRA_BTN_LEFT)  sel = (sel-rows>0) ? sel-rows : 0;
    if (edge & AKIRA_BTN_RIGHT) sel = (sel+rows<n_entries) ? sel+rows : (n_entries?n_entries-1:0);

    if ((edge & AKIRA_BTN_A) && n_entries > 0) {
        char full[PATH_MAX];
        join(full, cur_dir, entries[sel], sizeof(full));
        if (is_dir[sel]) {
            scopy(cur_dir, full, sizeof(cur_dir));
            load_dir();
        } else {
            load_file(full);
            view_off = 0; hex_mode = 1;
            state = S_VIEW;
        }
    }

    if (edge & AKIRA_BTN_B) {
        if (cur_dir[0] == '\0') return 1;       /* exit at root */
        /* pop last path component */
        int i = slen(cur_dir);
        while (i>0 && cur_dir[i-1] != '/') i--;
        cur_dir[i>0 ? i-1 : 0] = '\0';
        load_dir();
    }
    return 0;
}

/* ── Input: view ─────────────────────────────────────────────────────── */
static void view_input(int edge) {
    int area_y = HDR_H + 4;
    int rows = (SCR_H - area_y - FTR_H - 2) / (hex_mode?12:12);
    int page = hex_mode ? rows*16 : 0;

    if (hex_mode) {
        if (edge & AKIRA_BTN_UP)    view_off -= 16;
        if (edge & AKIRA_BTN_DOWN)  view_off += 16;
        if (edge & AKIRA_BTN_LEFT)  view_off -= page;
        if (edge & AKIRA_BTN_RIGHT) view_off += page;
    } else {
        if (edge & AKIRA_BTN_UP)    view_off = prev_line(view_off);
        if (edge & AKIRA_BTN_DOWN)  view_off = next_line(view_off);
        if (edge & AKIRA_BTN_LEFT)  for(int i=0;i<rows;i++) view_off=prev_line(view_off);
        if (edge & AKIRA_BTN_RIGHT) for(int i=0;i<rows;i++) view_off=next_line(view_off);
    }

    if (view_off < 0) view_off = 0;
    if (view_off >= fsize) view_off = fsize>0 ? fsize-1 : 0;

    if (edge & AKIRA_BTN_X) { hex_mode = !hex_mode; view_off = 0; }
    if (edge & AKIRA_BTN_B) { state = S_BROWSE; }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS File Viewer");
    display_get_size(&SCR_W, &SCR_H);

    load_dir();

    int prev = (int)input_get_buttons();
    draw_browse();

    while (1) {
        int held = (int)input_get_buttons();
        int edge = held & ~prev;
        prev = held;

        if (edge) {
            if (state == S_BROWSE) {
                if (browse_input(edge)) { app_switch("supervisor"); return 0; }
            } else {
                view_input(edge);
            }
        }

        if (state == S_BROWSE) draw_browse();
        else                   draw_view();

        delay(30000);
    }
    return 0;
}
