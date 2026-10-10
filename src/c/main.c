#include <pebble.h>
#include "font_data.h"

// ---------------------------------------------------------------------------
// PBL-7 — landscape sidescroll shmup for Pebble Time 2 (emery).
// Held sideways (quarter-turn clockwise, right edge down): 228 wide x 200 tall.
// Logic in landscape coords (u:0..227 right, v:0..199 down); ls()/lp() map to
// native and perform the 90-degree rotation, so drawing stays plain primitives.
// Controls: UP hold = rise / DOWN = fire / SELECT = smart bomb / BACK = pause.
// Milestone 3: Mercury vertical slice — approach, The Solar Sentinel boss
// (phase-2 overheat), upgrade cards, MAP screen, collectible + persistence.
// ---------------------------------------------------------------------------

#define TICK_MS 33
#define FP 8

#define SHIP_U 36
#define SHIP_R 7
#define HUD_V 14

#define MAX_STARS 30
#define MAX_PB 12
#define MAX_EN 12
#define MAX_EB 20
#define MAX_PT 20

#define HP_MAX_BASE 6
#define LIVES_START 3
#define SPECIAL_MAX 100
#define FIRE_CD_BASE 6

// Weapon heat: holding DOWN builds heat until the gun jams, so burst-firing
// beats mashing the trigger. Heat cools whenever you are not firing.
#define HEAT_MAX 100
#define HEAT_PER_SHOT 8      // ~12 shots of continuous fire before it jams
#define HEAT_COOL 4          // per tick while not firing
#define JAM_TICKS 22         // ~0.7s locked out

// Approach length ramps gently per planet (~30 fps): ~40s on Mercury up to a
// ~49s cap, so later levels escalate without dragging.
#define APPROACH_BASE 1200
#define APPROACH_PER_PLANET 45
#define APPROACH_CAP 1470
static int approach_ticks_for(int planet) {
  int t = APPROACH_BASE + planet * APPROACH_PER_PLANET;
  return t > APPROACH_CAP ? APPROACH_CAP : t;
}
#define INTRO_TICKS 45          // WARNING card ~1.5s
#define CLEARED_TICKS 40

#define NUM_PLANETS 11
#define PLAYABLE_MAX NUM_PLANETS   // all planets implemented; gated by unlock bitmask
#define NUM_COLLECT 8

// Persist keys
#define PK_UNLOCKED 1
#define PK_COLLECT  2
#define PK_BESTSCORE 3
#define PK_SECRET   4
#define PK_BACKLIGHT 5      // legacy bool; superseded by PK_BL_MODE
#define PK_JAM      6
#define PK_RUMBLE   7
#define PK_SOUND    8          // legacy on/off bool; superseded by PK_SOUND_VOL
#define PK_BL_MODE  9
#define PK_SOUND_VOL 10

#define BL_TIMEOUT_MS 30000

// Landscape -> native helpers. s_native_h is the real screen height, cached in
// window_load: everything else in this file derives from layer_get_bounds(), and
// these two were the only place a 228 was baked in.
static int16_t s_native_h = 228;
static GRect ls(int u, int v, int w, int h) { return GRect(v, s_native_h - u - w, h, w); }
static GPoint lp(int u, int v) { return GPoint(v, s_native_h - u - 1); }

typedef enum {
  ST_TITLE, ST_MAP, ST_PLAYING, ST_PAUSED, ST_UPGRADE, ST_GAMEOVER, ST_VICTORY,
  ST_SECRET_ENDING, ST_SETTINGS
} GameState;
typedef enum { PH_APPROACH, PH_BOSS_INTRO, PH_BOSS, PH_CLEARED } Phase;
// Backlight: off (system default), timeout (lit while interacting, then dims),
// or always (forced 100% on). Mirrors Switchback's 3-state model.
typedef enum { BL_OFF, BL_TIMED, BL_ALWAYS, NUM_BL_MODES } BacklightMode;
typedef enum { EN_NONE, EN_DRONE, EN_FIGHTER, EN_MINE, EN_COLLECT, EN_ACID, EN_TURRET } EnemyKind;
typedef enum { UP_TIMELINE, UP_ARMOR, UP_SHIELD, UP_REBBLE, NUM_UPGRADES } UpgradeType;

typedef struct { int32_t u_fp; int16_t v; uint8_t layer; } Star;
typedef struct { bool on; int32_t u_fp; int16_t v; } Shot;
// kind: 0 = normal, 1 = fireball (big), 2 = ring (splits when ttl expires)
typedef struct { bool on; int32_t u_fp, v_fp, vu_fp, vv_fp; uint8_t kind; int16_t ttl; } EBullet;
typedef struct {
  EnemyKind kind; int32_t u_fp, v_fp; int16_t base_v; uint16_t phase;
  int8_t hp; int16_t fire_cd; int16_t r;
} Enemy;
typedef struct { bool on; int16_t u, v; int8_t life; uint8_t max; } Particle;

typedef struct {
  const char *name;
  const char *boss;
  GColor planet;   // horizon disc colour
  GColor accent;   // theme accent
} PlanetInfo;

static const PlanetInfo PLANETS[NUM_PLANETS] = {
  { "MERCURY", "The Solar Sentinel", {.argb=GColorOrangeARGB8},        {.argb=GColorRedARGB8} },
  { "VENUS",   "The Acid Queen",     {.argb=GColorYellowARGB8},        {.argb=GColorSpringBudARGB8} },
  { "MARS",    "War Commander",      {.argb=GColorRedARGB8},           {.argb=GColorDarkCandyAppleRedARGB8} },
  { "JUPITER", "Gravity Colossus",   {.argb=GColorOrangeARGB8},        {.argb=GColorWindsorTanARGB8} },
  { "SATURN",  "Ring Master",        {.argb=GColorYellowARGB8},        {.argb=GColorChromeYellowARGB8} },
  { "URANUS",  "Ice Core",           {.argb=GColorCyanARGB8},          {.argb=GColorCelesteARGB8} },
  { "NEPTUNE", "Storm Leviathan",    {.argb=GColorCobaltBlueARGB8},    {.argb=GColorVividCeruleanARGB8} },
  { "PLUTO",   "Boss Rush",          {.argb=GColorPurpleARGB8},        {.argb=GColorMagentaARGB8} },
  { "REBBLE",  "Unknown Signal",     {.argb=GColorIslamicGreenARGB8},  {.argb=GColorGreenARGB8} },
  { "MOON",    "Lunar Defense",      {.argb=GColorLightGrayARGB8},     {.argb=GColorWhiteARGB8} },
  { "EARTH",   "The Human Armada",   {.argb=GColorVividCeruleanARGB8}, {.argb=GColorGreenARGB8} },
};

// Upgrade names are lore/brand — never translated (only their descriptions are).
static const char *UP_NAME[NUM_UPGRADES] = {
  "Timeline Boost", "Steel Armor", "Round Shield", "Rebble Core" };

// ---------------------------------------------------------------------------
// Localisation — device language via i18n_get_system_locale(), English
// fallback. Lore stays English (PBL-7, PEBBLONIAN EXPLORER, REBBLE, Time Core,
// boss names, upgrade names, the 8 device names). Planet names are translated.
// ---------------------------------------------------------------------------
typedef enum { L_EN, L_NL, L_FR, L_DE, L_ES, NUM_LANGS } Lang;

enum {
  S_MISSION, S_INTRO, S_START, S_HINT_FLY, S_HINT_BOMB, S_BEST,
  S_SEL_PLANET, S_LAUNCH, S_LOCKED, S_CHOOSE, S_LOST_PEBBLES, S_PORTAL,
  S_CHOOSE_UP, S_UP_HINT, S_WARNING, S_APPROACH, S_TURRETS, S_SIGNAL,
  S_OLD_FRIENDS, S_PAUSED, S_GAMEOVER, S_SCORE, S_NEW_RECORD, S_RETRY,
  S_EARTH_SAVED, S_CORE_DELIVERED, S_CONTINUE, S_TRUE_ENDING,
  S_EPI1, S_EPI2, S_EPI3,
  S_UD_TIMELINE, S_UD_ARMOR, S_UD_SHIELD, S_UD_REBBLE,
  S_SETTINGS, S_BACKLIGHT, S_WEAPON_JAM, S_RUMBLE, S_SOUND, S_RESET,
  S_RESET_CONFIRM, S_ON, S_OFF, S_SET_HINT, S_TITLE_SETTINGS,
  S_BL_TIMED, S_BL_ALWAYS,
  S_SND_SOFT, S_SND_NORMAL, S_SND_LOUD, S_SND_MUTED,
  NUM_STRS
};

static const char *TR[NUM_LANGS][NUM_STRS] = {
  [L_EN] = {
    "DELIVER THE TIME CORE TO EARTH", "Earth's last hope", "SELECT  start",
    "UP rise   DOWN fire", "SELECT bomb   BACK pause", "Best: %ld",
    "SELECT A PLANET", "SELECT  launch", "LOCKED", "UP / DOWN  choose",
    "LOST PEBBLES", "SECRET PORTAL OPENED!", "CHOOSE AN UPGRADE",
    "UP/DOWN choose   SELECT take", "WARNING", "APPROACH", "TURRETS  %d",
    "UNKNOWN SIGNAL DETECTED", "old friends still live here", "PAUSED",
    "GAME OVER", "SCORE %ld", "NEW RECORD!", "SELECT retry   BACK title",
    "EARTH SAVED", "TIME CORE DELIVERED", "SELECT  continue", "TRUE ENDING",
    "REBBLE honours PBL-7", "the Time Core is reunited", "with the 8 lost Pebbles",
    "Faster fire", "+2 max HP", "Blocks 1 hit/level", "Faster bomb charge",
    "SETTINGS", "Backlight", "Weapon Jam", "Rumble", "Sound", "Reset Save",
    "Press again to erase", "On", "Off", "UP/DOWN  SELECT toggle", "UP  settings",
    "Timeout", "Always",
    "Soft", "Normal", "Loud", "Muted",
  },
  [L_NL] = {
    "BRENG DE TIME CORE NAAR AARDE", "Aardes laatste hoop", "SELECT  start",
    "UP stijgen  DOWN vuur", "SELECT bom  BACK pauze", "Record: %ld",
    "KIES EEN PLANEET", "SELECT  start", "VERGRENDELD", "UP / DOWN  kiezen",
    "VERLOREN PEBBLES", "GEHEIM PORTAAL OPEN!", "KIES EEN UPGRADE",
    "UP/DOWN kiezen  SELECT nemen", "WAARSCHUWING", "NADEREN", "TORENS  %d",
    "ONBEKEND SIGNAAL", "oude vrienden leven hier nog", "GEPAUZEERD",
    "GAME OVER", "SCORE %ld", "NIEUW RECORD!", "SELECT opnieuw  BACK titel",
    "AARDE GERED", "TIME CORE GELEVERD", "SELECT  verder", "WARE EINDE",
    "REBBLE eert PBL-7", "de Time Core is herenigd", "met de 8 Pebbles",
    "Sneller vuren", "+2 max HP", "Blokt 1 hit/level", "Snellere bomlading",
    "INSTELLINGEN", "Verlichting", "Wapenstoring", "Trillen", "Geluid", "Wissen",
    "Nogmaals om te wissen", "Aan", "Uit", "UP/DOWN  SELECT wisselen", "UP  instellingen",
    "Time-out", "Altijd",
    "Zacht", "Normaal", "Luid", "Gedempt",
  },
  [L_FR] = {
    "LIVREZ LE TIME CORE À LA TERRE", "Dernier espoir", "SELECT  jouer",
    "UP monter  DOWN tir", "SELECT bombe  BACK pause", "Record : %ld",
    "CHOISIS UNE PLANÈTE", "SELECT  lancer", "VERROUILLÉ", "UP / DOWN  choisir",
    "PEBBLES PERDUS", "PORTAIL SECRET OUVERT!", "CHOISIS UNE AMÉLIORATION",
    "UP/DOWN choisir  SELECT prendre", "ALERTE", "APPROCHE", "TOURELLES  %d",
    "SIGNAL INCONNU", "de vieux amis vivent ici", "PAUSE",
    "PARTIE FINIE", "SCORE %ld", "NOUVEAU RECORD!", "SELECT rejouer  BACK titre",
    "TERRE SAUVÉE", "TIME CORE LIVRÉ", "SELECT  continuer", "VRAIE FIN",
    "REBBLE honore PBL-7", "le Time Core est réuni", "avec les 8 Pebbles",
    "Tir plus rapide", "+2 PV max", "Bloque 1 coup/niv", "Bombe plus vite",
    "RÉGLAGES", "Éclairage", "Enrayage", "Vibration", "Son", "Effacer",
    "Encore pour effacer", "Activé", "Désactivé", "UP/DOWN  SELECT changer", "UP  réglages",
    "Minuteur", "Toujours",
    "Doux", "Normal", "Fort", "Muet",
  },
  [L_DE] = {
    "BRING DEN TIME CORE ZUR ERDE", "Letzte Hoffnung", "SELECT  Start",
    "UP steigen  DOWN Feuer", "SELECT Bombe  BACK Pause", "Rekord: %ld",
    "WÄHLE EINEN PLANETEN", "SELECT  Start", "GESPERRT", "UP / DOWN  wählen",
    "VERLORENE PEBBLES", "GEHEIMES PORTAL OFFEN!", "WÄHLE EIN UPGRADE",
    "UP/DOWN wählen  SELECT nehmen", "WARNUNG", "ANFLUG", "TÜRME  %d",
    "UNBEKANNTES SIGNAL", "alte Freunde leben hier", "PAUSE",
    "GAME OVER", "PUNKTE %ld", "NEUER REKORD!", "SELECT nochmal  BACK Titel",
    "ERDE GERETTET", "TIME CORE GELIEFERT", "SELECT  weiter", "WAHRES ENDE",
    "REBBLE ehrt PBL-7", "der Time Core ist vereint", "mit den 8 Pebbles",
    "Schneller feuern", "+2 max LP", "Blockt 1 Treffer", "Bombe lädt schneller",
    "EINSTELLUNGEN", "Beleuchtung", "Ladehemmung", "Vibration", "Ton", "Löschen",
    "Nochmal zum Löschen", "An", "Aus", "UP/DOWN  SELECT ändern", "UP  Optionen",
    "Zeitlimit", "Immer",
    "Leise", "Normal", "Laut", "Stumm",
  },
  [L_ES] = {
    "LLEVA EL TIME CORE A LA TIERRA", "Última esperanza", "SELECT  jugar",
    "UP subir  DOWN fuego", "SELECT bomba  BACK pausa", "Récord: %ld",
    "ELIGE UN PLANETA", "SELECT  lanzar", "BLOQUEADO", "UP / DOWN  elegir",
    "PEBBLES PERDIDOS", "¡PORTAL SECRETO ABIERTO!", "ELIGE UNA MEJORA",
    "UP/DOWN elegir  SELECT tomar", "ALERTA", "APROXIMACIÓN", "TORRETAS  %d",
    "SEÑAL DESCONOCIDA", "viejos amigos viven aquí", "PAUSA",
    "FIN DEL JUEGO", "PUNTOS %ld", "¡NUEVO RÉCORD!", "SELECT otra vez  BACK inicio",
    "TIERRA SALVADA", "TIME CORE ENTREGADO", "SELECT  continuar", "FINAL VERDADERO",
    "REBBLE honra a PBL-7", "el Time Core se reúne", "con los 8 Pebbles",
    "Disparo rápido", "+2 PV máx", "Bloquea 1 golpe", "Bomba más rápida",
    "AJUSTES", "Luz", "Atasco", "Vibración", "Sonido", "Borrar",
    "Otra vez para borrar", "Sí", "No", "UP/DOWN  SELECT cambiar", "UP  ajustes",
    "Con tiempo", "Siempre",
    "Bajo", "Normal", "Alto", "Silencio",
  },
};

