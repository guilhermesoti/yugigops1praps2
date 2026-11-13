#include "../types.h"
#include "../psyq/libgte.h"
#include "../psyq/libgpu.h"
#include "../psyq/libgs.h"
#include "display_object.h"
#include "display_object_layout.h"
#include "display_object_projection.h"
#define GRAPHICS_VIEWPORT_IN_DATA
#include "graphics_frame.h"
#include "sprite_primitive.h"
#include "display_object_packet_submit.h"
#include "display_object_render_sprite_sheet.h"
/* Caching this pointer shortens retail; each use must reload field_4C. */
#define SPRITE_SHEET_HEADER(object) \
    ((SpriteSheetHeader *G32)(object)->field_4C)
#define GS_SPRITE_VIEW(sprite) ((GsSPRITE *)(sprite))
#define DISPLAY_OBJECT_COLOR_BYTES(object) ((u8 *)&(object)->field_0C)

#ifdef MEMORIES_PC
#include <string.h>
#include "pc/cards/pack_shop.h"
#include "pc/cards/card_layout_art.h"
#include "duel_card_viewer.h"
#include "duel_effect_resource_record.h"
#include "pc/debug/log.h"

/* One piece of a style's card-viewer back (back_image), on the on-screen
 * rectangle retail's own loop below gives the same SpriteSheetPart, sampling
 * the style's bank cell instead of retail's page. GsSortFastSprite's packet
 * only keeps 5 bits of tpage, so this is a POLY_FT4 (a full-word tpage).
 * `flip` swaps the u columns, as DisplayObject_SubmitPacket does for a
 * sprite with 0x800000. While the card turns, the object is clip-tested:
 * func_80041F90 has left its rotation in the GTE, and `proj` is the origin
 * it wrote, so the piece is projected (and subdivided) the way retail's
 * 0xF0000 submission projects its own; with `proj` NULL the rectangle is
 * already final screen space.
 *
 * `attribute` is the word retail's loop gives each part's sprite: as in
 * DisplayObject_SubmitPacket's default case, its 0x40000000 makes the quad
 * semi-transparent and its bits 28-29 are the rate in the tpage. Its color
 * mode bits (24-25) are not taken: they describe retail's page, and the
 * style's cell already carries its own depth. */
static void submit_back_cell(u32 attribute, u32 rgb, s32 x, s32 y, s32 w, s32 h,
                             s32 tpage, s32 u, s32 v, s32 tex_w, s32 tex_h,
                             s32 clut, s32 flip, s32 ot, s32 depth, u8 *proj)
{
    POLY_FT4 quad;
    setPolyFT4(&quad);
    quad.pad1 = quad.pad2 = 0;
    if (attribute & 0x40000000) setSemiTrans(&quad, 1);
    quad.r0 = (u8)rgb;
    quad.g0 = (u8)(rgb >> 8);
    quad.b0 = (u8)(rgb >> 16);
    quad.tpage = (u16)(tpage | ((attribute >> 23) & 0x60));
    quad.clut = (u16)clut;
    quad.x0 = quad.x2 = (short)x;
    quad.x1 = quad.x3 = (short)(x + w);
    quad.y0 = quad.y1 = (short)y;
    quad.y2 = quad.y3 = (short)(y + h);
    if (flip) {
        quad.u1 = quad.u3 = (u8)u;
        quad.u0 = quad.u2 = (u8)(u + tex_w);
    } else {
        quad.u0 = quad.u2 = (u8)u;
        quad.u1 = quad.u3 = (u8)(u + tex_w);
    }
    quad.v0 = quad.v1 = (u8)v;
    quad.v2 = quad.v3 = (u8)(v + tex_h);
    if (proj) {
        DisplayObject_SubmitProjectedQuad((u8 *)&quad, ot, depth, proj);
    } else {
        GsSortPoly(&quad, (GsOT *G32)ot, (u16)depth);
    }
}
#endif

/* Emits one sprite per sheet part. The object's position is offset by the
 * viewport unless it is screen-space (flag 8). With the clip-test flag (4)
 * the object is projected once through func_80041F90, which may ask
 * (D_8009B424) for the setup to run again, and every part is submitted as a
 * 9-word semi-transparent quad at 0x1F800344. Otherwise each part either
 * goes out as a plain sprite, culled against the 320x240 screen when the
 * attribute's 0x08000000 bit asked for that, or as a rotated/scaled sprite
 * about the object's +0x48 pivot. The scale and color are written through
 * GsSPRITE's own members, which is the view libgs gives the record. */
