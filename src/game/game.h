// Port de com.nitrome.throwgame.* (Mutiny, Nitrome 2008) a C.
// Los nombres siguen a las clases originales para poder compararlas.
#ifndef MUT_GAME_H
#define MUT_GAME_H
#include <stdbool.h>

#include "../core/mc.h"
#include "../platform/platform.h"

// Vista de juego en la pantalla de arriba (el original es 550x400)
#define VIEW_W TOP_W
#define VIEW_H TOP_H
#define VIEW_CX (VIEW_W / 2)
#define VIEW_CY (VIEW_H / 2)
#define WATER_MARGIN 192       // el agua no sube de esta fila de la pantalla (320 en 400)
#define BG_SHIFT (-(320 - WATER_MARGIN))

typedef enum {
    W_NONE = 0,
    W_CHERRYBOMB, W_DYNAMITE, W_BOULDER, W_PIECESOFEIGHT, W_RUMBOTTLE, W_BANANA, W_PARACHUTEBOMB,
    W_WOODENCRATE, W_GUNPOWDERBARREL, W_SEAGULL, W_MINE, W_CANNON, W_ANCHOR, W_VOODOODOLL, W_TIDALWAVE,
    W_CANNONBALL_ITEM,  // "cannonball": arma por defecto sin munición (no tiene clase en el original)
    W_CANNONBALL, W_SEAGULLFIRE, W_SIMSHOT,
    W_COUNT
} WType;

extern const char *const weapon_names[W_COUNT];  // nombre AS ("cherryBomb"...)
WType weapon_from_name(const char *name);

enum { CK_PLAIN, CK_DEBRIS, CK_EXPLOSION, CK_FLAME, CK_CHEST, CK_SOLID, CK_SPEECH };

typedef struct Clip {
    MC *parent;
    char link[32];
    MC *mc, *holder;
    bool use_holder;
    float x, y, rotation;
    int kind;
    void *owner;
} Clip;

void clip_init(Clip *c, MC *parent, const char *link, int kind, void *owner);
void clip_show(Clip *c);
void clip_hide(Clip *c);
void clip_update(Clip *c);
void clip_destroy(Clip *c);

typedef struct Team Team;
typedef struct Character Character;
typedef struct Weapon Weapon;
typedef struct Solid Solid;

enum { FLOOR = 0, WALL_RIGHT = 1, CEILING = 2, WALL_LEFT = 3 };
enum { SO_CHARACTER = 1, SO_WEAPON = 2 };

struct Solid {
    Clip clip;
    int so;
    float left, right, top, bottom;
    float vx, vy, weight, friction, bounce;
    bool draggable, twangable;
    float twang_max;
    Clip pin;
    bool pin_shown;
    bool hits_tiles, hits_boxes, over_water;
    float drag_x, drag_y;
};

typedef struct {
    float vx, vy, ex, ey;
    float x;            // ancla
    float sx, sy;       // cañón: posición inicial
    int angle;
    float success;
    Character *player;
    int weapon;         // índice en has_weapons, -1 = lanzar al personaje
    Character *target;  // muñeco vudú
    int shots[16], nshots;
    float height;
    float poss[16][2];
    int nposs;
} AIMove;

#define MAX_WEAPONS 96
struct Character {
    Solid s;
    Team *team;
    int index;
    bool alive;
    float health, max_health, shown_health;
    MC *overlay, *dead_clip;
    Weapon *weapon;
    int weapon_index;
    bool weapon_selected, weapon_locked, thrown, throw_finished, hit;
    float map_visibility;
    bool ai_finished, ai_throw_checked;
    int ai_weapons_checked;
    AIMove ai_remember;
    bool has_remember;
    AIMove *ai_moves;
    int nai, capai;
    AIMove *ai_throws;
    int nthrows;
    bool can_throw, can_shoot;
    float evilness;
    int luck;
    int contact_time;
    WType has[MAX_WEAPONS];
    int nhas;
    WType inf[W_COUNT];
    int ninf;
};

