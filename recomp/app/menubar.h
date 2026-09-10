/* menubar: the desktop-style menu bar drawn across the top of the window.
 *
 * SDL3 has no native menus, so the bar is drawn by the app with render primitives and
 * the app's own 5x7 font (font5x7.c), above the game viewport. Mouse-driven, with a
 * keyboard fallback: Alt or F10 focuses the bar, arrows move, Enter selects, Escape
 * closes. It never touches emulator state; every item it can produce is an action the
 * caller performs.
 */
#ifndef MENUBAR_H
#define MENUBAR_H

#include <stdbool.h>

#include <SDL3/SDL.h>

typedef enum {
  MENU_ACT_NONE = 0,
  MENU_ACT_QUIT,
  MENU_ACT_SCALE,           /* arg = 1..4 */
  MENU_ACT_FIT,             /* integer fit to window */
  MENU_ACT_ASPECT,          /* arg = 0: 8:7 square pixels, 1: 4:3 */
  MENU_ACT_FULLSCREEN,
  MENU_ACT_GALLERY,         /* arg = gallery section */
  MENU_ACT_GALLERY_CLOSE
} MenuActionKind;

typedef struct {
  MenuActionKind kind;
  int arg;
} MenuAction;

/* What the bar needs to know to draw its ticks. The app owns all of it. */
typedef struct {
  int scaleMode;            /* 0 = fit to window, 1..4 = fixed scale */
  int aspect;               /* 0 = 8:7, 1 = 4:3 */
  bool fullscreen;
  bool galleryOpen;
  int gallerySection;
} MenuModel;

typedef struct Menubar Menubar;

Menubar* menubar_create(SDL_Renderer* renderer);
void menubar_destroy(Menubar* mb);

/* Height in output pixels for a window this wide (the bar scales with the window). */
int menubar_height(int outW);

bool menubar_focused(const Menubar* mb);
void menubar_close(Menubar* mb);

/* Handle one event. Returns true when the bar consumed it; `out` is filled with the
 * action the user picked, or MENU_ACT_NONE. */
bool menubar_event(Menubar* mb, SDL_Renderer* renderer, SDL_Event* ev,
                   const MenuModel* model, MenuAction* out);

void menubar_draw(Menubar* mb, SDL_Renderer* renderer, int outW, const MenuModel* model);

#endif
