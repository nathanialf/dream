/* menubar: see menubar.h.
 *
 * Everything is laid out in "logical" units (the same units the 5x7 font is drawn in)
 * and multiplied by an integer UI scale that follows the window width, so the bar looks
 * the same at every window size and hit-testing is one division away from the layout.
 */
#include "menubar.h"

#include <string.h>

#include "font5x7.h"
#include "gallery.h"

/* logical metrics */
#define L_BAR_H     13
#define L_TITLE_PAD 6
#define L_ITEM_H    10
#define L_ITEM_PAD  4
#define L_CHECK_W   10
#define L_SEP_H     4

#define ATLAS_GLYPHS 95
#define ATLAS_CELL   6

typedef enum { CHK_NONE = 0, CHK_SCALE, CHK_FIT, CHK_ASPECT, CHK_FULL, CHK_SECTION } CheckKind;

typedef struct {
  const char* label;       /* NULL: a gallery section name, taken from gallery.c */
  MenuActionKind kind;
  int arg;
  CheckKind check;
} MenuItem;

typedef struct {
  const char* title;
  const MenuItem* items;
  int count;
} MenuDef;

static const MenuItem kFileItems[] = {
  { "Quit", MENU_ACT_QUIT, 0, CHK_NONE },
};

static const MenuItem kViewItems[] = {
  { "Scale 1x", MENU_ACT_SCALE, 1, CHK_SCALE },
  { "Scale 2x", MENU_ACT_SCALE, 2, CHK_SCALE },
  { "Scale 3x", MENU_ACT_SCALE, 3, CHK_SCALE },
  { "Scale 4x", MENU_ACT_SCALE, 4, CHK_SCALE },
  { "Fit to window", MENU_ACT_FIT, 0, CHK_FIT },
  { "-", MENU_ACT_NONE, 0, CHK_NONE },
  { "Aspect 8:7 (square pixels)", MENU_ACT_ASPECT, 0, CHK_ASPECT },
  { "Aspect 4:3", MENU_ACT_ASPECT, 1, CHK_ASPECT },
  { "-", MENU_ACT_NONE, 0, CHK_NONE },
  { "Fullscreen", MENU_ACT_FULLSCREEN, 0, CHK_FULL },
};

static const MenuItem kGalleryItems[] = {
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_SCENES, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_SPRITES, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_SPRITES_ALT, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_BACKGROUNDS, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_FONTS, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_PREV_BUILD, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_MUSIC, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_SAMPLES, CHK_SECTION },
  { NULL, MENU_ACT_GALLERY, GALLERY_SEC_STALE, CHK_SECTION },
  { "-", MENU_ACT_NONE, 0, CHK_NONE },
  { "Close gallery", MENU_ACT_GALLERY_CLOSE, 0, CHK_NONE },
};

#define COUNT(a) ((int) (sizeof(a) / sizeof((a)[0])))

static const MenuDef kMenus[] = {
  { "File", kFileItems, COUNT(kFileItems) },
  { "View", kViewItems, COUNT(kViewItems) },
  { "Gallery", kGalleryItems, COUNT(kGalleryItems) },
};
#define MENU_COUNT COUNT(kMenus)
#define BACK_INDEX MENU_COUNT      /* the "Back" title, shown only with a page open */

struct Menubar {
  SDL_Texture* atlas;
  int open;          /* -1, else a menu index */
  int hoverTitle;    /* -1, else a title index (BACK_INDEX for Back) */
  int hoverItem;     /* -1, else an item index in the open menu */
  bool focused;      /* the keyboard has the bar */
  float mouseX, mouseY;
};

/* ---- text ---------------------------------------------------------------------- */

static SDL_Texture* build_atlas(SDL_Renderer* renderer) {
  const int w = ATLAS_GLYPHS * ATLAS_CELL, h = FONT5X7_H + 1;
  uint32_t* px = SDL_calloc((size_t) w * (size_t) h, sizeof(uint32_t));
  if(px == NULL) return NULL;
  for(int gi = 0; gi < ATLAS_GLYPHS; gi++)
    for(int y = 0; y < FONT5X7_H; y++)
      for(int x = 0; x < FONT5X7_W; x++)
        if(font5x7_pixel((char) (0x20 + gi), x, y))
          px[y * w + gi * ATLAS_CELL + x] = 0xFFFFFFFFu;
  SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STATIC, w, h);
  if(t != NULL) {
    SDL_UpdateTexture(t, NULL, px, w * (int) sizeof(uint32_t));
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
  }
  SDL_free(px);
  return t;
}