// Planet display names (translated; REBBLE stays lore). Boss names never change.
static const char *PLANET_TR[NUM_LANGS][NUM_PLANETS] = {
  [L_EN] = { "MERCURY","VENUS","MARS","JUPITER","SATURN","URANUS","NEPTUNE","PLUTO","REBBLE","MOON","EARTH" },
  [L_NL] = { "MERCURIUS","VENUS","MARS","JUPITER","SATURNUS","URANUS","NEPTUNUS","PLUTO","REBBLE","MAAN","AARDE" },
  [L_FR] = { "MERCURE","VÉNUS","MARS","JUPITER","SATURNE","URANUS","NEPTUNE","PLUTON","REBBLE","LUNE","TERRE" },
  [L_DE] = { "MERKUR","VENUS","MARS","JUPITER","SATURN","URANUS","NEPTUN","PLUTO","REBBLE","MOND","ERDE" },
  [L_ES] = { "MERCURIO","VENUS","MARTE","JÚPITER","SATURNO","URANO","NEPTUNO","PLUTÓN","REBBLE","LUNA","TIERRA" },
};

static const char **s_tr = TR[L_EN];
static const char **s_pn = PLANET_TR[L_EN];
#define S(id) s_tr[id]
#define PNAME(i) s_pn[i]

static void pick_language(void) {
  const char *loc = i18n_get_system_locale();
  Lang l = L_EN;
  if (loc) {
    if (!strncmp(loc, "nl", 2)) l = L_NL;
    else if (!strncmp(loc, "fr", 2)) l = L_FR;
    else if (!strncmp(loc, "de", 2)) l = L_DE;
    else if (!strncmp(loc, "es", 2)) l = L_ES;
  }
  s_tr = TR[l];
  s_pn = PLANET_TR[l];
}

// ---------------------------------------------------------------------------
static Window *s_window;
static Layer *s_layer;
static AppTimer *s_timer;

static GameState s_state = ST_TITLE;
static uint32_t s_tick;
static int16_t s_wls, s_hls;

// ship
static int32_t s_ship_v_fp, s_ship_vy_fp;
static bool s_rise, s_shoot;
static int16_t s_fire_cd;
static int16_t s_heat;   // weapon heat — builds while firing
static int16_t s_jam;    // ticks the gun is jammed (can't fire)

// settings (persisted)
static int8_t s_bl_mode = BL_OFF; // BacklightMode
static bool s_jam_on = true;      // false = easy mode, weapon never jams
static bool s_rumble_on = true;
static int8_t s_sound_vol = 1;    // 0=Off 1=Soft 2=Normal 3=Loud (chiptune SFX)
static int8_t s_set_sel;          // settings-menu row
static bool s_reset_armed;        // "Reset Save" needs a confirming second press
static AppTimer *s_backlight_timer;

// run scope (persists across planets within one playthrough)
static int8_t s_hp, s_hp_max, s_lives;
static int16_t s_invuln;
static int16_t s_special;
static int32_t s_score;
static bool s_go_lock;
static int16_t s_spawn_cd;
// upgrades (run scope)
static int s_fire_cd_base;
static int s_special_gain;
static int s_shield_max, s_shield;
static int s_hp_bonus;

// level / phase
static int s_planet;
static Phase s_phase;
static int s_phase_timer;
static int s_approach_left, s_approach_total;
static bool s_intro_reached;
static bool s_collect_spawned;

// boss
static int s_boss_hp, s_boss_hp_max;
static int32_t s_boss_u_fp, s_boss_v_fp, s_boss_vv_fp;
static int s_boss_over;          // 0 = normal, 1 = enraged (phase 2)
static int s_boss_fire_cd;
static int s_boss_enter;         // still flying in from the right
static int s_boss_shield;        // Mars: invulnerable while its drones live
static int s_boss_sub;           // per-boss special-attack timer
static int s_boss_state2;        // per-boss sub-state (Mars vulnerable window, ...)
// environmental gimmick state
static int s_sluggish;           // Uranus: ticks of frozen/slow controls left
static int16_t s_light_v;        // Neptune: lightning target altitude
static int s_light_warn;         // ticks of telegraph before the strike
static int s_light_strike;       // ticks the bolt is active
static int s_earth_phase;        // Earth finale: 0/1/2
static int s_pluto_pat;          // Pluto boss-rush: which past boss it mimics
static int s_pluto_pat_t;        // ticks until Pluto swaps pattern

// Boss HP per planet (0 entries filled in later milestones).
// HP: Pluto (7) is a lower-HP boss rush; Earth (10) is the 3-phase finale.
// Rebble (8) and Moon (9) are special levels with no single-HP boss.
static const int BOSS_HP[NUM_PLANETS] = { 60, 70, 80, 90, 95, 100, 120, 75, 0, 0, 180 };

#define P_PLUTO 7
#define P_REBBLE 8
#define P_MOON 9
#define P_EARTH 10

// progress (persist)
static uint16_t s_unlocked;      // bitmask of unlocked planets (non-linear route)
static uint16_t s_collected;
static int32_t s_best_score;
static bool s_new_record;
static bool s_secret_seen;       // has the true ending been reached
static bool s_boss_nodmg;        // no damage taken this boss (Neptune → Pluto portal)
static bool s_portal;            // "portal opened" flash after a no-damage Neptune

// map / upgrade selection
static int s_map_sel;
static int s_up_choices[3];
static int s_up_sel;

static Star s_stars[MAX_STARS];
static Shot s_pb[MAX_PB];
static EBullet s_eb[MAX_EB];
static Enemy s_en[MAX_EN];
static Particle s_pt[MAX_PT];

// ---------------------------------------------------------------------------
// Bitmap-font text (landscape)
// ---------------------------------------------------------------------------
static uint32_t next_cp(const char **p) {
  const uint8_t *b = (const uint8_t *)*p;
  uint32_t cp; int adv;
  if (b[0] < 0x80) { cp = b[0]; adv = 1; }
  else if ((b[0] & 0xE0) == 0xC0 && b[1]) { cp = ((b[0] & 0x1F) << 6) | (b[1] & 0x3F); adv = 2; }
  else if ((b[0] & 0xF0) == 0xE0 && b[1] && b[2]) {
    cp = ((b[0] & 0x0F) << 12) | ((b[1] & 0x3F) << 6) | (b[2] & 0x3F); adv = 3;
  } else { cp = b[0]; adv = 1; }
  *p += adv;
  return cp;
}
static int glyph_slot(uint32_t cp) {
  if (cp >= 32 && cp <= 126) return cp - 32;
  for (int i = 0; i < FONT_SPECIAL_N; i++) if (FONT_SPECIAL_CP[i] == cp) return 95 + i;
  return '?' - 32;
}
static void draw_glyph(GContext *ctx, int slot, int16_t u, int16_t v, bool big) {
  int H = big ? FONT_B_H : FONT_S_H;
  int w = big ? FONT_B_W[slot] : FONT_S_W[slot];
  for (int r = 0; r < H; r++) {
    uint32_t row = big ? FONT_B_ROWS[slot][r] : FONT_S_ROWS[slot][r];
    if (!row) continue;
    for (int c = 0; c < w; c++)
      if ((row >> c) & 1) graphics_draw_pixel(ctx, lp(u + c, v + r));
  }
}
static int16_t text_w_ls(const char *s, bool big) {
  const char *p = s; int16_t w = 0;
  while (*p) { int sl = glyph_slot(next_cp(&p)); w += (big ? FONT_B_W[sl] : FONT_S_W[sl]) + 1; }
  return w > 0 ? w - 1 : 0;
}
static int16_t draw_text_ls(GContext *ctx, const char *s, int16_t u, int16_t v, bool big, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  const char *p = s;
  while (*p) { int sl = glyph_slot(next_cp(&p)); draw_glyph(ctx, sl, u, v, big);
    u += (big ? FONT_B_W[sl] : FONT_S_W[sl]) + 1; }
  return u;
}
static void draw_text_center(GContext *ctx, const char *s, int16_t cu, int16_t v, bool big, GColor col) {
  draw_text_ls(ctx, s, cu - text_w_ls(s, big) / 2, v, big, col);
}

// Centre a heading in the big font, dropping to the small one when the big
// would run off the screen. draw_text_center does not clip -- it just starts
// drawing off-screen at both ends -- so a long translation loses characters
// from both sides. Wrapping is not an option for these: several are a single
// word ("¡ENHORABUENA!", "GEPAUZEERD") with no space to break at. The v nudge
// keeps the shrunk line optically where the big one sat.
static void draw_text_center_fit(GContext *ctx, const char *s, int16_t cu, int16_t v,
                                 GColor col) {
  bool big = text_w_ls(s, true) <= s_wls - 6;
  draw_text_center(ctx, s, cu, big ? v : v + (FONT_B_H - FONT_S_H) / 2, big, col);
}

// Centre text, wrapping onto a second line at the middle-most space if it is
// too wide for the screen (keeps long localised strings on-screen). Returns the
// v of the line below what was drawn.
static int16_t draw_text_wrap2(GContext *ctx, const char *s, int16_t cu, int16_t v,
                               int16_t lineh, GColor col) {
  if (text_w_ls(s, false) <= s_wls - 6) { draw_text_center(ctx, s, cu, v, false, col); return v + lineh; }
  int len = strlen(s), mid = len / 2, best = -1, bd = 9999;
  for (int i = 0; i < len; i++)
    if (s[i] == ' ') { int d = i - mid; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
  if (best < 0) { draw_text_center(ctx, s, cu, v, false, col); return v + lineh; }
  char a[48], b2[48];
  int na = best < (int)sizeof(a) ? best : (int)sizeof(a) - 1;
  memcpy(a, s, na); a[na] = 0;
  strncpy(b2, s + best + 1, sizeof(b2) - 1); b2[sizeof(b2) - 1] = 0;
  draw_text_center(ctx, a, cu, v, false, col);
  draw_text_center(ctx, b2, cu, v + lineh, false, col);
  return v + 2 * lineh;
}

// ---------------------------------------------------------------------------
static void game_tick(void *data);
static void spawn_boss_drone(int16_t v);
static void stop_timer(void) { if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; } }
static void start_timer(void) { stop_timer(); s_timer = app_timer_register(TICK_MS, game_tick, NULL); }
static void vibe_short(void) { if (s_rumble_on) vibes_short_pulse(); }
static void vibe_long(void)  { if (s_rumble_on) vibes_long_pulse(); }
static void vibe_double(void){ if (s_rumble_on) vibes_double_pulse(); }

