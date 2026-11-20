/**
 * @file main.c
 * @brief Image Viewer — display 24-bit BMP images from the app sandbox.
 *
 * BMP is decoded a row at a time and blitted straight to the panel, so a
 * full framebuffer never has to live in WASM memory.  Images larger than
 * the screen are integer-downscaled to fit; smaller ones are centred.
 *
 * Supported: uncompressed (BI_RGB) 24-bpp Windows BMP, bottom-up or top-down.
 * Drop .bmp files into this app's sandbox directory to view them.
 *
 * Controls:
 *   BROWSE: UP/DOWN move   A view   B up a level / exit
 *   VIEW:   A/B back to the list
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
#define C_DIR     0x07FF
#define C_IMG     0x07E0   /* green — a viewable image */
#define C_FILE    0xFFFF
#define C_FOOT_BG 0x2104
#define C_ERR     0xF800

/* ── Browser ─────────────────────────────────────────────────────────── */
#define MAX_ENTRIES 64
#define NAME_MAX    40
#define PATH_MAX    128

static char cur_dir[PATH_MAX] = "";
static char entries[MAX_ENTRIES][NAME_MAX];
static uint8_t is_dir[MAX_ENTRIES];
static int  n_entries = 0, sel = 0, top = 0;

enum { S_BROWSE, S_VIEW };
static int state = S_BROWSE;

/* ── Row buffers ──────────────────────────────────────────────────────────
 * Downscaling samples across a full source scanline, so rowbuf must hold an
 * entire BMP row (up to MAX_SRC_W source pixels).  linebuf holds the scaled
 * output row, which never exceeds the panel width. */
#define MAX_SRC_W 2048                     /* widest source image we accept */
#define MAX_IMG_W 400                      /* widest output row (Sharp panel) */
static uint8_t  rowbuf[MAX_SRC_W*3 + 4];   /* one BMP scanline incl. padding */
static uint16_t linebuf[MAX_IMG_W];        /* one output row in RGB565 */
static char err_msg[40];

/* ── Helpers ─────────────────────────────────────────────────────────── */
static int  slen(const char *s){ int n=0; while(s&&s[n]) n++; return n; }
static void scopy(char*d,const char*s,int cap){int i=0;for(;s[i]&&i<cap-1;i++)d[i]=s[i];d[i]='\0';}
static void join(char*o,const char*d,const char*n,int cap){
    int p=0; for(int i=0;d[i]&&p<cap-1;i++)o[p++]=d[i];
    if(p>0&&p<cap-1)o[p++]='/'; for(int i=0;n[i]&&p<cap-1;i++)o[p++]=n[i]; o[p]='\0';
}
static uint32_t rd_u32(const uint8_t*b){ return b[0]|(b[1]<<8)|(b[2]<<16)|((uint32_t)b[3]<<24); }
static int32_t  rd_i32(const uint8_t*b){ return (int32_t)rd_u32(b); }
static uint16_t rd_u16(const uint8_t*b){ return (uint16_t)(b[0]|(b[1]<<8)); }

/* Case-insensitive ".bmp" suffix test. */
static int is_bmp(const char *name) {
    int n = slen(name);
    if (n < 4) return 0;
    const char *e = name + n - 4;
    char a=e[0],b=e[1],c=e[2],d=e[3];
    if (a>='A'&&a<='Z') a+=32; if (b>='A'&&b<='Z') b+=32;
    if (c>='A'&&c<='Z') c+=32; if (d>='A'&&d<='Z') d+=32;
    return a=='.'&&b=='b'&&c=='m'&&d=='p';
}

/* ── Directory load ──────────────────────────────────────────────────── */
static void load_dir(void) {
    char buf[1536];
    n_entries=0; sel=0; top=0;
    int n = fs_readdir(cur_dir[0]?cur_dir:".", buf, sizeof(buf));
    if (n <= 0) return;
    int pos=0;
    while (pos<n && n_entries<MAX_ENTRIES) {
        int start=pos;
        while (pos<n && buf[pos]!='\n' && buf[pos]!='\0') pos++;
        int len=pos-start;
        if (len>0) {
            if (len>NAME_MAX-1) len=NAME_MAX-1;
            for (int i=0;i<len;i++) entries[n_entries][i]=buf[start+i];
            entries[n_entries][len]='\0';
            char full[PATH_MAX]; join(full,cur_dir,entries[n_entries],sizeof(full));
            akira_stat_t st;
            is_dir[n_entries] = (fs_stat(full,&st)==0 && st.type==1)?1:0;
            n_entries++;
        }
        if (pos<n && buf[pos]=='\n') pos++;
        else if (pos<n && buf[pos]=='\0') break;
    }
}