void DisplayObject_RenderSpriteSheet(DisplayObject *object, s32 ot, s32 depth)
{
    SpritePrim *sprite;
    SpriteSheetWork *work;
    SpriteSheetState *state;
    u8 *quad;
    u8 *sheet;
    SpriteSheetPart *part;
    s32 count;
    s32 x;
    s32 y;
    s32 dx;
    s32 dy;
    s32 value;
    s32 tpage;
    u16 cx;

#ifdef MEMORIES_PC
    /* Retail never sets GsDOFF on a sprite-sheet object, so this check is a
     * no-op there; it exists for card_layout's fallback handling, which
     * sets it (func_80028B08.c) once its replacement frame is fully drawn,
     * to retire the opaque retail frame underneath. */
    if (object->attribute & GsDOFF) return;
#endif
    state = (SpriteSheetState *)SCRATCHPAD_ADDR(0x1F8003A4);
    x = (s16)object->field_30.h.field_30;
    y = (s16)object->field_30.h.field_32;
    if ((object->flags & DISPLAY_OBJECT_FLAG_SCREEN_SPACE) == 0) {
        x -= gGraphics_sViewportX;
        y -= gGraphics_sViewportY;
    }
    work = (SpriteSheetWork *)SCRATCHPAD_ADDR(0x1F800378);
    work->mx = object->field_48.h.field_48;
    work->my = object->field_48.h.field_4A;
    sprite = (SpritePrim *)SCRATCHPAD_ADDR(0x1F800320);
    work->clip = object->flags & DISPLAY_OBJECT_FLAG_CLIP_TEST;
retry:
    work->attribute = object->attribute;
    work->u = ((u8 *)&object->field_5E)[0];
    work->v = ((u8 *)&object->field_5E)[1];
    count = SPRITE_SHEET_HEADER(object)->count;
    if (count == 0) {
        return;
    }
    state->flags = SPRITE_SHEET_HEADER(object)->flags;
    if (work->attribute & 0x01000000) {
        work->wide = 1;
        tpage = object->field_66 + SPRITE_SHEET_HEADER(object)->tpage * 2;
    } else {
        work->wide = 0;
        tpage = object->field_66 + SPRITE_SHEET_HEADER(object)->tpage;
    }
    sprite->tpage = work->tpage = tpage;
    work->cx = object->field_40.h.field_40;
    work->cy = object->field_40.h.field_42;
    if (object->flags & DISPLAY_OBJECT_FLAG_TEXTURE_CELL_OFFSET) {
        work->cx += (SPRITE_SHEET_HEADER(object)->clut & 0xF) << 4;
        work->cy += SPRITE_SHEET_HEADER(object)->clut >> 4;
    }
    sheet = (u8 *G32)object->field_4C;
    sprite->rotate = object->field_20.h.field_22 * 5760;
    GS_SPRITE_VIEW(sprite)->scalex = object->field_44.h.field_44;
    GS_SPRITE_VIEW(sprite)->scaley = object->field_44.h.field_46;
    sprite->mxmy.h.y = 0;
    sprite->mxmy.h.x = 0;
    GS_SPRITE_VIEW(sprite)->r = DISPLAY_OBJECT_COLOR_BYTES(object)[0];
    GS_SPRITE_VIEW(sprite)->g = DISPLAY_OBJECT_COLOR_BYTES(object)[1];
    GS_SPRITE_VIEW(sprite)->b = DISPLAY_OBJECT_COLOR_BYTES(object)[2];
    part = (SpriteSheetPart *)(sheet + sizeof(SpriteSheetHeader));
    sprite->cxcy.h.cx = work->cx;
    sprite->cxcy.h.cy = work->cy;
    state->flip = 0;
    state->mirror = 0;
    state->screen = work->attribute & 0x08000000;
    if (state->screen != 0) {
        work->attribute &= 0xF7FFFFFF;
    }
    if (work->attribute & 0x800000) {
        work->attribute &= 0xF7FFFFFF;
        state->flip = 0x2000;
        state->mirror = 0x2000;
    }
    quad = 0;
    work->mode = 0;
    if (work->clip != 0) {
        quad = (u8 *)SCRATCHPAD_ADDR(0x1F800344);
        *(u32 *)&((POLY_FT4 *)quad)->r0 = sprite->rgb;
        setlen(quad, 9);
        setcode(quad, 0x2C);
        D_8009B424 = 0;
        if (func_80041F90(object, x + (s16)object->field_18, y + (s16)object->field_1A,
                          &work->proj) <= 0) {
            return;
        }
        if (D_8009B424 != 0) {
            goto retry;
        }
        work->mode = 0xF0000;
    }
#ifdef MEMORIES_PC
    /* The front of a card whose place a card pack's whole picture takes
       (pack_shop.h); its back, turning, is drawn. */
    if (PackShop_HidesFrame(object)) {
        return;
    }
    /* The card viewer's own rotating "card turning" sprite
     * (func_800283F4.c's DuelEffect_UpdateCardViewerState; Library, Build
     * Deck and the duel effect card viewer all share it) -- checked by
     * pointer identity against gDuel_pCardViewerCard rather than a
     * repurposed field, since this object already uses field_6A for retail's
     * own variant,
     * and (unlike entry->object_04, the other half of the same pair)
     * field_67 is never set on it -- its card id is read from D_800EA0E8[3]
     * instead, the one resource slot func_800283F4.c and card_browse.c's
     * VIEWER_SLOT both ever pass this object's own constructor
     * (func_800291E0). A style's own back_image replaces retail's swirl
     * outright; "" leaves it alone. */
    /* `gDuel_pCardViewerCard` is a screen-owned publication pointer, not a
     * lifetime-tracked handle.  Closing the viewer releases resource slot 3
     * but leaves that pointer intact; a later menu object can reuse the same
     * slot/address.  Require slot 3 to still own this object as well, so a
     * stale viewer pointer cannot turn an unrelated title-menu sprite into a
     * card back.
     *
     * The same object also draws the card's front plate: CardPreview_
     * UpdateVariant (its field_10 callback, run by func_80041F90 above)
     * switches it to variant 2, the back, while the card faces away and
     * back to its own variant (field_6A) when it faces the viewer. Only
     * variant 2 is the back; the front plate stays retail's (or is retired
     * by func_80028B08.c's GsDOFF when a full-bleed frame replaces it). */
    if (object == gDuel_pCardViewerCard && D_800EA0E8[3].object_00 == object &&
        object->field_69 == 2) {
        s32 back_tpage, back_u, back_v, back_clut, back_w, back_h;
        s32 card_id = (s16)D_800EA0E8[3].field_30;
        /* Without the clip test the retail loop below either sorts fast
         * sprites at (x + dx, y + dy) (the 0x08000000 screen cull) or
         * rotates and scales them about the +0x48 pivot. Only the first, or
         * the second at no rotation and unit scale, is the flat rectangle
         * submit_back_cell draws; anything else keeps retail's own back. */
        s32 flat_exact = state->screen != 0 ||
                         (sprite->rotate == 0 && GS_SPRITE_VIEW(sprite)->scalex == ONE &&
                          GS_SPRITE_VIEW(sprite)->scaley == ONE);
        if ((work->clip != 0 || flat_exact) &&
            CardLayoutArt_BackCell(card_id, &back_tpage, &back_u, &back_v, &back_clut, &back_w, &back_h)) {
            /* Retail's own card-view back is six SpriteSheetPart pieces
             * tiling one logical 144x200 image ((0, 0)-(144, 200), no gaps
             * or overlap; tools/pc/hd_assets_pack.py's BACK_SIZE). Each
             * piece keeps retail's on-screen rectangle -- also the turn
             * animation's geometry -- and samples the part of the style's
             * picture (back_w x back_h, the style's width/height) that
             * covers the same fraction of the card. */
            u32 rgb = DISPLAY_OBJECT_COLOR_BYTES(object)[0] |
                      (DISPLAY_OBJECT_COLOR_BYTES(object)[1] << 8) |
                      (DISPLAY_OBJECT_COLOR_BYTES(object)[2] << 16);
            u8 *proj = work->clip != 0 ? (u8 *)&work->proj : 0;
            s32 pi;
            for (pi = 0; pi < count; pi++) {
                SpriteSheetPart *p = &part[pi];
                s32 pdx, pdy, pw, ph, sx0, sy0, sx1, sy1, sx;
                if (state->flags & 0x10) {
                    pdx = (u8)p->dx | ((p->cell & 0xC000) >> 6);
                    if (pdx & 0x200) pdx |= ~0x1FF;
                    pdy = (u8)p->dy | ((p->size & 0xC000) >> 6);
                    if (pdy & 0x200) pdy |= ~0x1FF;
                } else {
                    pdx = p->dx;
                    pdy = p->dy;
                }
                pw = ((p->size >> 2) & 0x78) + 8;
                ph = ((p->size >> 6) & 0x78) + 8;
                /* Rounding both edges, rather than scaling a width alone,
                 * keeps neighbouring parts contiguous and never reads
                 * beyond back_w/back_h. */
                sx0 = pdx * back_w / 144;
                sy0 = pdy * back_h / 200;
                sx1 = (pdx + pw) * back_w / 144;
                sy1 = (pdy + ph) * back_h / 200;
                /* A mirrored object (attribute 0x800000) puts the piece at
                 * -(dx + w), as retail's loop does, and shows its texels
                 * reversed. A part's own 0x2000 cell bit is not applied:
                 * that marks retail texels stored mirrored, while these are
                 * read from the picture's own unmirrored place. */
                sx = state->mirror != 0 ? -(pdx + pw) : pdx;
                submit_back_cell(work->attribute, rgb, x + sx, y + pdy, pw, ph,
                                 back_tpage, back_u + sx0, back_v + sy0, sx1 - sx0, sy1 - sy0,
                                 back_clut, state->mirror != 0, ot, depth, proj);
            }
            return;
        }
    }
#endif
    do {
        sprite->attribute = work->attribute;
        work->cell = part->cell;
        work->size = part->size;
        sprite->extent.wh.w.word = ((work->size >> 2) & 0x78) + 8;
        sprite->extent.wh.h = ((work->size >> 6) & 0x78) + 8;
        if (state->flags & 0x10) {
            dx = (u8)part->dx | ((part->cell & 0xC000) >> 6);
            if (dx & 0x200) {
                dx |= ~0x1FF;
            }
            dy = (u8)part->dy | ((part->size & 0xC000) >> 6);
            if (dy & 0x200) {
                dy |= ~0x1FF;
            }
        } else {
            dx = part->dx;
            dy = part->dy;
        }
        if (state->mirror != 0) {
            dx = -(dx + sprite->extent.wh.w.word);
        }
        if (state->flags & 0xE0) {
            state->flip = (work->cell ^ state->mirror) & 0x2000;
            if (work->wide != 0) {
                sprite->tpage = work->tpage + ((part->cell >> 9) & 0xE);
                sprite->cxcy.h.cx = work->cx;
                sprite->cxcy.h.cy = work->cy + (work->size & 0x1F);
            } else {
                cx = work->cx;
                value = cx & 0xFF;
                value += (work->size & 0xF) << 4;
                sprite->tpage = work->tpage + ((part->cell >> 10) & 7);
                sprite->cxcy.h.cx = (cx & 0x300) | (value & 0xFF);
                sprite->cxcy.h.cy = work->cy + (value >> 8);
            }
        }
        sprite->xy.h.x = x + dx;
        sprite->xy.h.y = y + dy;
        value = ((work->cell & 0x1F) << 3) + work->u;
        sprite->uv.b.lo = value;
        sprite->uv.b.hi = ((work->cell & 0x3E0) >> 2) + (u8)work->v;
        sprite->tpage += value >> 8;
        /* From here `value` is the DisplayObject_SubmitPacket mode: case 1 sorts a fast
         * sprite, 2 a flipped one, 3 a rotated/scaled one. */
        value = (u16)depth | 0x10000;
        if (state->flip != 0) {
            value = (u16)depth | 0x20000;
            sprite->attribute |= 0x800000;
            if (sprite->uv.b.hi + sprite->extent.wh.h >= 0x100) {
                sprite->extent.wh.h--;
            }
            if (work->wide != 0) {
                if (sprite->uv.b.lo + sprite->extent.wh.w.word >= 0x100 &&
                    (s8)sprite->uv.b.lo < 0 && sprite->tpage != 0x1F) {
                    sprite->tpage++;
                    sprite->uv.b.lo += 0x80;
                    state->flags |= 0x80;
                } else if (sprite->uv.b.lo == 0) {
                    if (sprite->tpage != 0x10) {
                        sprite->tpage--;
                        sprite->uv.b.lo = 0x80;
                        state->flags |= 0x80;
                    } else {
                        goto narrow;
                    }
                }
            } else {
narrow:
                if (sprite->uv.b.lo == 0) {
                    sprite->uv.b.lo++;
                    sprite->extent.wh.w.word--;
                }
            }
        }
        if (work->clip == 0) {
            if (state->screen != 0) {
                if ((s16)sprite->xy.h.x >= 320 ||
                    (s16)sprite->xy.h.x + sprite->extent.wh.w.word <= 0 ||
                    (s16)sprite->xy.h.y >= 240 ||
                    (s16)sprite->xy.h.y + sprite->extent.wh.h <= 0) {
                    goto next;
                }
            } else {
                value = (u16)depth | 0x30000;
                sprite->mxmy.h.x = work->mx - dx;
                sprite->mxmy.h.y = work->my - dy;
                sprite->xy.h.x += sprite->mxmy.h.x;
                sprite->xy.h.y += sprite->mxmy.h.y;
            }
        }
        DisplayObject_SubmitPacket(sprite, quad, ot, value | work->mode, (u8 *)&work->proj);
next:
        part++;
    } while (--count != 0);
}