// ---------------------------------------------------------------------------
// Sound effects (PT2 speaker), all gated by the Sound setting. Arcade chiptune:
// square-wave melodies, plus real explosions -- one noise burst synthesised by
// tools/gen_sfx.py (resources/sfx/boom.raw) and pitch-shifted per use, high and
// short for a fighter, low and long for a boss. Fanfares carry a bass voice via
// speaker_play_tracks(). MIDI notes: 60 = C4. Velocity 0 = the call's volume; a
// nonzero velocity per note shapes a fade.
// ---------------------------------------------------------------------------
#define SFX_VOL 80
#define SFX_N(a) (sizeof(a) / sizeof((a)[0]))
#define SQ  SpeakerWaveformSquare
#define TR_ SpeakerWaveformTriangle
#define SAW SpeakerWaveformSawtooth
#define REST(ms) {0, SQ, ms, 0, 0}

// The explosion sample; base note = the pitch it was rendered at. The waveform
// field of a sample track's notes is ignored.
#define BOOM_BASE 72
static SpeakerSample s_boom = { NULL, 0, SpeakerPcmFormat_16kHz_8bit, BOOM_BASE, false };

// Shot: a quiet two-step "pew" -- quiet because it fires many times a second.
static const SpeakerNote SHOT[] = {{86, SQ, 14, 55, 0}, {79, SQ, 18, 35, 0}};
// Enemies going up: a fighter or drone pops, a mine goes off deeper.
static const SpeakerNote POP_SMALL[] = {{86, SQ, 140, 100, 0}};
static const SpeakerNote POP_MINE[]  = {{76, SQ, 220, 115, 0}};
// Hit: a low crack under a falling tone.
static const SpeakerNote HURT_BOOM[] = {{66, SQ, 220, 110, 0}};
static const SpeakerNote HURT_TONE[] = {{57, SQ, 60, 100, 0}, {50, SQ, 100, 80, 0}};
// Overheat: the gun fizzles out.
static const SpeakerNote JAM_TONE[] = {{60, SAW, 50, 100, 0}, {57, SAW, 50, 90, 0}, {53, SAW, 80, 70, 0}};
static const SpeakerNote JAM_HISS[] = {{98, SQ, 150, 60, 0}};
// Collectible: the rising jingle, with a triangle an octave below for weight.
static const SpeakerNote COLLECT_A[] = {{81, SQ, 45, 0, 0}, {86, SQ, 45, 0, 0}, {93, SQ, 120, 0, 0}};
static const SpeakerNote COLLECT_B[] = {{69, TR_, 45, 0, 0}, {74, TR_, 45, 0, 0}, {81, TR_, 120, 0, 0}};
// Smart bomb: the biggest blast in the game, with a falling sweep through it.
static const SpeakerNote BOMB_BOOM[]  = {{58, SQ, 700, 127, 0}};
static const SpeakerNote BOMB_SWEEP[] = {{84, SQ, 40, 90, 0}, {79, SQ, 40, 85, 0}, {74, SQ, 40, 80, 0},
                                         {69, SQ, 50, 70, 0}, {64, SQ, 60, 60, 0}, {57, SQ, 90, 45, 0}};
// A boss on its way: the WARNING siren.
static const SpeakerNote SIREN[] = {{76, SAW, 170, 90, 0}, {71, SAW, 170, 90, 0},
                                    {76, SAW, 170, 90, 0}, {71, SAW, 170, 90, 0},
                                    {76, SAW, 170, 90, 0}, {71, SAW, 250, 70, 0}};
// A boss down: a big blast, then the fanfare over a bass line.
static const SpeakerNote BIG_BOOM[]  = {{55, SQ, 650, 127, 0}};
static const SpeakerNote FANFARE_A[] = {REST(420), {72, SQ, 70, 0, 0}, {76, SQ, 70, 0, 0},
                                        {79, SQ, 70, 0, 0}, {84, SQ, 240, 0, 0}};
static const SpeakerNote FANFARE_B[] = {REST(420), {60, TR_, 210, 0, 0}, {67, TR_, 240, 0, 0}};
// Losing a ship: a blast and a falling tone (game over has its own).
static const SpeakerNote SHIP_BOOM[] = {{62, SQ, 420, 120, 0}};
static const SpeakerNote SHIP_TONE[] = {{67, SQ, 90, 90, 0}, {60, SQ, 90, 75, 0}, {55, SQ, 160, 60, 0}};
// Launching into a planet: a rising whoosh of noise and tone.
static const SpeakerNote WHOOSH[]      = {{70, SQ, 70, 50, 0}, {77, SQ, 70, 65, 0}, {84, SQ, 90, 55, 0}};
static const SpeakerNote WHOOSH_TONE[] = {{60, SQ, 50, 50, 0}, {64, SQ, 50, 55, 0}, {67, SQ, 50, 60, 0},
                                          {72, SQ, 80, 55, 0}};
// Game over: a slow minor fall over a bass.
static const SpeakerNote OVER_A[] = {{67, SQ, 200, 0, 0}, {63, SQ, 200, 0, 0}, {60, SQ, 200, 0, 0},
                                     {55, SQ, 480, 0, 0}};
static const SpeakerNote OVER_B[] = {{60, TR_, 400, 0, 0}, {56, TR_, 200, 0, 0},
                                     {55, TR_, 480, 0, 0}};
// Earth saved: the full fanfare, melody over bass.
static const SpeakerNote WIN_A[] = {{72, SQ, 90, 0, 0}, {72, SQ, 90, 0, 0}, {72, SQ, 90, 0, 0},
                                    {77, SQ, 360, 0, 0}, {81, SQ, 180, 0, 0}, {84, SQ, 520, 0, 0}};
static const SpeakerNote WIN_B[] = {{60, TR_, 270, 0, 0}, {65, TR_, 360, 0, 0}, {69, TR_, 180, 0, 0},
                                    {72, TR_, 520, 0, 0}};

#define TRACK(n)  { n, SFX_N(n), NULL }
#define BOOM(n)   { n, SFX_N(n), &s_boom }

// Master volume per Sound setting (Off/Soft/Normal/Loud). Each SFX keeps its own
// tuned base loudness; sv() scales it by the chosen level, so Loud == the
// original tuning and lower levels attenuate everything proportionally.
static const uint8_t SND_VOL[4] = {0, 35, 70, 100};
static uint8_t sv(uint8_t base) { return (uint16_t)base * SND_VOL[s_sound_vol & 3] / 100; }

// Quiet Time is the watch saying "not now" — a game blipping through it is
// exactly what the user turned it on to stop. The setting stays where it was;
// this only silences the speaker while the mode is active.
// Quiet Time and the watch's own mute (Settings → Sounds & Haptics) both win
// over the in-game Sound level.
static bool sfx_enabled(void) { return s_sound_vol && !quiet_time_is_active() && !speaker_is_muted(); }

static void sfx_play(const SpeakerNote *n, uint32_t c) { if (sfx_enabled()) speaker_play_notes(n, c, sv(SFX_VOL)); }

// Plays the tracks; one whose sample failed to load is left out rather than
// played from a NULL buffer.
static void sfx_tracks(const SpeakerTrack *t, uint32_t c) {
  if (!sfx_enabled()) return;
  SpeakerTrack ok[SPEAKER_MAX_TRACKS];
  uint32_t n = 0;
  for (uint32_t i = 0; i < c && n < SPEAKER_MAX_TRACKS; i++) {
    if (t[i].sample && !t[i].sample->data) continue;
    ok[n++] = t[i];
  }
  if (n) speaker_play_tracks(ok, n, sv(SFX_VOL));
}
#define PLAY(...) do { static const SpeakerTrack t_[] = { __VA_ARGS__ }; sfx_tracks(t_, SFX_N(t_)); } while (0)

// Loaded once and never freed: a sound can still be playing from the buffer when
// the app closes, and the system reclaims the app's heap after it stops it.
static void sfx_load(void) {
  ResHandle h = resource_get_handle(RESOURCE_ID_SFX_BOOM);
  size_t size = resource_size(h);
  uint8_t *buf = malloc(size);
  if (!buf) return;
  s_boom.num_bytes = resource_load(h, buf, size);
  s_boom.data = buf;
}

static void sfx_shoot(void)       { sfx_play(SHOT, SFX_N(SHOT)); }
static void sfx_pop(bool mine)    { if (mine) PLAY(BOOM(POP_MINE)); else PLAY(BOOM(POP_SMALL)); }
static void sfx_hurt(void)        { PLAY(BOOM(HURT_BOOM), TRACK(HURT_TONE)); }
static void sfx_jam(void)         { PLAY(TRACK(JAM_TONE), BOOM(JAM_HISS)); }
static void sfx_collect(void)     { PLAY(TRACK(COLLECT_A), TRACK(COLLECT_B)); }
static void sfx_bomb(void)        { PLAY(BOOM(BOMB_BOOM), TRACK(BOMB_SWEEP)); }
static void sfx_siren(void)       { sfx_play(SIREN, SFX_N(SIREN)); }
static void sfx_boss_down(void)   { PLAY(BOOM(BIG_BOOM), TRACK(FANFARE_A), TRACK(FANFARE_B)); }
static void sfx_cleared(void)     { PLAY(TRACK(FANFARE_A), TRACK(FANFARE_B)); }
static void sfx_ship_lost(void)   { PLAY(BOOM(SHIP_BOOM), TRACK(SHIP_TONE)); }
static void sfx_launch(void)      { PLAY(BOOM(WHOOSH), TRACK(WHOOSH_TONE)); }
static void sfx_over(void)        { PLAY(TRACK(OVER_A), TRACK(OVER_B)); }
static void sfx_win(void)         { PLAY(TRACK(WIN_A), TRACK(WIN_B)); }

// Backlight "always on": forces the light while the player presses buttons,
// reverting to the watch's automatic behaviour after an idle timeout so it
// can't quietly drain the battery. (Same approach as PebbleDiver.)
static void backlight_timeout(void *data) { s_backlight_timer = NULL; light_enable(false); }
// Push the auto-off further out (or start it). No-op unless in TIMED mode.
static void backlight_touch(void) {
  if (s_bl_mode != BL_TIMED) return;
  light_enable(true);
  if (s_backlight_timer) app_timer_reschedule(s_backlight_timer, BL_TIMEOUT_MS);
  else s_backlight_timer = app_timer_register(BL_TIMEOUT_MS, backlight_timeout, NULL);
}
static void backlight_disable(void) {
  if (s_backlight_timer) { app_timer_cancel(s_backlight_timer); s_backlight_timer = NULL; }
  light_enable(false);
}
// ALWAYS forces the light on; TIMED lights while interacting then auto-dims;
// OFF hands back to the watch's normal auto-dim.
static void apply_backlight(void) {
  if (s_backlight_timer) { app_timer_cancel(s_backlight_timer); s_backlight_timer = NULL; }
  switch (s_bl_mode) {
    case BL_ALWAYS: light_enable(true); break;
    case BL_TIMED:  backlight_touch(); break;
    default:        light_enable(false); break;
  }
}

static void save_progress(void) {
  persist_write_int(PK_UNLOCKED, s_unlocked);
  persist_write_int(PK_COLLECT, s_collected);
  persist_write_int(PK_BESTSCORE, s_best_score);
  persist_write_bool(PK_SECRET, s_secret_seen);
  persist_write_int(PK_BL_MODE, s_bl_mode);
  persist_write_bool(PK_JAM, s_jam_on);
  persist_write_bool(PK_RUMBLE, s_rumble_on);
  persist_write_int(PK_SOUND_VOL, s_sound_vol);
}
static void load_progress(void) {
  s_unlocked = persist_exists(PK_UNLOCKED) ? persist_read_int(PK_UNLOCKED) : 0;
  s_unlocked |= 1;   // Mercury always available
  s_collected = persist_exists(PK_COLLECT) ? persist_read_int(PK_COLLECT) : 0;
  s_best_score = persist_exists(PK_BESTSCORE) ? persist_read_int(PK_BESTSCORE) : 0;
  s_secret_seen = persist_exists(PK_SECRET) ? persist_read_bool(PK_SECRET) : false;
  // Migrate the old on/off backlight bool to the 3-state mode on first run.
  if (persist_exists(PK_BL_MODE)) s_bl_mode = persist_read_int(PK_BL_MODE);
  else s_bl_mode = (persist_exists(PK_BACKLIGHT) && persist_read_bool(PK_BACKLIGHT)) ? BL_TIMED : BL_OFF;
  if (s_bl_mode < 0 || s_bl_mode >= NUM_BL_MODES) s_bl_mode = BL_OFF;
  s_jam_on = persist_exists(PK_JAM) ? persist_read_bool(PK_JAM) : true;
  s_rumble_on = persist_exists(PK_RUMBLE) ? persist_read_bool(PK_RUMBLE) : true;
  // Migrate the old on/off sound bool to the 4-level volume. It only ever said
  // "on", never "loud", so it lands on the same Soft the fresh install gets.
  if (persist_exists(PK_SOUND_VOL)) s_sound_vol = persist_read_int(PK_SOUND_VOL);
  else if (persist_exists(PK_SOUND)) s_sound_vol = persist_read_bool(PK_SOUND) ? 1 : 0;
  else s_sound_vol = 1;
  if (s_sound_vol < 0 || s_sound_vol > 3) s_sound_vol = 1;
}

// Rebble is a full surprise — kept off the map until discovered. Pluto stays
// visible as a locked teaser (a hint that something lies beyond Neptune).
static bool map_hidden(int i) {
  if (i == P_REBBLE) return !((s_unlocked >> i) & 1);
  return false;
}