struct Weapon {
    Solid s;
    WType type;
    Character *owner;
    bool fired, finished, simulation, sim_finished, placeable, limited_to_turn, track, show_circle;
    float track_x, track_y, drag_range, drag_offset;
    // Por tipo
    float visibility;
    bool hit_bottom;
    int hold_time, fade_time, ai_wait, test_delay;
    Weapon *next_box;
    int create_more, ai_delay, ai_delay_after, ai_index;
    float ai_offset, ai_next_x, ai_next_y;
    float ai_list[3][2];
    int nai_list;
    bool mouse_down;
    bool dragging_pin;
    float fire_strength, pin_x;
    Weapon *cannonball;
    int ai_fire_time;
    AIMove ai_fire;
    int ignore_time, countdown;
    bool active, exploded;
    int beeps[10], nbeeps;
    bool chute_open;
    int frames_from_fire;
    int times_fired;
    float last_sq;
    Character *target;
    int frames_on_this, frames_on_target;
    bool thrown_character;
    float throw_vx, throw_vy;
    Weapon *shots[32];
    int nshots;
    Clip dotted;
    int ai_shot_x[16], nai_shot_x;
    Weapon *gull;  // disparo de gaviota: su gaviota
    bool in_boxes;
};

struct Team {
    int number;
    Character *chars[16];
    int n;
    Character *selected;
    bool ai, ai_finished, ai_can_bail;
    int turns;
    AIMove ai_move;
    bool has_ai_move;
};

typedef struct {
    Clip clip;
    bool falling, finished;
    Character *touched;
    int time_taken, to_next;
    float visibility, floor_y;
    WType contains[5];
    int counts[5];     // cuántas de cada una (1 en el modo historia; 1-10 en dos jugadores)
    int ncontains;
    WType released;
} Chest;

typedef struct {
    Clip clip;
    char text[256];
    int shown;
    int start_time, complete_time;
    Character *target;
} Speech;

// Estado global (Controller + TileSystem)
typedef struct {
    MC *root, *background, *holder, *content;
    MC *back_tiles, *objects, *effects, *tiles, *characters, *water_layer;
    MC *range_circle;
    MC *text_area;
    Team teams[2];
    Team *current;
    Solid *dragging, *twanging;
    Character *hover;
    int inactivity;
    int sky;
    // TileSystem
    int lw, lh;
    uint16_t *grid, *bggrid;
    MC **tile_mc, **bgtile_mc;
    int *drop_cols, ndrop;
    float cam_x, cam_y;
    Character *pan_to;
    float scroll_x, scroll_y;
    int scroll_dx, scroll_dy;
    bool mouse_down;
    int anim_counter;
    // Ratón virtual en coordenadas del mundo (lo mueven el panel táctil y la cruceta)
    float mouse_x, mouse_y;
    bool mouse_visible;
    // Agua
    Clip water;
    float water_y;
    bool has_water;
    // Listas
    Weapon *boxes[64];
    int nboxes;
    Weapon *mines[32];
    int nmines;
    Chest *chests[8];
    int nchests;
    WType potential[64];
    int npotential;
    int max_chests;
    Speech *speech;
    int speech_line;
    // Objetos del nivel (se liberan al descargar)
    void **alloc;
    int nalloc, capalloc;
    // Texto de turno
    char lines[8][96];
    int nlines, line_frame;
    // Fin de nivel
    int result;  // 0 jugando, 1 gana P1, 2 gana P2, 3 empate, 4 nivel superado (1P), 5 perdido, 6 juego completo
    bool popup;
    int level;
    int game_type;
    float score_gain;
} World;

// Partida de pruebas (START+SELECT): todas las armas para todos los piratas
extern bool world_give_all;

extern World W;

void *world_alloc(size_t size);

// Controller
void world_start(int level, int game_type);
void world_unload(void);
void world_tick(void);
void world_next_turn(void);
int world_level_score(void);
const char *world_line(int n);
void world_say(const char *s);