/* ── Chrome ──────────────────────────────────────────────────────────── */
static int text_px(const char*s){ return slen(s)*6; }
static void header(const char*t,const char*r){
    display_rect(0,0,SCR_W,HDR_H,C_HDR); display_text(6,5,t,C_HDR_TXT);
    if(r) display_text(SCR_W-6-text_px(r),5,r,C_HDR_TXT);
}
static void footer(const char*h){
    display_rect(0,SCR_H-FTR_H,SCR_W,FTR_H,C_FOOT_BG); display_text(4,SCR_H-FTR_H+2,h,C_DIM);
}

/* ── BMP viewer ───────────────────────────────────────────────────────────
 * Streams one scanline at a time.  Returns 0 on success, negative on a
 * format problem (err_msg is set for the on-screen message).             */
static int view_bmp(const char *path) {
    err_msg[0]='\0';
    int fd = fs_open(path, AKIRA_FS_O_READ);
    if (fd < 0) { scopy(err_msg,"cannot open file",sizeof(err_msg)); return -1; }

    uint8_t hdr[54];
    if (fs_read(fd, hdr, 54) != 54 || hdr[0]!='B' || hdr[1]!='M') {
        scopy(err_msg,"not a BMP file",sizeof(err_msg)); fs_close(fd); return -1;
    }
    uint32_t data_off = rd_u32(hdr+10);
    int32_t  w  = rd_i32(hdr+18);
    int32_t  h  = rd_i32(hdr+22);
    uint16_t bpp = rd_u16(hdr+28);
    uint32_t comp = rd_u32(hdr+30);

    int top_down = 0;
    if (h < 0) { h = -h; top_down = 1; }

    if (bpp != 24 || comp != 0) {
        scopy(err_msg,"need 24bpp uncompressed",sizeof(err_msg)); fs_close(fd); return -1;
    }
    if (w <= 0 || h <= 0 || h > 4096) {
        scopy(err_msg,"bad dimensions",sizeof(err_msg)); fs_close(fd); return -1;
    }
    if (w > MAX_SRC_W) {
        scopy(err_msg,"image too wide (max 2048)",sizeof(err_msg)); fs_close(fd); return -1;
    }

    uint32_t stride = ((uint32_t)w*3 + 3) & ~3u;   /* rows are 4-byte aligned */

    /* Fit: integer downscale so the image sits inside the content area. */
    int avail_h = SCR_H - HDR_H - FTR_H;
    int step = 1;
    while (w/step > SCR_W || h/step > avail_h) step++;
    int ow = w/step, oh = h/step;
    if (ow < 1) ow = 1; if (oh < 1) oh = 1;
    if (ow > MAX_IMG_W) ow = MAX_IMG_W;

    int ox = (SCR_W - ow)/2;
    int oy = HDR_H + (avail_h - oh)/2;

    display_clear(C_BG);

    for (int y = 0; y < oh; y++) {
        int src_y = y*step;
        /* BMP is bottom-up unless the height was negative. */
        int file_row = top_down ? src_y : (h - 1 - src_y);
        if (fs_seek(fd, (int32_t)(data_off + (uint32_t)file_row*stride),
                    AKIRA_FS_SEEK_SET) < 0) break;
        uint32_t want = stride; if (want > sizeof(rowbuf)) want = sizeof(rowbuf);
        if ((uint32_t)fs_read(fd, rowbuf, want) < want) break;

        for (int x = 0; x < ow; x++) {
            int sx = x*step;
            const uint8_t *px = rowbuf + sx*3;      /* B, G, R */
            uint16_t r = px[2] >> 3, g = px[1] >> 2, b = px[0] >> 3;
            linebuf[x] = (uint16_t)((r<<11)|(g<<5)|b);
        }
        display_bitmap(ox, oy + y, ow, 1, linebuf, (uint32_t)ow*2);
    }

    fs_close(fd);

    /* Caption bar */
    char cap[40]; int p=0;
    /* "WxH" */
    int vals[2] = { (int)w, (int)h };
    for (int k=0;k<2;k++){
        int v=vals[k]; char tmp[8]; int t=0;
        if (v==0) tmp[t++]='0';
        while (v>0){ tmp[t++]=(char)('0'+v%10); v/=10; }
        while (t>0) cap[p++]=tmp[--t];
        if (k==0) cap[p++]='x';
    }
    cap[p]='\0';
    footer(cap);
    display_flush();
    return 0;
}