// Mainline route: Mercury..Uranus advance one step; Neptune jumps to the Moon
// base; Moon leads to Earth. The secret chain (Pluto→Rebble→Moon) is unlocked
// separately. Returns -1 when there is no next planet (after Earth).
static int next_mainline(int p) {
  if (p <= 5) return p + 1;
  if (p == 6) return P_MOON;      // Neptune → Moon base
  if (p == P_PLUTO) return P_REBBLE;
  if (p == P_REBBLE) return P_MOON;
  if (p == P_MOON) return P_EARTH;
  return -1;                      // Earth → victory
}

// ---------------------------------------------------------------------------
static void init_stars(void) {
  for (int i = 0; i < MAX_STARS; i++) {
    s_stars[i].u_fp = (rand() % (s_wls > 0 ? s_wls : 228)) << FP;
    s_stars[i].v = HUD_V + rand() % ((s_hls > HUD_V ? s_hls : 200) - HUD_V);
    s_stars[i].layer = rand() % 2;
  }
}
static void clear_field(void) {
  for (int i = 0; i < MAX_PB; i++) s_pb[i].on = false;
  for (int i = 0; i < MAX_EB; i++) s_eb[i].on = false;
  for (int i = 0; i < MAX_EN; i++) s_en[i].kind = EN_NONE;
  for (int i = 0; i < MAX_PT; i++) s_pt[i].on = false;
}

static void new_game(void) {
  s_lives = LIVES_START;
  s_score = 0;
  s_special = 0;
  s_fire_cd_base = FIRE_CD_BASE;
  s_special_gain = 14;
  s_shield_max = 0; s_shield = 0;
  s_hp_bonus = 0;
  s_new_record = false;
  s_state = ST_MAP;
  s_map_sel = 0;
  stop_timer();
  layer_mark_dirty(s_layer);
}

static void begin_phase_playing(void) {
  s_ship_v_fp = (s_hls / 2) << FP;
  s_ship_vy_fp = 0;
  s_rise = false; s_shoot = false;
  s_fire_cd = 0;
  s_heat = 0; s_jam = 0;
  s_invuln = 50;
  s_hp = s_hp_max;
  s_shield = s_shield_max;
  clear_field();
  init_stars();
}

static void start_planet(int idx) {
  GRect b = layer_get_bounds(s_layer);
  s_wls = b.size.h; s_hls = b.size.w; s_native_h = b.size.h;
  if (idx == P_PLUTO) s_portal = false;   // the reveal has served its purpose
  s_planet = idx;
  s_hp_max = HP_MAX_BASE + s_hp_bonus;
  s_phase = PH_APPROACH;
  s_approach_total = approach_ticks_for(idx);
  s_approach_left = s_approach_total;
  s_spawn_cd = 24;
  s_intro_reached = false;
  s_collect_spawned = false;
  begin_phase_playing();
  s_state = ST_PLAYING;
  sfx_launch();
  start_timer();
  layer_mark_dirty(s_layer);
}

static void init_boss(void) {
  s_boss_hp_max = BOSS_HP[s_planet] > 0 ? BOSS_HP[s_planet] : 60;
  s_boss_hp = s_boss_hp_max;
  s_boss_u_fp = (s_wls + 30) << FP;
  s_boss_v_fp = (s_hls / 2) << FP;
  s_boss_vv_fp = 1;
  s_boss_over = 0;
  s_boss_fire_cd = 45;
  s_boss_enter = 1;
  s_boss_shield = (s_planet == 2) ? 1 : 0;   // Mars starts behind its drone shield
  s_boss_sub = 70;
  s_boss_state2 = 0;
  s_sluggish = 0;
  s_light_v = 0; s_light_warn = 0; s_light_strike = 0;
  s_boss_nodmg = true;
  s_earth_phase = 0;
  s_pluto_pat = 0; s_pluto_pat_t = 90;
  if (s_planet == 2) {                        // Mars: initial drone shield wave
    spawn_boss_drone(HUD_V + 24);
    spawn_boss_drone(s_hls / 2);
    spawn_boss_drone(s_hls - 24);
  }
}

static void go_unlock(void *data) { s_go_lock = false; layer_mark_dirty(s_layer); }

static void game_over(void) {
  stop_timer();
  s_rise = false; s_shoot = false;
  if (s_score > s_best_score) { s_best_score = s_score; s_new_record = true; }
  save_progress();
  s_state = ST_GAMEOVER;
  vibe_long();
  sfx_over();
  s_go_lock = true;
  app_timer_register(1000, go_unlock, NULL);
  layer_mark_dirty(s_layer);
}

static void open_upgrade(void) {
  int pool[NUM_UPGRADES]; for (int i = 0; i < NUM_UPGRADES; i++) pool[i] = i;
  for (int i = NUM_UPGRADES - 1; i > 0; i--) { int j = rand() % (i + 1); int t = pool[i]; pool[i] = pool[j]; pool[j] = t; }
  for (int i = 0; i < 3; i++) s_up_choices[i] = pool[i];
  s_up_sel = 0;
  s_state = ST_UPGRADE;
  layer_mark_dirty(s_layer);
}

static void clear_level(void) {
  stop_timer();
  s_rise = false; s_shoot = false;

  int nx = next_mainline(s_planet);
  if (nx >= 0) s_unlocked |= (1 << nx);
  if (s_planet == 6 && s_boss_nodmg) { s_unlocked |= (1 << P_PLUTO); s_portal = true; }
  if (s_planet == P_PLUTO) s_unlocked |= (1 << P_REBBLE);
  if (s_score > s_best_score) { s_best_score = s_score; s_new_record = true; }
  save_progress();

  if (s_planet == P_EARTH) {                       // finale → victory / true ending
    bool all8 = (s_collected & 0xFF) == 0xFF;
    s_state = all8 ? ST_SECRET_ENDING : ST_VICTORY;
    if (all8) { s_secret_seen = true; save_progress(); }
    vibe_double();
    sfx_win();
    s_go_lock = true;
    app_timer_register(1200, go_unlock, NULL);
    layer_mark_dirty(s_layer);
    return;
  }
  if (s_planet == P_REBBLE || s_planet == P_MOON) { // story / mini-level: no upgrade
    s_map_sel = (nx >= 0) ? nx : s_planet;
    s_state = ST_MAP;
    layer_mark_dirty(s_layer);
    return;
  }
  open_upgrade();                                  // ordinary boss → pick a card
}

static void apply_upgrade(int type) {
  switch (type) {
    case UP_TIMELINE: if (s_fire_cd_base > 3) s_fire_cd_base--; break;
    case UP_ARMOR:    s_hp_bonus += 2; s_hp_max = HP_MAX_BASE + s_hp_bonus; s_hp = s_hp_max; break;
    case UP_SHIELD:   s_shield_max += 1; break;
    case UP_REBBLE:   s_special_gain += 6; break;
  }
}

static void spawn_particle(int16_t u, int16_t v, uint8_t size) {
  for (int i = 0; i < MAX_PT; i++)
    if (!s_pt[i].on) { s_pt[i].on = true; s_pt[i].u = u; s_pt[i].v = v; s_pt[i].life = size; s_pt[i].max = size; return; }
}

static void lose_life(void) {
  s_lives--;
  spawn_particle(SHIP_U, s_ship_v_fp >> FP, 12);
  if (s_lives < 0) { game_over(); return; }
  vibe_long();
  sfx_ship_lost();
  // checkpoint: restart current phase
  if (s_phase == PH_BOSS || (s_phase == PH_BOSS_INTRO && s_intro_reached)) {
    begin_phase_playing();
    s_phase = PH_BOSS;
    init_boss();
  } else {
    s_approach_left = s_approach_total;
    s_collect_spawned = (s_collected >> s_planet) & 1;  // don't respawn if already owned
    begin_phase_playing();
    s_phase = PH_APPROACH;
  }
}

// Colour backlight (Time 2; a no-op elsewhere): a hit flashes the light red,
// a hit the shield soaks up flashes it blue, then the user's own colour returns.
#define FLASH_MS        300
#define FLASH_HIT       0xFF2814u
#define FLASH_SHIELD    0x3C8CFFu
static AppTimer *s_flash_timer;

static void flash_end(void *data) {
  s_flash_timer = NULL;
  light_set_system_color();
}

static void flash_light(uint32_t rgb) {
  light_set_color_rgb888(rgb);
  if (s_flash_timer) app_timer_reschedule(s_flash_timer, FLASH_MS);
  else s_flash_timer = app_timer_register(FLASH_MS, flash_end, NULL);
}

static void hurt_ship(int8_t dmg) {
  if (s_invuln > 0) return;
  s_boss_nodmg = false;    // any hit (even a blocked shield) breaks the no-damage run
  if (s_shield > 0) { s_shield--; s_invuln = 35; vibe_short(); flash_light(FLASH_SHIELD); return; }
  s_hp -= dmg;
  s_invuln = 40;
  vibe_short();
  flash_light(FLASH_HIT);
  sfx_hurt();
  if (s_hp <= 0) { s_hp = 0; lose_life(); }
}

static void kill_enemy(Enemy *e) {
  spawn_particle(e->u_fp >> FP, e->v_fp >> FP, e->kind == EN_MINE ? 10 : 7);
  s_score += (e->kind == EN_FIGHTER) ? 30 : (e->kind == EN_MINE ? 20 : 10);
  s_special += s_special_gain;
  if (s_special > SPECIAL_MAX) s_special = SPECIAL_MAX;
  sfx_pop(e->kind == EN_MINE);
  e->kind = EN_NONE;
}

static void fire_player(void) {
  for (int i = 0; i < MAX_PB; i++)
    if (!s_pb[i].on) { s_pb[i].on = true; s_pb[i].u_fp = (SHIP_U + 10) << FP; s_pb[i].v = s_ship_v_fp >> FP; sfx_shoot(); return; }
}

static void spawn_enemy(void) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_NONE) { e = &s_en[i]; break; }
  if (!e) return;
  int roll = rand() % 100;
  EnemyKind k = roll < 55 ? EN_DRONE : (roll < 85 ? EN_FIGHTER : EN_MINE);
  int16_t top = HUD_V + 10, bot = s_hls - 10;
  int16_t v = top + rand() % (bot - top);
  e->kind = k; e->u_fp = (s_wls + 12) << FP; e->v_fp = v << FP; e->base_v = v;
  e->phase = rand() % TRIG_MAX_ANGLE; e->fire_cd = 30 + rand() % 40;
  switch (k) {
    case EN_DRONE:   e->hp = 1; e->r = 6; break;
    case EN_FIGHTER: e->hp = 2; e->r = 8; break;
    default:         e->hp = 2; e->r = 8; break;
  }
}

static void spawn_collectible(void) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_NONE) { e = &s_en[i]; break; }
  if (!e) return;
  e->kind = EN_COLLECT;
  e->u_fp = (s_wls + 14) << FP;
  int16_t v = (rand() % 2) ? HUD_V + 16 : s_hls - 20;   // hidden high or low
  e->v_fp = v << FP; e->base_v = v; e->phase = 0; e->hp = 1; e->r = 7;
  s_collect_spawned = true;
}

// Aimed enemy shot toward target altitude tv. kind: 0 normal, 1 fireball.
static void spawn_ebullet(int16_t u, int16_t v, int16_t tv, uint8_t kind) {
  for (int i = 0; i < MAX_EB; i++) {
    if (!s_eb[i].on) {
      s_eb[i].on = true; s_eb[i].u_fp = u << FP; s_eb[i].v_fp = v << FP;
      int16_t dv = tv - v; if (dv > 70) dv = 70; if (dv < -70) dv = -70;
      s_eb[i].vu_fp = -((kind == 1 ? 3 : 4) << FP);
      s_eb[i].vv_fp = dv * (kind == 1 ? 3 : 4);
      s_eb[i].kind = kind; s_eb[i].ttl = 0;
      return;
    }
  }
}

// Saturn ring: a big slow disc that splits into a 3-way spread when ttl ends.
static void spawn_ring(int16_t u, int16_t v) {
  for (int i = 0; i < MAX_EB; i++) {
    if (!s_eb[i].on) {
      s_eb[i].on = true; s_eb[i].u_fp = u << FP; s_eb[i].v_fp = v << FP;
      s_eb[i].vu_fp = -(2 << FP); s_eb[i].vv_fp = 0;
      s_eb[i].kind = 2; s_eb[i].ttl = 42;
      return;
    }
  }
}

// Venus acid cloud: a lingering damage zone that drifts left.
static void spawn_acid(int16_t u, int16_t v) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_NONE) { e = &s_en[i]; break; }
  if (!e) return;
  e->kind = EN_ACID; e->u_fp = u << FP; e->v_fp = v << FP; e->base_v = v;
  e->phase = rand() % TRIG_MAX_ANGLE; e->hp = 1; e->r = 13;
  e->fire_cd = 150;   // lifetime in ticks
}

static int count_drones(void) {
  int n = 0;
  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_DRONE) n++;
  return n;
}
static void spawn_boss_drone(int16_t v) {
  Enemy *e = NULL;
  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_NONE) { e = &s_en[i]; break; }
  if (!e) return;
  e->kind = EN_DRONE; e->u_fp = (s_wls - 60) << FP; e->v_fp = v << FP;
  e->base_v = v; e->phase = 0; e->hp = 1; e->r = 6; e->fire_cd = 40;
}