static int text_w(const char* s) {
  return (int) strlen(s) * FONT5X7_ADVANCE;
}

static void draw_text(Menubar* mb, SDL_Renderer* r, int s, float x, float y,
                      const char* str, Uint8 cr, Uint8 cg, Uint8 cb) {
  if(mb->atlas == NULL) return;
  SDL_SetTextureColorMod(mb->atlas, cr, cg, cb);
  for(const char* p = str; *p != 0; p++) {
    unsigned c = (unsigned char) *p;
    if(c >= 0x20 && c < 0x20 + ATLAS_GLYPHS) {
      SDL_FRect src = { (float) ((int) (c - 0x20) * ATLAS_CELL), 0.0f,
                        (float) FONT5X7_W, (float) FONT5X7_H };
      SDL_FRect dst = { x, y, (float) (FONT5X7_W * s), (float) (FONT5X7_H * s) };
      SDL_RenderTexture(r, mb->atlas, &src, &dst);
    }
    x += (float) (FONT5X7_ADVANCE * s);
  }
}

static void fill(SDL_Renderer* r, float x, float y, float w, float h,
                 Uint8 cr, Uint8 cg, Uint8 cb) {
  SDL_FRect rc = { x, y, w, h };
  SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
  SDL_RenderFillRect(r, &rc);
}

/* ---- layout -------------------------------------------------------------------- */

static int ui_scale(int outW) {
  int s = outW / 256;
  if(s < 1) s = 1;
  if(s > 3) s = 3;
  return s;
}

int menubar_height(int outW) {
  return L_BAR_H * ui_scale(outW);
}

static const char* item_label(const MenuItem* it) {
  return it->label != NULL ? it->label : gallery_section_name(it->arg);
}

static bool item_selectable(const MenuItem* it) {
  return it->kind != MENU_ACT_NONE;
}

/* Logical x of a title and its width. Index MENU_COUNT is "Back". */
static void title_box(int index, const MenuModel* model, int* x, int* w) {
  int cur = 4;
  for(int i = 0; i < MENU_COUNT; i++) {
    int tw = text_w(kMenus[i].title) + 2 * L_TITLE_PAD;
    if(i == index) { *x = cur; *w = tw; return; }
    cur += tw;
  }
  int tw = text_w("Back") + 2 * L_TITLE_PAD;
  *x = model->galleryOpen ? cur : -1000;
  *w = tw;
}

static int panel_width(const MenuDef* def) {
  int w = 0;
  for(int i = 0; i < def->count; i++) {
    int t = text_w(item_label(&def->items[i]));
    if(t > w) w = t;
  }
  return w + L_CHECK_W + 2 * L_ITEM_PAD + 4;
}

static int item_y(const MenuDef* def, int index) {
  int y = L_BAR_H + L_ITEM_PAD;
  for(int i = 0; i < index; i++)
    y += item_selectable(&def->items[i]) ? L_ITEM_H : L_SEP_H;
  return y;
}

static int panel_height(const MenuDef* def) {
  return item_y(def, def->count) - L_BAR_H + L_ITEM_PAD;
}

static bool checked(const MenuItem* it, const MenuModel* m) {
  switch(it->check) {
    case CHK_SCALE:   return m->scaleMode == it->arg;
    case CHK_FIT:     return m->scaleMode == 0;
    case CHK_ASPECT:  return m->aspect == it->arg;
    case CHK_FULL:    return m->fullscreen;
    case CHK_SECTION: return m->galleryOpen && m->gallerySection == it->arg;
    default:          return false;
  }
}

/* ---- events -------------------------------------------------------------------- */

static int hit_title(float lx, float ly, const MenuModel* model) {
  if(ly < 0.0f || ly >= (float) L_BAR_H) return -1;
  for(int i = 0; i <= MENU_COUNT; i++) {
    int x, w;
    title_box(i, model, &x, &w);
    if(x < 0) continue;
    if(lx >= (float) x && lx < (float) (x + w)) return i;
  }
  return -1;
}