/* ── Browse render ───────────────────────────────────────────────────── */
static void draw_browse(void) {
    display_clear(C_BG);
    header("IMAGES", cur_dir[0]?cur_dir:"/");

    int row_h=14, area_y=HDR_H+4;
    int rows=(SCR_H-area_y-FTR_H-2)/row_h;
    if (sel<top) top=sel;
    if (sel>=top+rows) top=sel-rows+1;

    if (n_entries==0) {
        display_text(10, area_y+10, "(empty sandbox)", C_DIM);
        display_text(10, area_y+26, "Drop .bmp files into this", C_DIM);
        display_text(10, area_y+40, "app's directory.", C_DIM);
    }
    for (int i=0;i<rows && top+i<n_entries;i++) {
        int idx=top+i, y=area_y+i*row_h;
        if (idx==sel) display_rect(0,y-1,SCR_W,row_h,C_SEL_BG);
        uint16_t col = is_dir[idx]?C_DIR:(is_bmp(entries[idx])?C_IMG:C_DIM);
        display_text(10,y+2, is_dir[idx]?"/":" ", col);
        display_text(20,y+2, entries[idx], col);
    }
    footer("UP/DN move  A view  B up/exit");
    display_flush();
}

static void draw_error(void) {
    display_clear(C_BG);
    header("IMAGE", "ERR");
    display_text(10, HDR_H+24, err_msg[0]?err_msg:"decode failed", C_ERR);
    footer("A/B back");
    display_flush();
}

/* ── Input ───────────────────────────────────────────────────────────── */
static int browse_input(int edge) {
    int rows=(SCR_H-(HDR_H+4)-FTR_H-2)/14;
    if (edge&AKIRA_BTN_UP)    sel=(sel>0)?sel-1:(n_entries?n_entries-1:0);
    if (edge&AKIRA_BTN_DOWN)  sel=(n_entries&&sel<n_entries-1)?sel+1:0;
    if (edge&AKIRA_BTN_LEFT)  sel=(sel-rows>0)?sel-rows:0;
    if (edge&AKIRA_BTN_RIGHT) sel=(sel+rows<n_entries)?sel+rows:(n_entries?n_entries-1:0);

    if ((edge&AKIRA_BTN_A) && n_entries>0) {
        char full[PATH_MAX]; join(full,cur_dir,entries[sel],sizeof(full));
        if (is_dir[sel]) { scopy(cur_dir,full,sizeof(cur_dir)); load_dir(); }
        else {
            if (view_bmp(full) < 0) draw_error();
            state=S_VIEW;
        }
    }
    if (edge&AKIRA_BTN_B) {
        if (cur_dir[0]=='\0') return 1;
        int i=slen(cur_dir); while(i>0&&cur_dir[i-1]!='/') i--;
        cur_dir[i>0?i-1:0]='\0'; load_dir();
    }
    return 0;
}

/* ── Entry point ─────────────────────────────────────────────────────── */
int main(void) {
    printf("AkiraOS Image Viewer");
    display_get_size(&SCR_W, &SCR_H);

    load_dir();
    int prev=(int)input_get_buttons();
    draw_browse();

    while (1) {
        int held=(int)input_get_buttons();
        int edge=held&~prev; prev=held;

        if (state==S_BROWSE) {
            if (edge) { if (browse_input(edge)) { app_switch("supervisor"); return 0; } }
            draw_browse();
        } else {
            /* VIEW: the image is already on-screen; wait for A/B to return. */
            if (edge & (AKIRA_BTN_A|AKIRA_BTN_B)) { state=S_BROWSE; draw_browse(); }
        }
        delay(30000);
    }
    return 0;
}