static int count_turrets(void) {
  int n = 0;
  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_TURRET) n++;
  return n;
}
static void spawn_turrets(void) {
  // Moon base: a column of dug-in turrets down the right edge
  for (int k = 0; k < 5; k++) {
    Enemy *e = NULL;
    for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind == EN_NONE) { e = &s_en[i]; break; }
    if (!e) break;
    int16_t v = HUD_V + 18 + k * ((s_hls - HUD_V - 30) / 4);
    e->kind = EN_TURRET; e->u_fp = (s_wls - 16) << FP; e->v_fp = v << FP;
    e->base_v = v; e->phase = 0; e->hp = 4; e->r = 9; e->fire_cd = 40 + k * 12;
  }
}

static void smart_bomb(void) {
  if (s_special < SPECIAL_MAX) return;
  s_special = 0;
  vibe_double();
  sfx_bomb();
  for (int i = 0; i < MAX_EN; i++)
    if (s_en[i].kind != EN_NONE && s_en[i].kind != EN_COLLECT) {
      spawn_particle(s_en[i].u_fp >> FP, s_en[i].v_fp >> FP, 8);
      s_score += 5; s_en[i].kind = EN_NONE;
    }
  for (int i = 0; i < MAX_EB; i++) s_eb[i].on = false;
  if (s_phase == PH_BOSS && !s_boss_shield) {
    s_boss_hp -= 12;
    spawn_particle(s_boss_u_fp >> FP, s_boss_v_fp >> FP, 12);
    if (s_boss_hp * 2 <= s_boss_hp_max && !s_boss_over) s_boss_over = 1;
  }
}

// ---------------------------------------------------------------------------
static void ship_physics(void) {
  // Uranus freeze makes the controls sluggish (thrust weaker) for a while.
  int32_t up = (s_sluggish > 0) ? 18 : 34;
  int32_t dn = (s_sluggish > 0) ? 13 : 24;
  if (s_rise) { s_ship_vy_fp -= up; if (s_ship_vy_fp > 0) s_ship_vy_fp -= up / 2; }
  else { s_ship_vy_fp += dn; if (s_ship_vy_fp < 0) s_ship_vy_fp += dn / 2; }

  bool boss = (s_phase == PH_BOSS && !s_boss_enter);
  // Jupiter: gravity well pulls the ship toward the boss — constant counter-steer.
  if (boss && s_planet == 3) {
    int16_t bv = s_boss_v_fp >> FP, sv = s_ship_v_fp >> FP;
    s_ship_vy_fp += (bv > sv) ? 20 : -20;
  }
  // Neptune: storm swells push the ship up and down.
  if (boss && s_planet == 6)
    s_ship_vy_fp += sin_lookup((s_tick * 1400) % TRIG_MAX_ANGLE) * 26 / TRIG_MAX_RATIO;

  int32_t vmax = 340;
  if (s_ship_vy_fp < -vmax) s_ship_vy_fp = -vmax;
  if (s_ship_vy_fp > vmax) s_ship_vy_fp = vmax;
  s_ship_v_fp += s_ship_vy_fp;
  int32_t vmin = (HUD_V + 8) << FP, vlim = (s_hls - 10) << FP;
  if (s_ship_v_fp < vmin) { s_ship_v_fp = vmin; s_ship_vy_fp = 0; }
  if (s_ship_v_fp > vlim) { s_ship_v_fp = vlim; s_ship_vy_fp = 0; }
  if (s_sluggish > 0) s_sluggish--;
}

static void boss_dead(int16_t bu, int16_t bv) {
  for (int k = 0; k < 8; k++) spawn_particle(bu - 12 + rand() % 24, bv - 12 + rand() % 24, 11);
  s_score += 300 + s_planet * 60;
  s_phase = PH_CLEARED;
  s_phase_timer = CLEARED_TICKS;
  vibe_double();
  sfx_boss_down();
}

static void boss_hit(int16_t bu, int16_t bv) {
  if (s_boss_shield) { spawn_particle(bu - 8, bv, 3); return; }   // drones must fall first
  s_boss_hp--;
  if (s_boss_hp * 2 <= s_boss_hp_max && !s_boss_over) s_boss_over = 1;
  if (s_boss_hp <= 0) boss_dead(bu, bv);
}

static void boss_tick(int16_t sv) {
  int p = s_planet;
  int32_t target = (s_wls - 42) << FP;
  if (s_boss_enter) {
    s_boss_u_fp -= (3 << FP);
    if (s_boss_u_fp <= target) { s_boss_u_fp = target; s_boss_enter = 0; }
  }

  // --- vertical movement (per boss speed) ---
  int32_t spd;
  switch (p) {
    case 0: spd = s_boss_over ? 520 : 340; break;   // Mercury: fast
    case 1: spd = 150; break;                        // Venus: slow
    case 2: spd = 190; break;                        // Mars: steady
    case 3: spd = 120; break;                        // Jupiter: ponderous
    case 4: spd = 250; break;                        // Saturn
    case 5: spd = 230; break;                        // Uranus
    case 6: spd = 300; break;                        // Neptune
    case P_PLUTO: spd = 400; break;                  // Pluto: erratic
    case P_EARTH: spd = 130; break;                  // Earth: ponderous armada
    default: spd = 300; break;
  }
  if (s_boss_over && p != 0) spd += 110;
  if (p == P_PLUTO && (rand() % 45 == 0)) s_boss_vv_fp = -s_boss_vv_fp;   // unpredictable
  s_boss_v_fp += (s_boss_vv_fp > 0 ? spd : -spd);
  int32_t vtop = (HUD_V + 22) << FP, vbot = (s_hls - 22) << FP;
  if (s_boss_v_fp < vtop) { s_boss_v_fp = vtop; s_boss_vv_fp = 1; }
  if (s_boss_v_fp > vbot) { s_boss_v_fp = vbot; s_boss_vv_fp = -1; }

  int16_t bu = s_boss_u_fp >> FP, bv = s_boss_v_fp >> FP, br = 18;

  // Pluto mimics a rotating past boss; Earth's phase drives its attack set.
  if (p == P_PLUTO && --s_pluto_pat_t <= 0) { s_pluto_pat = rand() % 7; s_pluto_pat_t = 80 + rand() % 50; }
  if (p == P_EARTH) {
    int ph = (s_boss_hp * 3 <= s_boss_hp_max) ? 2 : (s_boss_hp * 3 <= s_boss_hp_max * 2 ? 1 : 0);
    if (ph != s_earth_phase) {
      s_earth_phase = ph;
      if (ph >= 1) s_boss_over = 1;
      for (int k = 0; k < 6; k++) spawn_particle(bu - 10 + rand() % 20, bv - 10 + rand() % 20, 9);
      vibe_double();
    }
  }
  int atk = p;
  if (p == P_PLUTO) atk = s_pluto_pat;
  else if (p == P_EARTH) atk = (s_earth_phase == 0) ? 0 : (s_earth_phase == 1 ? 4 : (int)((s_tick / 50) % 7));

  if (!s_boss_enter) {
    // --- ordinary aimed fire ---
    if (--s_boss_fire_cd <= 0) {
      switch (atk) {
        case 0:
          spawn_ebullet(bu - 16, bv, sv, 1);
          if (s_boss_over) { spawn_ebullet(bu - 16, bv, sv - 40, 1); spawn_ebullet(bu - 16, bv, sv + 40, 1); }
          s_boss_fire_cd = s_boss_over ? 34 : 52;
          break;
        case 1: spawn_ebullet(bu - 14, bv, sv, 0); s_boss_fire_cd = 60; break;
        case 2: if (!s_boss_shield) spawn_ebullet(bu - 14, bv, sv, 0); s_boss_fire_cd = 46; break;
        case 3: spawn_ebullet(bu - 14, bv, sv, 1); s_boss_fire_cd = 58; break;
        case 4: spawn_ebullet(bu - 14, bv, sv, 0); s_boss_fire_cd = 66; break;
        case 5: spawn_ebullet(bu - 14, bv, sv, 0); s_boss_fire_cd = 50; break;
        default: spawn_ebullet(bu - 14, bv, sv, 0); s_boss_fire_cd = 54; break;
      }
      if (s_boss_over && atk >= 1 && atk != 2) s_boss_fire_cd -= 12;
    }

    // --- special gimmick attack ---
    if (--s_boss_sub <= 0) {
      switch (atk) {
        case 1:   // Venus: drop an acid cloud (area denial)
          spawn_acid(bu - 14, bv);
          s_boss_sub = s_boss_over ? 55 : 85;
          break;
        case 2: { // Mars: raise shield + summon a drone wave (real Mars only)
          if (p == 2 && count_drones() == 0 && s_boss_state2 <= 0) {
            s_boss_shield = 1;
            spawn_boss_drone(HUD_V + 24);
            spawn_boss_drone(s_hls / 2);
            spawn_boss_drone(s_hls - 24);
          }
          s_boss_sub = 40;
          break;
        }
        case 4:   // Saturn: hurl a splitting ring
          spawn_ring(bu - 16, bv);
          s_boss_sub = s_boss_over ? 70 : 100;
          break;
        case 5:   // Uranus: freeze the controls
          s_sluggish = 90;
          spawn_ebullet(bu - 14, bv, sv - 30, 0);
          spawn_ebullet(bu - 14, bv, sv + 30, 0);
          s_boss_sub = 190;
          break;
        case 6:   // Neptune: telegraph a lightning strike at an altitude
          s_light_v = HUD_V + 20 + rand() % (s_hls - HUD_V - 40);
          s_light_warn = 26;
          s_boss_sub = 150;
          break;
        default:
          s_boss_sub = 200;
          break;
      }
    }

    // Mars shield bookkeeping: when all drones die, expose the boss briefly
    if (p == 2) {
      if (s_boss_shield && count_drones() == 0) { s_boss_shield = 0; s_boss_state2 = 160; }
      if (!s_boss_shield && s_boss_state2 > 0) s_boss_state2--;
    }

    // Neptune lightning resolution
    if (s_light_warn > 0) {
      if (--s_light_warn == 0) {
        s_light_strike = 8;
        if (sv > s_light_v - 12 && sv < s_light_v + 12) hurt_ship(2);
      }
    }
    if (s_light_strike > 0) s_light_strike--;
  }

  // --- player bullets vs boss ---
  for (int j = 0; j < MAX_PB; j++) {
    if (!s_pb[j].on) continue;
    int16_t bx = s_pb[j].u_fp >> FP;
    int32_t dx = bx - bu, dy = s_pb[j].v - bv;
    if (dx * dx + dy * dy < (br + 3) * (br + 3)) {
      s_pb[j].on = false;
      boss_hit(bu, bv);
      if (s_phase != PH_BOSS) return;
    }
  }
  // boss body contact
  int32_t cdx = bu - SHIP_U, cdy = bv - sv;
  if (cdx * cdx + cdy * cdy < (br + SHIP_R) * (br + SHIP_R)) hurt_ship(2);
}