// TileSystem
static inline bool tile_at(int x, int y) {
    if (x < 0 || y < 0 || x >= W.lw || y >= W.lh) return false;
    uint16_t t = W.grid[y * W.lw + x];
    return t != 0 && t != 0xFFFF;
}
void tiles_pan(float x, float y, bool force);
float tiles_pan_towards(float tx, float ty, float step);
void tiles_mouse_down(void);
void tiles_mouse_up(void);

// Solid
void solid_init(Solid *s, MC *parent, const char *link, int so);
void solid_advance_motion(Solid *s);  // con despacho a la subclase
void solid_base_motion(Solid *s);     // Solid.advanceMotion
void solid_splash_check(Solid *s);
void solid_twang(Solid *s);
void solid_draw_twang_line(Solid *s);
void solid_update(Solid *s);
void solid_contact(Solid *s, int side);
void solid_release(Solid *s);

// Character
Character *character_new(const char *link);
void character_set_weapons(Character *c, const char **keys, const char **vals, int n);
void character_advance(Character *c);
void character_ai_start(Character *c);
void character_ai_think(Character *c);
void character_equip(Character *c, int index);
void character_unequip(Character *c);
void character_weapon_expired(Character *c);
void character_subtract_health(Character *c, float amount);
int character_count_weapon(Character *c, WType w);
int character_first_weapon(Character *c, WType w);
bool character_weapon_infinite(Character *c, WType w);
bool character_targeting_voodoo(Character *c);
void character_release(Character *c);
void character_twang(Character *c);
void character_destroy(Character *c);
int character_random_throws(Character *c, int count, AIMove *out);

// Team
void team_init(Team *t, int number, int game_type);
void team_advance(Team *t);
bool team_any_alive(Team *t);
int team_count_alive(Team *t);
Character *team_speaker(Team *t);
Character *team_captain(Team *t);
void team_select(Team *t, Character *c, bool again);
void team_start_turn(Team *t);
void team_finish_turn(Team *t);
bool team_turn_complete(Team *t);
void team_continue_turn(Team *t);
const char *team_type(Team *t);
float team_average_health(Team *t);
void team_destroy(Team *t);

// Weapons
Weapon *weapon_new(WType type);
void weapon_advance(Weapon *w);
void weapon_motion(Weapon *w);       // advanceMotion con su sobrecarga
void weapon_contact(Weapon *w, int side);
void weapon_release(Weapon *w);
void weapon_twang(Weapon *w);
void weapon_fire(Weapon *w, float vx, float vy);
void weapon_place(Weapon *w, float x, float y);
void weapon_update(Weapon *w);
void weapon_destroy(Weapon *w);
int weapon_random_throws(Weapon *w, int count, AIMove *out);
void weapon_ai_perform(Weapon *w, AIMove *m);
bool box_can_place(float px, float py);
void box_explode(Weapon *w);
int box_ai_simulation(Weapon *w, float out[][2], int max);
float seagull_ai_simulation(Weapon *w, int *shots, int *nshots, float *height);
void seagull_end_shot(Weapon *gull, Weapon *shot);
void twang_prediction(Solid *s, float *vx, float *vy);

// Efectos
void explosion_new(float x, float y, float size, float max_damage, Character *caster);
void debris_new(MC *layer, const char *link, float x, float y);
void flame_new(float x, float y, bool to_right);

// Cofres
void chest_drop_new(void);
Chest *chest_falling(void);
void chest_advance(Chest *c);

// Bocadillo
Speech *speech_new(const char *text, Character *target);
void speech_advance(Speech *s);
void speech_destroy(Speech *s);

// Sonido (SfxManager)
void sfx(const char *name);

// Hooks del runtime
void world_clip_call(MC *mc, const char *stmt);

// Mapa (minimapa de la pantalla de abajo lo dibuja la interfaz)
float frand(void);  // Math.random()

#endif