static int hit_item(const Menubar* mb, float lx, float ly, const MenuModel* model) {
  if(mb->open < 0) return -1;
  const MenuDef* def = &kMenus[mb->open];
  int px = 0, pw = 0;
  title_box(mb->open, model, &px, &pw);
  (void) pw;
  int width = panel_width(def);
  if(lx < (float) px || lx >= (float) (px + width)) return -1;
  for(int i = 0; i < def->count; i++) {
    if(!item_selectable(&def->items[i])) continue;
    int y = item_y(def, i);
    if(ly >= (float) y && ly < (float) (y + L_ITEM_H)) return i;
  }
  return -1;
}

static void step_item(Menubar* mb, int dir) {
  if(mb->open < 0) return;
  const MenuDef* def = &kMenus[mb->open];
  int i = mb->hoverItem;
  for(int guard = 0; guard < def->count * 2 + 2; guard++) {
    i += dir;
    if(i < 0) i = def->count - 1;
    if(i >= def->count) i = 0;
    if(item_selectable(&def->items[i])) { mb->hoverItem = i; return; }
  }
}

static void step_menu(Menubar* mb, int dir) {
  int m = mb->open < 0 ? 0 : mb->open;
  m = (m + dir + MENU_COUNT) % MENU_COUNT;
  mb->open = m;
  mb->hoverItem = -1;
  step_item(mb, +1);
}

static bool activate(Menubar* mb, int item, MenuAction* out) {
  if(mb->open < 0 || item < 0) return false;
  const MenuDef* def = &kMenus[mb->open];
  if(item >= def->count || !item_selectable(&def->items[item])) return false;
  out->kind = def->items[item].kind;
  out->arg = def->items[item].arg;
  menubar_close(mb);
  return true;
}

bool menubar_event(Menubar* mb, SDL_Renderer* renderer, SDL_Event* ev,
                   const MenuModel* model, MenuAction* out) {
  out->kind = MENU_ACT_NONE;
  out->arg = 0;
  int outW = 0, outH = 0;
  SDL_GetRenderOutputSize(renderer, &outW, &outH);
  int s = ui_scale(outW);

  switch(ev->type) {
    case SDL_EVENT_MOUSE_MOTION: {
      SDL_ConvertEventToRenderCoordinates(renderer, ev);
      mb->mouseX = ev->motion.x;
      mb->mouseY = ev->motion.y;
      float lx = mb->mouseX / (float) s, ly = mb->mouseY / (float) s;
      int t = hit_title(lx, ly, model);
      mb->hoverTitle = t;
      if(mb->open >= 0) {
        if(t >= 0 && t < MENU_COUNT) { mb->open = t; mb->hoverItem = -1; }
        int it = hit_item(mb, lx, ly, model);
        if(it >= 0) mb->hoverItem = it;
        return true;
      }
      return t >= 0;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      SDL_ConvertEventToRenderCoordinates(renderer, ev);
      float lx = ev->button.x / (float) s, ly = ev->button.y / (float) s;
      int t = hit_title(lx, ly, model);
      if(t == BACK_INDEX) {
        menubar_close(mb);
        out->kind = MENU_ACT_GALLERY_CLOSE;
        return true;
      }
      if(t >= 0) {
        if(mb->open == t) menubar_close(mb);
        else { mb->open = t; mb->hoverItem = -1; mb->focused = true; }
        return true;
      }
      int it = hit_item(mb, lx, ly, model);
      if(it >= 0) { activate(mb, it, out); return true; }
      if(mb->open >= 0) { menubar_close(mb); return true; }
      return false;
    }
    case SDL_EVENT_KEY_DOWN: {
      SDL_Keycode k = ev->key.key;
      if(k == SDLK_F10 || k == SDLK_LALT || k == SDLK_RALT) {
        if(mb->open >= 0 || mb->focused) menubar_close(mb);
        else { mb->focused = true; mb->open = 0; mb->hoverItem = -1; step_item(mb, +1); }
        return true;
      }
      if(mb->open < 0 && !mb->focused) return false;
      switch(k) {
        case SDLK_ESCAPE: menubar_close(mb); return true;
        case SDLK_LEFT:   step_menu(mb, -1); return true;
        case SDLK_RIGHT:  step_menu(mb, +1); return true;
        case SDLK_UP:     step_item(mb, -1); return true;
        case SDLK_DOWN:   step_item(mb, +1); return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:  activate(mb, mb->hoverItem, out); return true;
        default:          return true;   /* the bar has the keyboard while it is open */
      }
    }
    default:
      return false;
  }
}