static void game_tick(void *data) {
  s_timer = app_timer_register(TICK_MS, game_tick, NULL);
  s_tick++;
  GRect b = layer_get_bounds(s_layer);
  s_wls = b.size.h; s_hls = b.size.w; s_native_h = b.size.h;

  ship_physics();
  int16_t sv = s_ship_v_fp >> FP;

  // firing + weapon heat/jam
  if (s_fire_cd > 0) s_fire_cd--;
  if (s_jam > 0) {
    s_jam--;
  } else if (s_shoot && s_fire_cd == 0) {
    if (s_jam_on && s_heat >= HEAT_MAX) {
      s_jam = JAM_TICKS; s_heat = 0; vibe_short(); sfx_jam();   // overheated from holding — cool it
    } else {
      fire_player();
      s_fire_cd = s_fire_cd_base;
      if (s_jam_on) { s_heat += HEAT_PER_SHOT; if (s_heat > HEAT_MAX) s_heat = HEAT_MAX; }
    }
  }
  // cool down whenever not actively firing this tick
  if (s_jam > 0 || !s_shoot) { s_heat -= HEAT_COOL; if (s_heat < 0) s_heat = 0; }

  for (int i = 0; i < MAX_STARS; i++) {
    s_stars[i].u_fp -= s_stars[i].layer ? (3 << FP) : ((1 << FP) + 128);
    if (s_stars[i].u_fp < 0) { s_stars[i].u_fp = (s_wls + rand() % 20) << FP; s_stars[i].v = HUD_V + rand() % (s_hls - HUD_V); }
  }

  for (int i = 0; i < MAX_PB; i++) {
    if (!s_pb[i].on) continue;
    s_pb[i].u_fp += (9 << FP);
    if ((s_pb[i].u_fp >> FP) > s_wls + 6) s_pb[i].on = false;
  }

  // enemies
  for (int i = 0; i < MAX_EN; i++) {
    Enemy *e = &s_en[i];
    if (e->kind == EN_NONE) continue;
    switch (e->kind) {
      case EN_DRONE:
        if (s_phase == PH_BOSS && s_planet == 2) {
          // Mars minions hover and weave — the player must hunt them down
          e->phase = (e->phase + 900) % TRIG_MAX_ANGLE;
          e->v_fp = (e->base_v << FP) + ((sin_lookup(e->phase) * 24 / TRIG_MAX_RATIO) << FP);
          int16_t want = s_wls - 70, cu = e->u_fp >> FP;
          e->u_fp += (cu > want + 1) ? -20 : (cu < want - 1 ? 20 : 0);
        } else {
          e->u_fp -= (2 << FP) + 40;
        }
        break;
      case EN_ACID:
        e->u_fp -= (1 << FP);
        e->phase = (e->phase + 400) % TRIG_MAX_ANGLE;
        if (--e->fire_cd <= 0) e->kind = EN_NONE;
        break;
      case EN_FIGHTER:
        e->u_fp -= (2 << FP);
        e->phase = (e->phase + 1100) % TRIG_MAX_ANGLE;
        e->v_fp = (e->base_v << FP) + ((sin_lookup(e->phase) * 22 / TRIG_MAX_RATIO) << FP);
        if (--e->fire_cd <= 0 && (e->u_fp >> FP) < s_wls - 10) {
          spawn_ebullet(e->u_fp >> FP, e->v_fp >> FP, sv, 0); e->fire_cd = 55 + rand() % 40;
        }
        break;
      case EN_MINE: {
        e->u_fp -= (1 << FP) + 20;
        int16_t mv = e->v_fp >> FP;
        if (sv > mv + 1) e->v_fp += 24; else if (sv < mv - 1) e->v_fp -= 24;
        break;
      }
      case EN_COLLECT: e->u_fp -= (1 << FP) + 30; break;
      case EN_TURRET:  // stationary Moon-base gun
        if (--e->fire_cd <= 0) { spawn_ebullet(e->u_fp >> FP, e->v_fp >> FP, sv, 0); e->fire_cd = 70 + rand() % 40; }
        break;
      default: break;
    }
    if (e->kind == EN_NONE) continue;
    if ((e->u_fp >> FP) < -16) { e->kind = EN_NONE; continue; }
    int16_t ex = e->u_fp >> FP, ey = e->v_fp >> FP;

    // acid clouds and collectibles are not shootable
    if (e->kind != EN_COLLECT && e->kind != EN_ACID) {
      for (int j = 0; j < MAX_PB; j++) {
        if (!s_pb[j].on) continue;
        int16_t bx = s_pb[j].u_fp >> FP;
        int32_t dx = bx - ex, dy = s_pb[j].v - ey;
        if (dx * dx + dy * dy < (e->r + 3) * (e->r + 3)) {
          s_pb[j].on = false;
          if (--e->hp <= 0) { kill_enemy(e); break; }
        }
      }
      if (e->kind == EN_NONE) continue;
    }

    int32_t cdx = ex - SHIP_U, cdy = ey - sv;
    if (cdx * cdx + cdy * cdy < (e->r + SHIP_R) * (e->r + SHIP_R)) {
      if (e->kind == EN_COLLECT) {
        s_collected |= (1 << s_planet);
        save_progress();
        for (int k = 0; k < 4; k++) spawn_particle(ex, ey, 8);
        vibe_double();
        sfx_collect();
        e->kind = EN_NONE;
      } else if (e->kind == EN_ACID) {
        hurt_ship(1);                    // lingering zone — keeps the cloud
        if (s_state != ST_PLAYING) return;
      } else {
        hurt_ship(e->kind == EN_MINE ? 2 : 1);
        spawn_particle(ex, ey, 7);
        e->kind = EN_NONE;
        if (s_state != ST_PLAYING) return;
      }
    }
  }

  // enemy bullets (kind 0 normal, 1 fireball, 2 splitting ring)
  for (int i = 0; i < MAX_EB; i++) {
    if (!s_eb[i].on) continue;
    s_eb[i].u_fp += s_eb[i].vu_fp; s_eb[i].v_fp += s_eb[i].vv_fp;
    int16_t bx = s_eb[i].u_fp >> FP, by = s_eb[i].v_fp >> FP;
    if (s_eb[i].kind == 2 && --s_eb[i].ttl <= 0) {   // ring bursts into a spread
      s_eb[i].on = false;
      spawn_ebullet(bx, by, sv, 0);
      spawn_ebullet(bx, by, sv - 35, 0);
      spawn_ebullet(bx, by, sv + 35, 0);
      continue;
    }
    if (bx < -8 || by < HUD_V || by > s_hls) { s_eb[i].on = false; continue; }
    int32_t dx = bx - SHIP_U, dy = by - sv;
    int r = s_eb[i].kind == 2 ? 9 : (s_eb[i].kind == 1 ? 4 : 3);
    if (dx * dx + dy * dy < (SHIP_R + r) * (SHIP_R + r)) {
      s_eb[i].on = false; hurt_ship(1);
      if (s_state != ST_PLAYING) return;
    }
  }

  for (int i = 0; i < MAX_PT; i++) if (s_pt[i].on && --s_pt[i].life <= 0) s_pt[i].on = false;
  if (s_invuln > 0) s_invuln--;

  // phase machine
  switch (s_phase) {
    case PH_APPROACH:
      if (s_planet != P_REBBLE && --s_spawn_cd <= 0) {   // Rebble is a calm flythrough
        spawn_enemy();
        if (rand() % 100 < 25) spawn_enemy();
        s_spawn_cd = 20 + rand() % 16;
      }
      if (s_planet < NUM_COLLECT && !s_collect_spawned && !((s_collected >> s_planet) & 1) &&
          s_approach_left < s_approach_total / 2) spawn_collectible();
      if (--s_approach_left <= 0) {
        if (s_planet == P_REBBLE) { s_phase = PH_CLEARED; s_phase_timer = 70; }   // signal → reveal → done
        else { s_phase = PH_BOSS_INTRO; s_phase_timer = INTRO_TICKS; s_intro_reached = true; sfx_siren(); }
      }
      break;
    case PH_BOSS_INTRO:
      if (--s_phase_timer <= 0) {
        s_phase = PH_BOSS;
        if (s_planet == P_MOON) spawn_turrets();
        else init_boss();
      }
      break;
    case PH_BOSS:
      if (s_planet == P_MOON) {
        if (count_turrets() == 0) { s_score += 250; s_phase = PH_CLEARED; s_phase_timer = CLEARED_TICKS; vibe_double(); sfx_cleared(); }
      } else {
        boss_tick(sv);
        if (s_state != ST_PLAYING) return;
      }
      break;
    case PH_CLEARED:
      if (--s_phase_timer <= 0) { clear_level(); return; }
      break;
  }

  layer_mark_dirty(s_layer);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
static void draw_ship(GContext *ctx, int16_t u, int16_t v) {
  if (s_state == ST_PLAYING && s_invuln > 0 && (s_tick / 3) % 2 == 0) return;
  if (s_shield > 0) {
    graphics_context_set_stroke_color(ctx, GColorCyan);
    graphics_draw_circle(ctx, lp(u, v), SHIP_R + 4);
  }
  graphics_context_set_fill_color(ctx, GColorMediumSpringGreen);
  graphics_fill_rect(ctx, ls(u - 8, v - 5, 16, 10), 3, GCornersAll);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, ls(u + 6, v - 3, 5, 6), 2, GCornersRight);
  graphics_context_set_fill_color(ctx, GColorCyan);
  graphics_fill_circle(ctx, lp(u + 2, v - 1), 3);
  if ((s_tick / 2) % 2 == 0) {
    graphics_context_set_fill_color(ctx, GColorOrange);
    graphics_fill_rect(ctx, ls(u - 13, v - 2, 5, 4), 1, GCornersLeft);
  }
}

static void draw_pebble_icon(GContext *ctx, int16_t u, int16_t v, bool filled) {
  graphics_context_set_stroke_color(ctx, filled ? GColorWhite : GColorLightGray);
  graphics_draw_rect(ctx, ls(u - 4, v - 6, 8, 12));
  if (filled) {
    graphics_context_set_fill_color(ctx, GColorMediumSpringGreen);
    graphics_fill_rect(ctx, ls(u - 3, v - 4, 6, 8), 0, GCornerNone);
  }
}

static void draw_enemy(GContext *ctx, Enemy *e) {
  int16_t u = e->u_fp >> FP, v = e->v_fp >> FP;
  switch (e->kind) {
    case EN_DRONE:
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_circle(ctx, lp(u, v), 5);
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_fill_circle(ctx, lp(u, v), 2);
      break;
    case EN_FIGHTER:
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_rect(ctx, ls(u - 7, v - 4, 13, 8), 2, GCornersAll);
      graphics_context_set_fill_color(ctx, GColorDarkCandyAppleRed);
      graphics_fill_rect(ctx, ls(u - 10, v - 2, 4, 4), 1, GCornersLeft);
      graphics_context_set_fill_color(ctx, GColorYellow);
      graphics_fill_circle(ctx, lp(u + 1, v), 2);
      break;
    case EN_MINE:
      graphics_context_set_stroke_color(ctx, GColorYellow);
      for (int a = 0; a < TRIG_MAX_ANGLE; a += TRIG_MAX_ANGLE / 8) {
        int16_t sx = sin_lookup(a) * 10 / TRIG_MAX_RATIO;
        int16_t sy = -cos_lookup(a) * 10 / TRIG_MAX_RATIO;
        graphics_draw_line(ctx, lp(u, v), lp(u + sx, v + sy));
      }
      graphics_context_set_fill_color(ctx, GColorChromeYellow);
      graphics_fill_circle(ctx, lp(u, v), 5);
      graphics_context_set_fill_color(ctx, GColorBlack);
      graphics_fill_circle(ctx, lp(u, v), 2);
      break;
    case EN_TURRET:
      graphics_context_set_fill_color(ctx, GColorLightGray);
      graphics_fill_rect(ctx, ls(u - 8, v - 8, 16, 16), 2, GCornersAll);
      graphics_context_set_fill_color(ctx, GColorDarkGray);
      graphics_fill_rect(ctx, ls(u - 12, v - 2, 6, 4), 0, GCornerNone);   // barrel (points left)
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_circle(ctx, lp(u, v), 3);
      break;
    case EN_COLLECT: {
      GColor g = ((s_tick / 4) % 2) ? GColorWhite : GColorCyan;
      graphics_context_set_stroke_color(ctx, g);
      graphics_draw_circle(ctx, lp(u, v), 8);
      draw_pebble_icon(ctx, u, v, true);
      break;
    }
    case EN_ACID: {
      // bubbling toxic cloud
      graphics_context_set_fill_color(ctx, GColorSpringBud);
      graphics_fill_circle(ctx, lp(u, v), 11);
      graphics_context_set_fill_color(ctx, GColorGreen);
      int16_t wob = sin_lookup(e->phase) * 3 / TRIG_MAX_RATIO;
      graphics_fill_circle(ctx, lp(u + wob, v - 4), 6);
      graphics_fill_circle(ctx, lp(u - 4, v + 5 + wob), 5);
      graphics_context_set_fill_color(ctx, GColorIslamicGreen);
      graphics_fill_circle(ctx, lp(u, v), 4);
      break;
    }
    default: break;
  }
}

static void draw_boss(GContext *ctx) {
  int16_t u = s_boss_u_fp >> FP, v = s_boss_v_fp >> FP;
  int p = s_planet;

  if (p == 0) {   // Mercury: blazing sun with a corona
    graphics_context_set_stroke_color(ctx, GColorChromeYellow);
    for (int a = 0; a < TRIG_MAX_ANGLE; a += TRIG_MAX_ANGLE / 12) {
      int16_t sx = sin_lookup(a) * 24 / TRIG_MAX_RATIO;
      int16_t sy = -cos_lookup(a) * 24 / TRIG_MAX_RATIO;
      graphics_draw_line(ctx, lp(u, v), lp(u + sx, v + sy));
    }
    graphics_context_set_fill_color(ctx, s_boss_over ? GColorRed : GColorOrange);
    graphics_fill_circle(ctx, lp(u, v), 18);
    graphics_context_set_fill_color(ctx, GColorChromeYellow);
    graphics_fill_circle(ctx, lp(u, v), 12);
  } else {
    // Generic themed core: outer body in accent, inner in planet colour
    graphics_context_set_fill_color(ctx, s_boss_over ? GColorRed : PLANETS[p].accent);
    graphics_fill_circle(ctx, lp(u, v), 18);
    graphics_context_set_fill_color(ctx, PLANETS[p].planet);
    graphics_fill_circle(ctx, lp(u, v), 11);
  }

  // angry eyes
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, lp(u - 4, v - 5), 2);
  graphics_fill_circle(ctx, lp(u - 4, v + 5), 2);

  // Mars: energy shield while its drones live
  if (p == 2 && s_boss_shield) {
    graphics_context_set_stroke_color(ctx, ((s_tick / 3) % 2) ? GColorCyan : GColorCeleste);
    graphics_draw_circle(ctx, lp(u, v), 24);
    graphics_draw_circle(ctx, lp(u, v), 22);
  }
}

