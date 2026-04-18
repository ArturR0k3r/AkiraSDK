/*
 * assets_tiles_nature.h — Tree, bush, flower, rock tiles (T_TREE_TL..T_TALL_GRASS)
 */
#ifndef ASSETS_TILES_NATURE_H
#define ASSETS_TILES_NATURE_H

#include "assets_palette.h"

/* T_TREE_TL (35) — top-left of 2×2 tree canopy */
static const uint16_t tile_tree_tl[64] = {
    G3, G3, G3, G1, G1, G2, G3, G3,
    G3, G3, G1, G2, G2, G1, G1, G3,
    G3, G1, G2, G4, G3, G2, G1, G3,
    G1, G2, G3, G4, G3, G2, G1, G1,
    G1, G2, G4, G3, G4, G2, G1, G2,
    G1, G1, G2, G3, G3, G2, G2, G1,
    G3, G1, G1, G2, G2, G1, G1, G2,
    G3, G3, G1, G1, G1, B0, G3, G3,
};

/* T_TREE_TR (36) — top-right of 2×2 tree */
static const uint16_t tile_tree_tr[64] = {
    G3, G2, G1, G1, G3, G3, G3, G3,
    G1, G1, G2, G2, G1, G3, G3, G3,
    G1, G2, G3, G4, G2, G1, G3, G3,
    G1, G2, G4, G3, G3, G2, G1, G3,
    G2, G2, G3, G4, G3, G2, G1, G3,
    G1, G2, G3, G3, G2, G1, G1, G3,
    G2, G1, G2, G2, G1, G1, G3, G3,
    G3, G3, B0, G1, G1, G3, G3, G3,
};

/* T_TREE_BL (37) — bottom-left, trunk visible */
static const uint16_t tile_tree_bl[64] = {
    G3, G3, G1, G1, G2, G1, G3, G3,
    G3, G3, G1, G2, G2, G1, G3, G3,
    G3, G3, G3, G1, G1, G3, G3, G3,
    G3, G3, G3, B0, B0, G3, G3, G3,
    G3, G3, G3, B0, B1, G3, G3, G3,
    G3, G3, G3, B0, B1, G3, G3, G3,
    G3, G2, G3, B0, B1, G3, G2, G3,
    G3, G3, G2, B0, B1, G2, G3, G3,
};

/* T_TREE_BR (38) — bottom-right, trunk visible */
static const uint16_t tile_tree_br[64] = {
    G3, G1, G2, G1, G1, G3, G3, G3,
    G3, G1, G2, G2, G1, G3, G3, G3,
    G3, G3, G1, G1, G3, G3, G3, G3,
    G3, G3, B0, B1, G3, G3, G3, G3,
    G3, G3, B0, B1, G3, G3, G3, G3,
    G3, G3, B0, B1, G3, G3, G3, G3,
    G3, G3, B0, B1, G3, G2, G3, G3,
    G3, G2, B0, B1, G3, G3, G2, G3,
};

/* T_BUSH (39) — small round bush on grass */
static const uint16_t tile_bush[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G1, G1, G1, G1, G3, G3,
    G3, G1, G2, G4, G3, G2, G1, G3,
    G3, G1, G3, G3, G4, G3, G1, G3,
    G3, G1, G2, G4, G3, G2, G1, G3,
    G3, G1, G1, G2, G2, G1, G1, G3,
    G3, G3, G3, G1, G1, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_FLOWER (40) — flower patch on grass */
static const uint16_t tile_flower[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, E2, G3, G3, D1, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, P2, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, E2, G3, G3,
    G3, D1, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, P2, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_ROCK (41) — large boulder on grass */
static const uint16_t tile_rock[64] = {
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, A2, A2, A3, A2, G3, G3,
    G3, A2, A3, A3, A4, A3, A2, G3,
    G3, A2, A3, A4, A3, A3, A2, G3,
    G3, A2, A3, A3, A3, A2, A2, G3,
    G3, G3, A1, A2, A2, A1, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
    G3, G3, G3, G3, G3, G3, G3, G3,
};

/* T_TALL_GRASS (42) — darker tall grass */
static const uint16_t tile_tall_grass[64] = {
    G2, G2, G2, G2, G2, G2, G2, G2,
    G2, G2, G2, G2, G2, G2, G2, G2,
    G2, G2, G4, G2, G2, G4, G2, G2,
    G2, G2, G2, G2, G2, G2, G2, G2,
    G2, G4, G2, G2, G4, G2, G2, G2,
    G2, G2, G2, G2, G2, G2, G2, G2,
    G2, G2, G2, G4, G2, G2, G2, G2,
    G2, G2, G2, G2, G2, G2, G2, G2,
};

#endif /* ASSETS_TILES_NATURE_H */