/* ---- drawing ------------------------------------------------------------------- */

void menubar_draw(Menubar* mb, SDL_Renderer* renderer, int outW, const MenuModel* model) {
  int s = ui_scale(outW);
  float S = (float) s;
  fill(renderer, 0, 0, (float) outW, (float) (L_BAR_H * s), 0x20, 0x24, 0x2C);
  fill(renderer, 0, (float) (L_BAR_H * s - s), (float) outW, S, 0x12, 0x14, 0x1A);

  for(int i = 0; i <= MENU_COUNT; i++) {
    int x, w;
    title_box(i, model, &x, &w);
    if(x < 0) continue;
    const char* label = i < MENU_COUNT ? kMenus[i].title : "Back";
    bool hot = (mb->open == i) || (mb->hoverTitle == i);
    if(hot) fill(renderer, x * S, 0, w * S, (float) ((L_BAR_H - 1) * s), 0x36, 0x40, 0x5C);
    draw_text(mb, renderer, s, (float) (x + L_TITLE_PAD) * S, 3.0f * S, label,
              0xE4, 0xE8, 0xF0);
  }

  if(mb->open < 0) return;
  const MenuDef* def = &kMenus[mb->open];
  int px = 0, pw = 0;
  title_box(mb->open, model, &px, &pw);
  (void) pw;
  int width = panel_width(def), height = panel_height(def);
  fill(renderer, px * S, (float) (L_BAR_H * s), width * S, height * S, 0x18, 0x1C, 0x24);
  SDL_SetRenderDrawColor(renderer, 0x44, 0x4C, 0x60, 255);
  SDL_FRect border = { px * S, (float) (L_BAR_H * s), width * S, height * S };
  SDL_RenderRect(renderer, &border);

  for(int i = 0; i < def->count; i++) {
    const MenuItem* it = &def->items[i];
    int y = item_y(def, i);
    if(!item_selectable(it)) {
      fill(renderer, (px + L_ITEM_PAD) * S, (float) ((y + L_SEP_H / 2) * s),
           (float) (width - 2 * L_ITEM_PAD) * S, S, 0x3A, 0x42, 0x54);
      continue;
    }
    bool hot = mb->hoverItem == i;
    if(hot) fill(renderer, (px + 1) * S, (float) (y * s), (float) (width - 2) * S,
                 (float) (L_ITEM_H * s), 0x2E, 0x5A, 0x9A);
    if(checked(it, model))
      fill(renderer, (px + L_ITEM_PAD + 2) * S, (float) ((y + 4) * s), 3 * S, 3 * S,
           0xE8, 0xC0, 0x50);
    draw_text(mb, renderer, s, (float) (px + L_ITEM_PAD + L_CHECK_W) * S,
              (float) ((y + 2) * s), item_label(it), 0xE4, 0xE8, 0xF0);
  }
}

/* ---- lifetime ------------------------------------------------------------------ */

Menubar* menubar_create(SDL_Renderer* renderer) {
  Menubar* mb = SDL_calloc(1, sizeof(Menubar));
  if(mb == NULL) return NULL;
  mb->atlas = build_atlas(renderer);
  mb->open = -1;
  mb->hoverTitle = -1;
  mb->hoverItem = -1;
  return mb;
}

void menubar_destroy(Menubar* mb) {
  if(mb == NULL) return;
  if(mb->atlas != NULL) SDL_DestroyTexture(mb->atlas);
  SDL_free(mb);
}

bool menubar_focused(const Menubar* mb) {
  return mb != NULL && (mb->focused || mb->open >= 0);
}

void menubar_close(Menubar* mb) {
  if(mb == NULL) return;
  mb->open = -1;
  mb->hoverItem = -1;
  mb->focused = false;
}