static void draw_hud(GContext *ctx) {
  int16_t W = s_wls;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, ls(0, 0, W, HUD_V), 0, GCornerNone);

  char buf[24];
  // lives at the far left
  snprintf(buf, sizeof(buf), "x%d", s_lives < 0 ? 0 : s_lives);
  draw_text_ls(ctx, buf, 6, 2, false, GColorWhite);

  // HP bar (after the lives)
  int16_t hpx = 28, hpw = 56;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, ls(hpx, 3, hpw, 5), 0, GCornerNone);
  int16_t fill = s_hp_max > 0 ? hpw * s_hp / s_hp_max : 0;
  GColor hc = s_hp <= s_hp_max / 3 ? GColorRed : (s_hp <= s_hp_max / 2 ? GColorChromeYellow : GColorGreen);
  graphics_context_set_fill_color(ctx, hc);
  graphics_fill_rect(ctx, ls(hpx, 3, fill, 5), 0, GCornerNone);

  // weapon-heat gauge (thin, under the HP bar) — only when the jam is enabled
  if (s_jam_on) {
    int16_t hfill = HEAT_MAX > 0 ? hpw * s_heat / HEAT_MAX : 0;
    GColor htc = s_jam > 0 ? (((s_tick / 2) % 2) ? GColorRed : GColorOrange)
                           : (s_heat > HEAT_MAX * 3 / 4 ? GColorOrange : GColorChromeYellow);
    graphics_context_set_fill_color(ctx, s_jam > 0 ? htc : GColorDarkGray);
    graphics_fill_rect(ctx, ls(hpx, 9, hpw, 3), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, htc);
    graphics_fill_rect(ctx, ls(hpx, 9, s_jam > 0 ? hpw : hfill, 3), 0, GCornerNone);
  }

  // special meter at the right
  int16_t sw = 56, sx = W - 6 - sw;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, ls(sx, 3, sw, 7), 0, GCornerNone);
  bool full = s_special >= SPECIAL_MAX;
  GColor sc = full ? ((s_tick / 4) % 2 ? GColorCyan : GColorWhite) : GColorCobaltBlue;
  graphics_context_set_fill_color(ctx, sc);
  graphics_fill_rect(ctx, ls(sx, 3, sw * s_special / SPECIAL_MAX, 7), 0, GCornerNone);

  // score centred in the gap between the HP bar and the special meter
  snprintf(buf, sizeof(buf), "%ld", (long)s_score);
  draw_text_center(ctx, buf, (hpx + hpw + sx) / 2, 2, false, GColorWhite);
}

static void draw_boss_bar(GContext *ctx) {
  int16_t W = s_wls;
  const char *nm = PLANETS[s_planet].boss;
  draw_text_center(ctx, nm, W / 2, HUD_V + 2, false, GColorWhite);
  int16_t bw = W - 40, bx = 20, by = HUD_V + 16;
  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, ls(bx, by, bw, 5), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, s_boss_over ? GColorRed : GColorOrange);
  graphics_fill_rect(ctx, ls(bx, by, bw * (s_boss_hp > 0 ? s_boss_hp : 0) / s_boss_hp_max, 5), 0, GCornerNone);
}

static void draw_game(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // planet at the horizon (large disc low on screen). During the boss intro it
  // slides off-screen so it's clear the boss — not the planet — is the target.
  int16_t planet_slide = 0;
  if (s_phase == PH_BOSS_INTRO)
    planet_slide = 90 * (INTRO_TICKS - s_phase_timer) / INTRO_TICKS;
  else if (s_phase == PH_BOSS || s_phase == PH_CLEARED)
    planet_slide = 90;
  if (planet_slide < 90) {
    graphics_context_set_fill_color(ctx, PLANETS[s_planet].planet);
    graphics_fill_circle(ctx, lp(W - 30, H + 34 + planet_slide), 60);
  }

  for (int i = 0; i < MAX_STARS; i++) {
    int16_t u = s_stars[i].u_fp >> FP;
    if (s_stars[i].layer) {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, ls(u, s_stars[i].v, 2, 2), 0, GCornerNone);
    } else {
      graphics_context_set_stroke_color(ctx, GColorLightGray);
      graphics_draw_pixel(ctx, lp(u, s_stars[i].v));
    }
  }

  // Rebble: ghostly old-Pebble silhouettes drift in the background
  if (s_planet == P_REBBLE) {
    for (int i = 0; i < 6; i++) {
      int16_t su = (s_stars[i].u_fp >> FP);
      int16_t svv = HUD_V + 12 + (i * 29) % (H - HUD_V - 24);
      graphics_context_set_stroke_color(ctx, GColorIslamicGreen);
      graphics_draw_rect(ctx, ls(su - 5, svv - 7, 10, 14));
      graphics_draw_line(ctx, lp(su - 3, svv - 3), lp(su + 3, svv - 3));
    }
  }

  graphics_context_set_fill_color(ctx, GColorCyan);
  for (int i = 0; i < MAX_PB; i++)
    if (s_pb[i].on) graphics_fill_rect(ctx, ls(s_pb[i].u_fp >> FP, s_pb[i].v - 1, 6, 2), 0, GCornerNone);

  for (int i = 0; i < MAX_EB; i++) {
    if (!s_eb[i].on) continue;
    int16_t bx = s_eb[i].u_fp >> FP, by = s_eb[i].v_fp >> FP;
    if (s_eb[i].kind == 2) {                 // ring
      graphics_context_set_stroke_color(ctx, GColorChromeYellow);
      graphics_draw_circle(ctx, lp(bx, by), 9);
      graphics_draw_circle(ctx, lp(bx, by), 7);
    } else {
      graphics_context_set_fill_color(ctx, s_eb[i].kind == 1 ? GColorRed : GColorOrange);
      graphics_fill_circle(ctx, lp(bx, by), s_eb[i].kind == 1 ? 4 : 2);
    }
  }

  for (int i = 0; i < MAX_EN; i++) if (s_en[i].kind != EN_NONE) draw_enemy(ctx, &s_en[i]);
  if (s_phase == PH_BOSS && s_planet != P_MOON && s_planet != P_REBBLE && s_boss_hp > 0) draw_boss(ctx);

  for (int i = 0; i < MAX_PT; i++) {
    if (!s_pt[i].on) continue;
    int16_t r = 1 + (s_pt[i].max - s_pt[i].life);
    graphics_context_set_stroke_color(ctx, (s_pt[i].life % 2) ? GColorYellow : GColorOrange);
    graphics_draw_circle(ctx, lp(s_pt[i].u, s_pt[i].v), r);
  }

  // lightning: telegraph line, then the bolt (Neptune + any boss mimicking it)
  if (s_phase == PH_BOSS && (s_light_warn > 0 || s_light_strike > 0)) {
    if (s_light_warn > 0 && (s_tick / 2) % 2 == 0) {
      graphics_context_set_stroke_color(ctx, GColorCeleste);
      graphics_draw_line(ctx, lp(0, s_light_v), lp(W, s_light_v));
    }
    if (s_light_strike > 0) {
      graphics_context_set_fill_color(ctx, ((s_tick) % 2) ? GColorYellow : GColorWhite);
      graphics_fill_rect(ctx, ls(0, s_light_v - 4, W, 8), 0, GCornerNone);
    }
  }

  draw_ship(ctx, SHIP_U, s_ship_v_fp >> FP);

  // weapon jam warning near the ship
  if (s_jam > 0 && (s_tick / 2) % 2 == 0) {
    int16_t jv = s_ship_v_fp >> FP;
    draw_text_ls(ctx, "JAM", SHIP_U + 12, jv - 4, false, GColorRed);
  }

  // Mercury phase-2 overheat: shimmering orange haze (reduced visibility)
  if (s_phase == PH_BOSS && s_planet == 0 && s_boss_over) {
    graphics_context_set_stroke_color(ctx, GColorOrange);
    int16_t hz = W - 70;
    for (int v2 = HUD_V + 2; v2 < H; v2 += 2) {
      int16_t off = sin_lookup((s_tick * 900 + v2 * 700) % TRIG_MAX_ANGLE) * 6 / TRIG_MAX_RATIO;
      for (int u2 = hz + off; u2 < W; u2 += 3) graphics_draw_pixel(ctx, lp(u2, v2));
    }
  }

  // freeze: icy frost creeps in from the edges while controls are slow
  if (s_phase == PH_BOSS && s_sluggish > 0) {
    graphics_context_set_stroke_color(ctx, GColorCeleste);
    for (int k = 0; k < 26; k++) {
      int16_t uu = (k * 53) % W;
      graphics_draw_line(ctx, lp(uu, HUD_V), lp(uu, HUD_V + 6));
      graphics_draw_line(ctx, lp(uu + 20, H - 6), lp(uu + 20, H));
    }
  }

  draw_hud(ctx);
  if (s_phase == PH_BOSS && s_planet == P_MOON) {
    char tb[24];
    snprintf(tb, sizeof(tb), S(S_TURRETS), count_turrets());
    draw_text_center(ctx, tb, W / 2, HUD_V + 3, false, GColorWhite);
  } else if (s_phase == PH_BOSS && s_planet != P_REBBLE) {
    draw_boss_bar(ctx);
  }
  if (s_phase == PH_BOSS_INTRO) {
    int16_t H2 = b.size.w;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, ls(0, H2 / 2 - 26, W, 52), 0, GCornerNone);
    const char *w1 = (s_planet == P_MOON) ? S(S_APPROACH) : S(S_WARNING);
    draw_text_center_fit(ctx, w1, W / 2, H2 / 2 - 22, s_planet == P_MOON ? GColorLightGray : GColorRed);
    draw_text_center(ctx, PLANETS[s_planet].boss, W / 2, H2 / 2 + 4, false, GColorWhite);
  }
  // Rebble: glitching signal reveal during the flythrough
  if (s_planet == P_REBBLE && s_phase == PH_APPROACH) {
    int16_t H2 = b.size.w;
    if ((s_tick / 3) % 5 != 0) {   // glitchy flicker
      draw_text_wrap2(ctx, S(S_SIGNAL), W / 2, H2 / 2 - 26, FONT_S_H, GColorGreen);
      draw_text_center(ctx, "ORIGIN:  REBBLE", W / 2, H2 / 2, true, GColorGreen);
    }
    if (s_approach_left < s_approach_total / 2)
      draw_text_wrap2(ctx, S(S_OLD_FRIENDS), W / 2, H2 - 36, FONT_S_H, GColorGreen);
  }
}

static void draw_map(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  s_wls = W; s_hls = H;
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  draw_text_center(ctx, S(S_SEL_PLANET), W / 2, 8, false, GColorLightGray);

  int16_t lineV = 60;
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_draw_line(ctx, lp(20, lineV), lp(W - 14, lineV));
  int step = (W - 40) / (NUM_PLANETS - 1);
  for (int i = 0; i < NUM_PLANETS; i++) {
    if (map_hidden(i)) continue;                 // secret planet: not on the map yet
    int16_t u = 20 + i * step;
    bool playable = (s_unlocked >> i) & 1;
    GColor c = playable ? PLANETS[i].planet : GColorDarkGray;
    graphics_context_set_fill_color(ctx, c);
    graphics_fill_circle(ctx, lp(u, lineV), i == s_map_sel ? 7 : 4);
    if (i == s_map_sel) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_circle(ctx, lp(u, lineV), 9);
    }
  }

  draw_text_center(ctx, PNAME(s_map_sel), W / 2, 78, true, GColorWhite);
  bool sel_playable = (s_unlocked >> s_map_sel) & 1;
  draw_text_center(ctx, sel_playable ? S(S_LAUNCH) : S(S_LOCKED), W / 2, 108, false,
                   sel_playable ? GColorCyan : GColorLightGray);
  draw_text_center(ctx, S(S_CHOOSE), W / 2, 126, false, GColorLightGray);

  if (s_portal)
    draw_text_wrap2(ctx, S(S_PORTAL), W / 2, 138, FONT_S_H,
                    ((s_tick / 4) % 2) ? GColorMagenta : GColorCyan);

  // collectible strip
  draw_text_center(ctx, S(S_LOST_PEBBLES), W / 2, H - 40, false, GColorLightGray);
  int16_t sx = W / 2 - (NUM_COLLECT - 1) * 12 / 2;
  for (int i = 0; i < NUM_COLLECT; i++)
    draw_pebble_icon(ctx, sx + i * 12, H - 18, (s_collected >> i) & 1);
}

static void draw_upgrade(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_center(ctx, S(S_CHOOSE_UP), W / 2, 6, false, GColorChromeYellow);

  int16_t cardW = W - 20, cardH = 46;
  for (int i = 0; i < 3; i++) {
    int type = s_up_choices[i];
    int16_t u = 10, v = 26 + i * (cardH + 6);
    bool sel = (i == s_up_sel);
    graphics_context_set_fill_color(ctx, sel ? GColorCobaltBlue : GColorDukeBlue);
    graphics_fill_rect(ctx, ls(u, v, cardW, cardH), 0, GCornerNone);
    if (sel) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_rect(ctx, ls(u, v, cardW, cardH));
    }
    draw_text_ls(ctx, UP_NAME[type], u + 10, v + 7, false, sel ? GColorWhite : GColorLightGray);
    draw_text_ls(ctx, S(S_UD_TIMELINE + type), u + 10, v + 26, false, GColorCeleste);
  }
  draw_text_wrap2(ctx, S(S_UP_HINT), W / 2, H - 20, FONT_S_H, GColorLightGray);
}

static void draw_title(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  s_wls = W; s_hls = H;
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWhite);
  for (int i = 0; i < 24; i++)
    graphics_draw_pixel(ctx, lp((i * 47) % W, HUD_V + (i * 83) % (H - HUD_V)));

  draw_text_center(ctx, "PBL-7", W / 2, 12, true, GColorMediumSpringGreen);
  draw_text_center(ctx, "PEBBLONIAN EXPLORER", W / 2, 42, false, GColorWhite);
  int16_t y = draw_text_wrap2(ctx, S(S_MISSION), W / 2, 58, 15, GColorChromeYellow);
  draw_text_center(ctx, S(S_INTRO), W / 2, y + 2, false, GColorCeleste);
  draw_ship(ctx, W / 2, y + 24);
  draw_text_center(ctx, S(S_START), W / 2, y + 42, false, GColorCyan);
  draw_text_center(ctx, S(S_TITLE_SETTINGS), W / 2, y + 58, false, GColorLightGray);
  draw_text_center(ctx, S(S_HINT_FLY), W / 2, H - 32, false, GColorLightGray);
  draw_text_center(ctx, S(S_HINT_BOMB), W / 2, H - 18, false, GColorLightGray);

  char bb[24];
  snprintf(bb, sizeof(bb), S(S_BEST), (long)s_best_score);
  draw_text_ls(ctx, bb, 6, 2, false, GColorLightGray);
}

static void draw_gameover(GContext *ctx, GRect b) {
  int16_t W = b.size.h;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_center(ctx, S(S_GAMEOVER), W / 2, 40, true, GColorRed);
  char buf[32];
  snprintf(buf, sizeof(buf), S(S_SCORE), (long)s_score);
  draw_text_center(ctx, buf, W / 2, 78, false, GColorWhite);
  if (s_new_record) draw_text_center(ctx, S(S_NEW_RECORD), W / 2, 96, false, GColorGreen);
  if (!s_go_lock) draw_text_wrap2(ctx, S(S_RETRY), W / 2, 128, FONT_S_H, GColorCyan);
}

static void draw_collect_gallery(GContext *ctx, int16_t W, int16_t v) {
  int16_t sx = W / 2 - (NUM_COLLECT - 1) * 14 / 2;
  for (int i = 0; i < NUM_COLLECT; i++) draw_pebble_icon(ctx, sx + i * 14, v, (s_collected >> i) & 1);
}

static void draw_victory(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorVividCerulean);   // Earth
  graphics_fill_circle(ctx, lp(W / 2, 40), 22);
  graphics_context_set_fill_color(ctx, GColorGreen);
  graphics_fill_circle(ctx, lp(W / 2 - 6, 34), 7);

  draw_text_center_fit(ctx, S(S_EARTH_SAVED), W / 2, 74, GColorMediumSpringGreen);
  draw_text_center(ctx, S(S_CORE_DELIVERED), W / 2, 104, false, GColorWhite);
  char buf[32];
  snprintf(buf, sizeof(buf), S(S_SCORE), (long)s_score);
  draw_text_center(ctx, buf, W / 2, 124, false, GColorChromeYellow);
  if (s_new_record) draw_text_center(ctx, S(S_NEW_RECORD), W / 2, 140, false, GColorGreen);
  draw_collect_gallery(ctx, W, H - 22);
  if (!s_go_lock) draw_text_center(ctx, S(S_CONTINUE), W / 2, H - 40, false, GColorCyan);
}

static void draw_secret_ending(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  // magical shimmer border
  GColor sh = ((s_tick / 4) % 2) ? GColorMediumSpringGreen : GColorCyan;
  graphics_context_set_stroke_color(ctx, sh);
  graphics_draw_rect(ctx, ls(4, 4, W - 8, H - 8));

  draw_text_center_fit(ctx, S(S_TRUE_ENDING), W / 2, 16, sh);
  draw_text_center(ctx, S(S_EPI1), W / 2, 48, false, GColorGreen);
  draw_text_center(ctx, S(S_EPI2), W / 2, 66, false, GColorGreen);
  draw_text_center(ctx, S(S_EPI3), W / 2, 82, false, GColorGreen);
  draw_collect_gallery(ctx, W, H - 40);
  if (!s_go_lock) draw_text_center(ctx, S(S_CONTINUE), W / 2, H - 18, false, GColorCyan);
}

#define NUM_SETTINGS 5

static void reset_save(void) {
  s_best_score = 0; s_unlocked = 1; s_collected = 0;
  s_secret_seen = false; s_new_record = false;
  save_progress();
  vibe_double();
}

static void draw_settings(GContext *ctx, GRect b) {
  int16_t W = b.size.h, H = b.size.w;
  graphics_context_set_fill_color(ctx, GColorOxfordBlue);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_center_fit(ctx, S(S_SETTINGS), W / 2, 8, GColorChromeYellow);

  const char *labels[NUM_SETTINGS] = {
    S(S_BACKLIGHT), S(S_WEAPON_JAM), S(S_RUMBLE), S(S_SOUND), S(S_RESET) };
  bool vals[4] = { s_bl_mode != BL_OFF, s_jam_on, s_rumble_on, s_sound_vol > 0 && !speaker_is_muted() };

  for (int i = 0; i < NUM_SETTINGS; i++) {
    int16_t v = 42 + i * 26;
    if (i == s_set_sel) {
      graphics_context_set_fill_color(ctx, GColorCobaltBlue);
      graphics_fill_rect(ctx, ls(8, v - 3, W - 16, 21), 0, GCornerNone);
    }
    // Armed reset: the red goes in the bar rather than the text — red on the
    // cobalt selection bar was the lowest-contrast pair on screen, in the one
    // state that must be read.
    if (i == 4 && s_reset_armed) {
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_rect(ctx, ls(8, v - 3, W - 16, 21), 0, GCornerNone);
      draw_text_ls(ctx, S(S_RESET_CONFIRM), 16, v, false, GColorWhite);
    } else {
      draw_text_ls(ctx, labels[i], 16, v, false, i == s_set_sel ? GColorWhite : GColorLightGray);
      if (i < 4) {
        // Backlight (row 0) is a 3-state cycle, Sound (row 3) a 4-level volume;
        // Weapon Jam and Rumble are simple on/off.
        const char *vt = (i == 0)
          ? ((s_bl_mode == BL_ALWAYS) ? S(S_BL_ALWAYS)
             : (s_bl_mode == BL_TIMED) ? S(S_BL_TIMED) : S(S_OFF))
          : (i == 3)
          ? (s_sound_vol == 0 ? S(S_OFF) : speaker_is_muted() ? S(S_SND_MUTED)
             : s_sound_vol == 1 ? S(S_SND_SOFT)
             : s_sound_vol == 2 ? S(S_SND_NORMAL) : S(S_SND_LOUD))
          : (vals[i] ? S(S_ON) : S(S_OFF));
        draw_text_ls(ctx, vt, W - 14 - text_w_ls(vt, false), v, false,
                     vals[i] ? GColorGreen : GColorLightGray);
      }
    }
  }
  draw_text_center(ctx, S(S_SET_HINT), W / 2, H - 14, false, GColorLightGray);
}

static void layer_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  switch (s_state) {
    case ST_TITLE: draw_title(ctx, b); break;
    case ST_SETTINGS: draw_settings(ctx, b); break;
    case ST_MAP: draw_map(ctx, b); break;
    case ST_PLAYING: draw_game(ctx, b); break;
    case ST_PAUSED:
      draw_game(ctx, b);
      // Size the plate from the word, not from a literal: NL "GEPAUZEERD" is
      // 190px against the old fixed 120, so it hung 35px over each edge and
      // landed on the live game field.
      {
        int16_t pw = text_w_ls(S(S_PAUSED), true) + 24;
        if (pw > b.size.h) pw = b.size.h;
        graphics_context_set_fill_color(ctx, GColorBlack);
        graphics_fill_rect(ctx, ls(b.size.h / 2 - pw / 2, b.size.w / 2 - 20, pw, 40), 0, GCornerNone);
        draw_text_center(ctx, S(S_PAUSED), b.size.h / 2, b.size.w / 2 - 14, true, GColorWhite);
      }
      break;
    case ST_UPGRADE: draw_upgrade(ctx, b); break;
    case ST_GAMEOVER: draw_gameover(ctx, b); break;
    case ST_VICTORY: draw_victory(ctx, b); break;
    case ST_SECRET_ENDING: draw_secret_ending(ctx, b); break;
  }
}


// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
static void map_move(int d) {
  int i = s_map_sel + d;
  while (i >= 0 && i < NUM_PLANETS && map_hidden(i)) i += d;   // skip hidden secrets
  if (i < 0 || i > NUM_PLANETS - 1) return;                    // edge: stay put
  s_map_sel = i;
  layer_mark_dirty(s_layer);
}

static void set_move(int d) {
  s_reset_armed = false;
  int i = s_set_sel + d;
  if (i < 0) i = 0;
  if (i > NUM_SETTINGS - 1) i = NUM_SETTINGS - 1;
  s_set_sel = i;
  layer_mark_dirty(s_layer);
}

static void up_raw_down(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  if (s_state == ST_PLAYING) s_rise = true;
  else if (s_state == ST_MAP) map_move(+1);
  else if (s_state == ST_UPGRADE) { if (s_up_sel > 0) { s_up_sel--; layer_mark_dirty(s_layer); } }
  else if (s_state == ST_SETTINGS) set_move(-1);
  else if (s_state == ST_TITLE) { s_set_sel = 0; s_reset_armed = false; s_state = ST_SETTINGS; layer_mark_dirty(s_layer); }
}
static void up_raw_up(ClickRecognizerRef rec, void *ctx) { s_rise = false; }
static void down_raw_down(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  if (s_state == ST_PLAYING) s_shoot = true;
  else if (s_state == ST_MAP) map_move(-1);
  else if (s_state == ST_UPGRADE) { if (s_up_sel < 2) { s_up_sel++; layer_mark_dirty(s_layer); } }
  else if (s_state == ST_SETTINGS) set_move(+1);
}
static void down_raw_up(ClickRecognizerRef rec, void *ctx) { s_shoot = false; }

static void settings_select(void) {
  switch (s_set_sel) {
    case 0: s_bl_mode = (s_bl_mode + 1) % NUM_BL_MODES;   // Off -> Timeout -> Always
            apply_backlight();
            persist_write_int(PK_BL_MODE, s_bl_mode); break;
    case 1: s_jam_on = !s_jam_on; persist_write_bool(PK_JAM, s_jam_on); break;
    case 2: s_rumble_on = !s_rumble_on; persist_write_bool(PK_RUMBLE, s_rumble_on); if (s_rumble_on) vibe_short(); break;
    case 3: s_sound_vol = (s_sound_vol + 1) % 4; persist_write_int(PK_SOUND_VOL, s_sound_vol); if (s_sound_vol) sfx_collect(); break;
    case 4:
      if (s_reset_armed) { reset_save(); s_reset_armed = false; }
      else s_reset_armed = true;
      break;
  }
  layer_mark_dirty(s_layer);
}

static void select_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_TITLE: new_game(); break;
    case ST_SETTINGS: settings_select(); break;
    case ST_MAP:
      if ((s_unlocked >> s_map_sel) & 1) start_planet(s_map_sel);
      break;
    case ST_PLAYING: smart_bomb(); break;
    case ST_PAUSED: s_state = ST_PLAYING; start_timer(); break;
    case ST_UPGRADE: {
      apply_upgrade(s_up_choices[s_up_sel]);
      int nx = next_mainline(s_planet);
      s_map_sel = (nx >= 0) ? nx : s_planet;
      s_state = ST_MAP;
      layer_mark_dirty(s_layer);
      break;
    }
    case ST_GAMEOVER: if (!s_go_lock) new_game(); break;
    case ST_VICTORY:
    case ST_SECRET_ENDING:
      if (!s_go_lock) { s_state = ST_TITLE; layer_mark_dirty(s_layer); }
      break;
  }
}

static void back_click(ClickRecognizerRef rec, void *ctx) {
  backlight_touch();
  switch (s_state) {
    case ST_PLAYING: s_state = ST_PAUSED; s_rise = false; s_shoot = false; stop_timer(); layer_mark_dirty(s_layer); break;
    case ST_PAUSED: s_state = ST_MAP; layer_mark_dirty(s_layer); break;
    case ST_MAP: s_state = ST_TITLE; layer_mark_dirty(s_layer); break;
    case ST_UPGRADE: break;
    case ST_SETTINGS: s_reset_armed = false; s_state = ST_TITLE; layer_mark_dirty(s_layer); break;
    case ST_GAMEOVER: s_state = ST_TITLE; layer_mark_dirty(s_layer); break;
    case ST_TITLE: window_stack_pop(true); break;
    case ST_VICTORY:
    case ST_SECRET_ENDING: s_state = ST_TITLE; layer_mark_dirty(s_layer); break;
  }
}

static void click_config(void *ctx) {
  window_raw_click_subscribe(BUTTON_ID_UP, up_raw_down, up_raw_up, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN, down_raw_down, down_raw_up, NULL);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, back_click);
}

// ---------------------------------------------------------------------------
static void focus_handler(bool in_focus) {
  if (!in_focus && s_state == ST_PLAYING) {
    s_state = ST_PAUSED; s_rise = false; s_shoot = false; stop_timer(); layer_mark_dirty(s_layer);
  }
}
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, layer_update);
  layer_add_child(root, s_layer);
}
static void window_unload(Window *window) { layer_destroy(s_layer); }

static void init(void) {
  srand(time(NULL));
  pick_language();
  load_progress();
  sfx_load();
  apply_backlight();
  s_window = window_create();
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers) { .load = window_load, .unload = window_unload });
  app_focus_service_subscribe(focus_handler);
  window_stack_push(s_window, true);
}
static void deinit(void) {
  stop_timer();
  backlight_disable();
  app_focus_service_unsubscribe();
  save_progress();
  window_destroy(s_window);
}
int main(void) { init(); app_event_loop(); deinit(); }
