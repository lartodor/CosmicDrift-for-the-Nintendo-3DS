/*
 * COSMIC DRIFT
 * A Nintendo 3DS Homebrew Game
 *
 * Build with devkitARM + libctru + citro2d
 * Compile: make
 * Output:  CosmicDrift.3dsx / CosmicDrift.cia
 *
 * Controls:
 *   Circle Pad / D-Pad  - Move ship
 *   A / ZR              - Shoot
 *   B                   - Boost (uses boost meter)
 *   Start               - Pause
 *   Select (on menu)    - Quit
 *
 * Game loop:
 *   Survive endless waves of alien ships & asteroids.
 *   Collect orbs to power up weapons and restore shields.
 *   Every 10 kills escalates the wave. 3 hits = game over.
 */

#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────── */
/*  Constants                                                   */
/* ─────────────────────────────────────────────────────────── */

#define SCREEN_W      400
#define SCREEN_H      240
#define BOT_W         320
#define BOT_H         240

#define MAX_BULLETS    64
#define MAX_ENEMIES    32
#define MAX_ASTEROIDS  20
#define MAX_PARTICLES 128
#define MAX_STARS      80
#define MAX_ORBS       12
#define MAX_EXPLOSIONS 16

#define PLAYER_SPEED      2.8f
#define PLAYER_BOOST      5.5f
#define BULLET_SPEED      7.0f
#define ENEMY_BULLET_SPD  3.5f
#define BOOST_MAX        100.0f
#define BOOST_REGEN       0.3f
#define BOOST_COST        1.5f
#define SHOOT_COOLDOWN    12
#define ENEMY_SHOOT_CD    90

#define SHIP_RADIUS    10.0f
#define BULLET_RADIUS   4.0f
#define ENEMY_RADIUS   12.0f
#define ORB_RADIUS      7.0f
#define ASTEROID_RADIUS 18.0f

#define MAX_SHIELDS  3
#define INVULN_TIME 90   /* frames of invulnerability after hit */

/* ─────────────────────────────────────────────────────────── */
/*  Colour helpers                                              */
/* ─────────────────────────────────────────────────────────── */

#define RGBA(r,g,b,a) C2D_Color32(r,g,b,a)

static const u32 COL_BG        = 0xFF0A0418;
static const u32 COL_SHIP      = 0xFF00FFCC;
static const u32 COL_SHIP_CORE = 0xFFFFFFFF;
static const u32 COL_BULLET    = 0xFF00FFFF;
static const u32 COL_E_BULLET  = 0xFFFF4444;
static const u32 COL_ENEMY_A   = 0xFFFF6600;
static const u32 COL_ENEMY_B   = 0xFFFF00AA;
static const u32 COL_ENEMY_C   = 0xFFAA00FF;
static const u32 COL_ASTEROID  = 0xFF8888AA;
static const u32 COL_ORB_SHIELD= 0xFF44FF88;
static const u32 COL_ORB_POWER = 0xFFFFEE00;
static const u32 COL_BOOST_BAR = 0xFF00CCFF;
static const u32 COL_SHIELD_ON = 0xFF44FF88;
static const u32 COL_SHIELD_OFF= 0xFF334433;
static const u32 COL_WHITE     = 0xFFFFFFFF;
static const u32 COL_YELLOW    = 0xFFFFEE00;
static const u32 COL_RED       = 0xFFFF3333;
static const u32 COL_DARK      = 0xFF111122;

/* ─────────────────────────────────────────────────────────── */
/*  Structs                                                     */
/* ─────────────────────────────────────────────────────────── */

typedef struct {
    float x, y, vx, vy;
    bool  active;
    bool  enemy;     /* true = enemy bullet */
} Bullet;

typedef struct {
    float x, y, vx, vy;
    int   hp;
    int   type;      /* 0=scout 1=fighter 2=boss */
    int   shoot_cd;
    bool  active;
    float angle;
    int   frame;
} Enemy;

typedef struct {
    float x, y, vx, vy;
    float angle, spin;
    float radius;
    int   hp;
    bool  active;
} Asteroid;

typedef struct {
    float x, y, vx, vy;
    u32   color;
    int   life, max_life;
    float size;
    bool  active;
} Particle;

typedef struct {
    float x, y;
    float twinkle;
    float speed;   /* parallax scroll speed */
    float size;
} Star;

typedef struct {
    float x, y, vx, vy;
    int   type;    /* 0=shield, 1=power */
    bool  active;
    float pulse;
} Orb;

typedef struct {
    float x, y;
    float radius, max_radius;
    int   life, max_life;
    u32   color;
    bool  active;
} Explosion;

typedef enum {
    STATE_TITLE,
    STATE_PLAY,
    STATE_PAUSED,
    STATE_GAMEOVER,
    STATE_HISCORE
} GameState;

/* ─────────────────────────────────────────────────────────── */
/*  Global game state                                           */
/* ─────────────────────────────────────────────────────────── */

static GameState g_state      = STATE_TITLE;
static int       g_frame      = 0;      /* total frame counter */
static int       g_score      = 0;
static int       g_hiscore    = 0;
static int       g_kills      = 0;
static int       g_wave       = 1;
static int       g_combo      = 0;
static int       g_combo_timer= 0;
static int       g_power_level= 1;      /* 1-3 */
static bool      g_new_hi     = false;

/* Player */
static float  px, py;
static float  pvx, pvy;
static int    p_shields;
static int    p_invuln;
static float  p_boost;
static bool   p_boosting;
static int    p_shoot_cd;
static float  p_angle;      /* visual tilt */

/* Arrays */
static Bullet    bullets[MAX_BULLETS];
static Enemy     enemies[MAX_ENEMIES];
static Asteroid  asteroids[MAX_ASTEROIDS];
static Particle  particles[MAX_PARTICLES];
static Star      stars[MAX_STARS];
static Orb       orbs[MAX_ORBS];
static Explosion explosions[MAX_EXPLOSIONS];

/* Wave spawn */
static int wave_spawn_timer = 0;
static int wave_enemies_left= 0;

/* ─────────────────────────────────────────────────────────── */
/*  Utility                                                     */
/* ─────────────────────────────────────────────────────────── */

static inline float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}
static inline float randf_range(float lo, float hi) {
    return lo + randf() * (hi - lo);
}
static inline float dist2(float ax,float ay,float bx,float by){
    float dx=ax-bx, dy=ay-by;
    return dx*dx+dy*dy;
}
static inline float lerpf(float a,float b,float t){
    return a+(b-a)*t;
}
/* Wrap angle to -PI..PI */
static inline float wrap_angle(float a){
    while(a> M_PI) a-=2.0f*(float)M_PI;
    while(a<-M_PI) a+=2.0f*(float)M_PI;
    return a;
}

/* ─────────────────────────────────────────────────────────── */
/*  Spawn helpers                                               */
/* ─────────────────────────────────────────────────────────── */

static void spawn_particle(float x,float y,float vx,float vy,
                           u32 col,int life,float size){
    for(int i=0;i<MAX_PARTICLES;i++){
        if(!particles[i].active){
            particles[i]=(Particle){x,y,vx,vy,col,life,life,size,true};
            return;
        }
    }
}

static void spawn_explosion(float x,float y,float radius,u32 col){
    for(int i=0;i<MAX_EXPLOSIONS;i++){
        if(!explosions[i].active){
            explosions[i]=(Explosion){x,y,0,radius,20,20,col,true};
            return;
        }
    }
}

static void spawn_bullet(float x,float y,float vx,float vy,bool enemy){
    for(int i=0;i<MAX_BULLETS;i++){
        if(!bullets[i].active){
            bullets[i]=(Bullet){x,y,vx,vy,true,enemy};
            return;
        }
    }
}

static void spawn_orb(float x,float y,int type){
    for(int i=0;i<MAX_ORBS;i++){
        if(!orbs[i].active){
            float vx=randf_range(-0.5f,0.5f);
            float vy=randf_range(-0.5f,0.5f);
            orbs[i]=(Orb){x,y,vx,vy,type,true,0};
            return;
        }
    }
}

static void fire_player_bullets(void){
    if(p_shoot_cd>0) return;
    p_shoot_cd=SHOOT_COOLDOWN;
    float bvx=0, bvy=-BULLET_SPEED;
    /* Power level spread */
    if(g_power_level==1){
        spawn_bullet(px,py-12,bvx,bvy,false);
    } else if(g_power_level==2){
        spawn_bullet(px-6,py-10,-0.5f,bvy,false);
        spawn_bullet(px+6,py-10, 0.5f,bvy,false);
    } else {
        spawn_bullet(px,  py-12, 0.0f,bvy,false);
        spawn_bullet(px-8,py-8, -0.8f,bvy,false);
        spawn_bullet(px+8,py-8,  0.8f,bvy,false);
    }
    /* Muzzle flash particles */
    for(int i=0;i<6;i++){
        spawn_particle(px,py-12,
            randf_range(-2,2),randf_range(-4,-1),
            COL_BULLET,10,2.0f);
    }
}

static void fire_enemy_bullet(float ex,float ey){
    /* Aim at player */
    float dx=px-ex, dy=py-ey;
    float d=sqrtf(dx*dx+dy*dy);
    if(d<1.0f) return;
    float speed=ENEMY_BULLET_SPD;
    spawn_bullet(ex,ey,(dx/d)*speed,(dy/d)*speed,true);
}

/* ─────────────────────────────────────────────────────────── */
/*  Wave management                                             */
/* ─────────────────────────────────────────────────────────── */

static void start_wave(void){
    wave_enemies_left = 4 + g_wave * 2;
    if(wave_enemies_left>MAX_ENEMIES) wave_enemies_left=MAX_ENEMIES;
    wave_spawn_timer  = 0;
    /* Spawn asteroids */
    int ast_count = 2 + g_wave;
    if(ast_count>MAX_ASTEROIDS) ast_count=MAX_ASTEROIDS;
    for(int i=0;i<ast_count;i++){
        for(int j=0;j<MAX_ASTEROIDS;j++){
            if(!asteroids[j].active){
                float side=(float)(rand()%4);
                float ax,ay;
                if(side<1){ax=randf_range(0,SCREEN_W);ay=-25;}
                else if(side<2){ax=SCREEN_W+25;ay=randf_range(0,SCREEN_H);}
                else if(side<3){ax=randf_range(0,SCREEN_W);ay=SCREEN_H+25;}
                else{ax=-25;ay=randf_range(0,SCREEN_H);}
                float speed=randf_range(0.5f,1.5f+g_wave*0.1f);
                float angle=atan2f(SCREEN_H/2-ay,SCREEN_W/2-ax)+randf_range(-0.5f,0.5f);
                int hp=1+(g_wave/3);
                asteroids[j]=(Asteroid){
                    ax,ay,
                    cosf(angle)*speed,sinf(angle)*speed,
                    randf_range(0,(float)M_PI*2),randf_range(-0.03f,0.03f),
                    randf_range(12,20),(hp>3?3:hp),true
                };
                break;
            }
        }
    }
}

static void try_spawn_enemy(void){
    if(wave_enemies_left<=0) return;
    wave_spawn_timer++;
    int interval=60-(g_wave*2);
    if(interval<20) interval=20;
    if(wave_spawn_timer<interval) return;
    wave_spawn_timer=0;
    wave_enemies_left--;

    for(int i=0;i<MAX_ENEMIES;i++){
        if(!enemies[i].active){
            /* Spawn from random edge */
            float ex,ey;
            int side=rand()%4;
            if(side==0){ex=randf_range(20,SCREEN_W-20);ey=-20;}
            else if(side==1){ex=SCREEN_W+20;ey=randf_range(20,SCREEN_H-20);}
            else if(side==2){ex=randf_range(20,SCREEN_W-20);ey=SCREEN_H+20;}
            else{ex=-20;ey=randf_range(20,SCREEN_H-20);}

            int type=0;
            int r=rand()%10;
            if(g_wave>=3 && r<3) type=1;
            if(g_wave>=6 && r<2) type=2;

            int hp=1;
            if(type==1) hp=2;
            if(type==2) hp=5;

            enemies[i]=(Enemy){
                ex,ey,0,0,hp,type,
                ENEMY_SHOOT_CD-(g_wave*3<40?g_wave*3:40),
                true,0,0
            };
            /* Move generally toward center */
            float dx=SCREEN_W/2-ex+randf_range(-40,40);
            float dy=SCREEN_H/2-ey+randf_range(-40,40);
            float d=sqrtf(dx*dx+dy*dy);
            float spd=randf_range(0.8f,1.5f+g_wave*0.1f);
            enemies[i].vx=(dx/d)*spd;
            enemies[i].vy=(dy/d)*spd;
            break;
        }
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Init / reset                                               */
/* ─────────────────────────────────────────────────────────── */

static void init_stars(void){
    for(int i=0;i<MAX_STARS;i++){
        stars[i].x     = randf_range(0,SCREEN_W);
        stars[i].y     = randf_range(0,SCREEN_H);
        stars[i].twinkle= randf_range(0,(float)M_PI*2);
        stars[i].speed = randf_range(0.2f,1.0f);
        stars[i].size  = randf_range(0.5f,2.0f);
    }
}

static void reset_game(void){
    memset(bullets,   0,sizeof(bullets));
    memset(enemies,   0,sizeof(enemies));
    memset(asteroids, 0,sizeof(asteroids));
    memset(particles, 0,sizeof(particles));
    memset(orbs,      0,sizeof(orbs));
    memset(explosions,0,sizeof(explosions));

    px=SCREEN_W/2.0f; py=SCREEN_H-40.0f;
    pvx=0; pvy=0;
    p_shields   = MAX_SHIELDS;
    p_invuln    = 0;
    p_boost     = BOOST_MAX;
    p_boosting  = false;
    p_shoot_cd  = 0;
    p_angle     = 0;
    g_score     = 0;
    g_kills     = 0;
    g_wave      = 1;
    g_combo     = 0;
    g_combo_timer=0;
    g_power_level=1;
    g_new_hi    = false;
    g_frame     = 0;

    start_wave();
}

/* ─────────────────────────────────────────────────────────── */
/*  Hit player                                                  */
/* ─────────────────────────────────────────────────────────── */

static void hit_player(void){
    if(p_invuln>0) return;
    p_shields--;
    p_invuln=INVULN_TIME;
    g_combo=0;
    g_combo_timer=0;
    /* Screen shake handled via render offset */
    /* Big particle burst */
    for(int i=0;i<30;i++){
        float ang=randf_range(0,(float)M_PI*2);
        float spd=randf_range(1,5);
        spawn_particle(px,py,cosf(ang)*spd,sinf(ang)*spd,
            COL_RED,30+(int)(randf()*20),3.0f);
    }
    spawn_explosion(px,py,30,COL_RED);
    if(p_shields<=0){
        /* Game over */
        if(g_score>g_hiscore){ g_hiscore=g_score; g_new_hi=true; }
        g_state=STATE_GAMEOVER;
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Kill enemy                                                  */
/* ─────────────────────────────────────────────────────────── */

static void kill_enemy(int idx){
    Enemy *e=&enemies[idx];
    g_kills++;
    g_combo++;
    g_combo_timer=120;

    int base_pts=10;
    if(e->type==1) base_pts=25;
    if(e->type==2) base_pts=100;
    int pts=base_pts*(g_combo>4?4:g_combo);
    g_score+=pts;

    u32 col=(e->type==0)?COL_ENEMY_A:(e->type==1)?COL_ENEMY_B:COL_ENEMY_C;
    /* Explosion */
    spawn_explosion(e->x,e->y,25+(e->type*15),col);
    for(int i=0;i<20+e->type*10;i++){
        float ang=randf_range(0,(float)M_PI*2);
        float spd=randf_range(0.5f,4.0f);
        spawn_particle(e->x,e->y,cosf(ang)*spd,sinf(ang)*spd,col,
            20+(int)(randf()*30),randf_range(1,4));
    }
    /* Maybe drop orb */
    if(randf()<0.25f) spawn_orb(e->x,e->y,(randf()<0.5f)?0:1);

    /* Power level based on kills */
    if(g_kills%15==0 && g_power_level<3) g_power_level++;

    e->active=false;

    /* Check wave complete */
    int alive=0;
    for(int i=0;i<MAX_ENEMIES;i++) if(enemies[i].active) alive++;
    if(alive==0 && wave_enemies_left==0){
        g_wave++;
        g_score+=g_wave*50;
        start_wave();
    }
}

static void kill_asteroid(int idx){
    Asteroid *a=&asteroids[idx];
    g_score+=5;
    g_combo++;
    g_combo_timer=90;

    spawn_explosion(a->x,a->y,20,COL_ASTEROID);
    for(int i=0;i<12;i++){
        float ang=randf_range(0,(float)M_PI*2);
        float spd=randf_range(0.3f,3.0f);
        spawn_particle(a->x,a->y,cosf(ang)*spd,sinf(ang)*spd,
            COL_ASTEROID,15+(int)(randf()*15),2.0f);
    }
    /* Split large ones */
    if(a->radius>14 && a->hp<=0){
        for(int s=0;s<2;s++){
            for(int i=0;i<MAX_ASTEROIDS;i++){
                if(!asteroids[i].active){
                    float ang2=randf_range(0,(float)M_PI*2);
                    asteroids[i]=(Asteroid){
                        a->x+randf_range(-10,10),
                        a->y+randf_range(-10,10),
                        cosf(ang2)*1.5f,sinf(ang2)*1.5f,
                        randf_range(0,(float)M_PI*2),
                        randf_range(-0.05f,0.05f),
                        a->radius*0.55f,1,true
                    };
                    break;
                }
            }
        }
    }
    if(randf()<0.12f) spawn_orb(a->x,a->y,0);
    a->active=false;
}

/* ─────────────────────────────────────────────────────────── */
/*  Update                                                      */
/* ─────────────────────────────────────────────────────────── */

static int shake_timer=0;

static void update_game(u32 held, u32 down, circlePosition cp){
    (void)down; /* reserved for future use (key-down events) */
    g_frame++;
    if(shake_timer>0) shake_timer--;

    /* ── Player movement ── */
    float ax=0,ay=0;
    float dx_stick=(float)cp.dx/150.0f;
    float dy_stick=-(float)cp.dy/150.0f;
    if(fabsf(dx_stick)>0.15f) ax+=dx_stick;
    if(fabsf(dy_stick)>0.15f) ay+=dy_stick;
    if(held&KEY_LEFT  || held&KEY_DLEFT)  ax=-1;
    if(held&KEY_RIGHT || held&KEY_DRIGHT) ax= 1;
    if(held&KEY_UP    || held&KEY_DUP)    ay=-1;
    if(held&KEY_DOWN  || held&KEY_DDOWN)  ay= 1;

    /* Normalize diagonal */
    float alen=sqrtf(ax*ax+ay*ay);
    if(alen>1.0f){ax/=alen;ay/=alen;}

    /* Boost */
    p_boosting=(held&KEY_B) && p_boost>0 && alen>0.1f;
    float spd= p_boosting ? PLAYER_BOOST : PLAYER_SPEED;
    if(p_boosting){
        p_boost-=BOOST_COST;
        if(p_boost<0) p_boost=0;
        /* Boost trail */
        spawn_particle(px,py,
            randf_range(-1,1),randf_range(1,3),
            COL_BOOST_BAR,8,3.0f);
    } else {
        p_boost+=BOOST_REGEN;
        if(p_boost>BOOST_MAX) p_boost=BOOST_MAX;
    }

    pvx=lerpf(pvx,ax*spd,0.18f);
    pvy=lerpf(pvy,ay*spd,0.18f);

    /* Visual tilt */
    float target_angle=pvx*0.06f;
    p_angle=lerpf(p_angle,target_angle,0.12f);

    px+=pvx; py+=pvy;
    /* Clamp to screen */
    if(px<12) px=12;
    if(px>SCREEN_W-12) px=SCREEN_W-12;
    if(py<12) py=12;
    if(py>SCREEN_H-12) py=SCREEN_H-12;

    /* Engine trail */
    if(g_frame%2==0){
        spawn_particle(px,py+12,
            pvx*0.3f+randf_range(-0.5f,0.5f),
            pvy*0.3f+randf_range(0.5f,2.0f),
            COL_SHIP,8,2.0f);
    }

    /* Shooting */
    if(p_shoot_cd>0) p_shoot_cd--;
    if((held&KEY_A)||(held&KEY_ZR)) fire_player_bullets();

    /* Invulnerability */
    if(p_invuln>0) p_invuln--;

    /* Combo timer */
    if(g_combo_timer>0){
        g_combo_timer--;
        if(g_combo_timer==0) g_combo=0;
    }

    /* ── Bullets ── */
    for(int i=0;i<MAX_BULLETS;i++){
        Bullet *b=&bullets[i];
        if(!b->active) continue;
        b->x+=b->vx; b->y+=b->vy;
        if(b->x<-20||b->x>SCREEN_W+20||b->y<-20||b->y>SCREEN_H+20){
            b->active=false; continue;
        }
        if(!b->enemy){
            /* Player bullet vs enemies */
            for(int j=0;j<MAX_ENEMIES;j++){
                Enemy *e=&enemies[j];
                if(!e->active) continue;
                if(dist2(b->x,b->y,e->x,e->y)<(BULLET_RADIUS+ENEMY_RADIUS)*(BULLET_RADIUS+ENEMY_RADIUS)){
                    b->active=false;
                    e->hp--;
                    spawn_particle(b->x,b->y,
                        randf_range(-2,2),randf_range(-2,2),
                        COL_BULLET,8,2.5f);
                    if(e->hp<=0) kill_enemy(j);
                    break;
                }
            }
            if(!b->active) continue;
            /* Player bullet vs asteroids */
            for(int j=0;j<MAX_ASTEROIDS;j++){
                Asteroid *a=&asteroids[j];
                if(!a->active) continue;
                float r=BULLET_RADIUS+a->radius;
                if(dist2(b->x,b->y,a->x,a->y)<r*r){
                    b->active=false;
                    a->hp--;
                    spawn_particle(b->x,b->y,
                        randf_range(-1,1),randf_range(-1,1),
                        COL_ASTEROID,6,2.0f);
                    if(a->hp<=0) kill_asteroid(j);
                    break;
                }
            }
        } else {
            /* Enemy bullet vs player */
            if(dist2(b->x,b->y,px,py)<(BULLET_RADIUS+SHIP_RADIUS)*(BULLET_RADIUS+SHIP_RADIUS)){
                b->active=false;
                hit_player();
                shake_timer=10;
            }
        }
    }

    /* ── Enemies ── */
    try_spawn_enemy();
    for(int i=0;i<MAX_ENEMIES;i++){
        Enemy *e=&enemies[i];
        if(!e->active) continue;
        e->frame++;

        /* Steering – slowly orbit player */
        float dx2=px-e->x, dy2=py-e->y;
        float d=sqrtf(dx2*dx2+dy2*dy2);
        if(d>1.0f){
            float desired_spd=1.0f+g_wave*0.05f;
            if(e->type==2) desired_spd*=1.4f;
            float tx=(dx2/d)*desired_spd;
            float ty=(dy2/d)*desired_spd;
            /* Orbit component */
            float perp_x=-dy2/d, perp_y=dx2/d;
            float orbit=0.4f;
            if(e->type==1) orbit=0.6f;
            tx+=perp_x*orbit; ty+=perp_y*orbit;
            e->vx=lerpf(e->vx,tx,0.03f);
            e->vy=lerpf(e->vy,ty,0.03f);
        }
        e->x+=e->vx; e->y+=e->vy;
        e->angle=atan2f(e->vy,e->vx);

        /* Shoot */
        e->shoot_cd--;
        if(e->shoot_cd<=0){
            int cd=ENEMY_SHOOT_CD-(g_wave*3);
            if(cd<25) cd=25;
            e->shoot_cd=cd+(int)(randf()*30);
            fire_enemy_bullet(e->x,e->y);
        }

        /* Collide with player */
        if(dist2(e->x,e->y,px,py)<(ENEMY_RADIUS+SHIP_RADIUS)*(ENEMY_RADIUS+SHIP_RADIUS)){
            hit_player();
            shake_timer=12;
        }

        /* Off-screen cull (only if very far) */
        if(e->x<-80||e->x>SCREEN_W+80||e->y<-80||e->y>SCREEN_H+80)
            e->active=false;
    }

    /* ── Asteroids ── */
    for(int i=0;i<MAX_ASTEROIDS;i++){
        Asteroid *a=&asteroids[i];
        if(!a->active) continue;
        a->x+=a->vx; a->y+=a->vy;
        a->angle+=a->spin;
        /* Wrap */
        if(a->x<-30) a->x=SCREEN_W+25;
        if(a->x>SCREEN_W+30) a->x=-25;
        if(a->y<-30) a->y=SCREEN_H+25;
        if(a->y>SCREEN_H+30) a->y=-25;
        /* Collide player */
        float r=a->radius+SHIP_RADIUS;
        if(dist2(a->x,a->y,px,py)<r*r){
            hit_player();
            shake_timer=8;
            a->hp--;
            if(a->hp<=0) kill_asteroid(i);
        }
    }

    /* ── Orbs ── */
    for(int i=0;i<MAX_ORBS;i++){
        Orb *o=&orbs[i];
        if(!o->active) continue;
        o->pulse+=0.1f;
        o->x+=o->vx; o->y+=o->vy;
        o->vx*=0.99f; o->vy*=0.99f;
        /* Attract to player when close */
        float dx3=px-o->x, dy3=py-o->y;
        float d3=sqrtf(dx3*dx3+dy3*dy3);
        if(d3<60.0f && d3>0.1f){
            o->vx+=dx3/d3*0.4f;
            o->vy+=dy3/d3*0.4f;
        }
        /* Collect */
        if(dist2(o->x,o->y,px,py)<(ORB_RADIUS+SHIP_RADIUS-2)*(ORB_RADIUS+SHIP_RADIUS-2)){
            o->active=false;
            if(o->type==0){
                if(p_shields<MAX_SHIELDS){
                    p_shields++;
                    for(int k=0;k<15;k++){
                        float ang2=randf_range(0,(float)M_PI*2);
                        spawn_particle(px,py,cosf(ang2)*3,sinf(ang2)*3,
                            COL_ORB_SHIELD,20,3.0f);
                    }
                }
            } else {
                if(g_power_level<3){
                    g_power_level++;
                    for(int k=0;k<15;k++){
                        float ang2=randf_range(0,(float)M_PI*2);
                        spawn_particle(px,py,cosf(ang2)*3,sinf(ang2)*3,
                            COL_ORB_POWER,20,3.0f);
                    }
                } else {
                    g_score+=50;
                }
            }
        }
        if(o->x<-20||o->x>SCREEN_W+20||o->y<-20||o->y>SCREEN_H+20)
            o->active=false;
    }

    /* ── Particles ── */
    for(int i=0;i<MAX_PARTICLES;i++){
        Particle *p2=&particles[i];
        if(!p2->active) continue;
        p2->x+=p2->vx; p2->y+=p2->vy;
        p2->vx*=0.93f; p2->vy*=0.93f;
        p2->life--;
        if(p2->life<=0) p2->active=false;
    }

    /* ── Explosions ── */
    for(int i=0;i<MAX_EXPLOSIONS;i++){
        Explosion *e2=&explosions[i];
        if(!e2->active) continue;
        e2->life--;
        e2->radius=e2->max_radius*(float)e2->life/(float)e2->max_life;
        if(e2->life<=0) e2->active=false;
    }

    /* ── Stars parallax scroll ── */
    for(int i=0;i<MAX_STARS;i++){
        stars[i].twinkle+=0.05f;
        stars[i].y+=stars[i].speed*0.2f;
        if(stars[i].y>SCREEN_H+2) stars[i].y=-2;
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Draw helpers                                                */
/* ─────────────────────────────────────────────────────────── */

static void draw_circle_outline(float cx,float cy,float r,u32 col,int segs){
    float prev_x=cx+r, prev_y=cy;
    float step=2.0f*(float)M_PI/(float)segs;
    for(int i=1;i<=segs;i++){
        float a=step*i;
        float nx=cx+cosf(a)*r, ny=cy+sinf(a)*r;
        C2D_DrawLine(prev_x,prev_y,col,nx,ny,col,1.5f,0);
        prev_x=nx; prev_y=ny;
    }
}

/* Draw a simple ship polygon */
static void draw_ship(float cx,float cy,float tilt,u32 col,float scale){
    /* Points relative to center, rotated by tilt */
    /* Ship: pointed up, wings out */
    float pts[8][2]={
        { 0,-14},  /* nose */
        { 7,  4},  /* right wing tip */
        { 4,  8},  /* right inner */
        { 0,  5},  /* tail center */
        {-4,  8},  /* left inner */
        {-7,  4},  /* left wing tip */
        { 0,-14},  /* back to nose */
        { 0,  0}   /* unused */
    };
    float ct=cosf(tilt), st=sinf(tilt);
    float rx[7],ry[7];
    for(int i=0;i<7;i++){
        float px2=pts[i][0]*scale, py2=pts[i][1]*scale;
        rx[i]=cx+px2*ct-py2*st;
        ry[i]=cy+px2*st+py2*ct;
    }
    for(int i=0;i<6;i++){
        C2D_DrawLine(rx[i],ry[i],col,rx[i+1],ry[i+1],col,1.8f,0);
    }
    /* Core glow dot */
    C2D_DrawCircleSolid(cx,cy,0,3.0f*scale,COL_SHIP_CORE);
}

/* Draw enemy polygon based on type */
static void draw_enemy(Enemy *e){
    u32 col=(e->type==0)?COL_ENEMY_A:(e->type==1)?COL_ENEMY_B:COL_ENEMY_C;
    float sz=8.0f+(e->type*4.0f);
    float pulse=sinf((float)e->frame*0.15f)*0.15f+1.0f;
    sz*=pulse;

    if(e->type==0){
        /* Scout: diamond */
        C2D_DrawLine(e->x,    e->y-sz,col, e->x+sz,e->y,   col,1.5f,0);
        C2D_DrawLine(e->x+sz, e->y,   col, e->x,   e->y+sz,col,1.5f,0);
        C2D_DrawLine(e->x,    e->y+sz,col, e->x-sz,e->y,   col,1.5f,0);
        C2D_DrawLine(e->x-sz, e->y,   col, e->x,   e->y-sz,col,1.5f,0);
        C2D_DrawCircleSolid(e->x,e->y,0,3.0f,col);
    } else if(e->type==1){
        /* Fighter: cross/X */
        float a=e->angle+e->frame*0.03f;
        for(int k=0;k<4;k++){
            float ang=a+(float)k*(float)M_PI*0.5f;
            C2D_DrawLine(e->x,e->y,col,
                e->x+cosf(ang)*sz,e->y+sinf(ang)*sz,col,2.0f,0);
        }
        C2D_DrawCircleSolid(e->x,e->y,0,5.0f,col);
    } else {
        /* Boss: hexagon */
        float prev_bx=e->x+sz, prev_by=e->y;
        float base_ang=e->frame*0.02f;
        for(int k=1;k<=6;k++){
            float ang=base_ang+(float)k*(float)M_PI/3.0f;
            float nbx=e->x+cosf(ang)*sz, nby=e->y+sinf(ang)*sz;
            C2D_DrawLine(prev_bx,prev_by,col,nbx,nby,col,2.5f,0);
            prev_bx=nbx; prev_by=nby;
        }
        /* HP bar */
        float bw=24.0f;
        C2D_DrawRectSolid(e->x-bw/2,e->y+sz+4,0,bw,4,COL_DARK);
        C2D_DrawRectSolid(e->x-bw/2,e->y+sz+4,0,bw*(e->hp/5.0f),4,col);
        C2D_DrawCircleSolid(e->x,e->y,0,5.0f,col);
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Render top screen                                           */
/* ─────────────────────────────────────────────────────────── */

static void render_top(void){
    /* Shake offset */
    float sx=0,sy=0;
    if(shake_timer>0){
        sx=randf_range(-3,3);
        sy=randf_range(-3,3);
    }

    C2D_DrawRectSolid(0,0,0,SCREEN_W,SCREEN_H,COL_BG);

    /* Stars */
    for(int i=0;i<MAX_STARS;i++){
        float alpha=0.4f+sinf(stars[i].twinkle)*0.3f;
        float s=stars[i].size;
        u32 col=C2D_Color32(255,255,255,(u8)(alpha*255));
        C2D_DrawCircleSolid(stars[i].x+sx*stars[i].speed*0.3f,
                            stars[i].y+sy*stars[i].speed*0.3f,
                            0,s,col);
    }

    /* Grid horizon effect (subtle) */
    for(int row=0;row<8;row++){
        float yy=SCREEN_H-(float)row*30.0f;
        float alpha=0.04f+(float)row*0.01f;
        u32 gc=C2D_Color32(0,200,255,(u8)(alpha*255));
        C2D_DrawLine(0,yy+sy,gc,SCREEN_W,yy+sy,gc,0.5f,0);
    }

    /* Explosions */
    for(int i=0;i<MAX_EXPLOSIONS;i++){
        Explosion *e=&explosions[i];
        if(!e->active) continue;
        float t=(float)e->life/(float)e->max_life;
        u8 a=(u8)(t*180);
        u32 col_fade=C2D_Color32(
            (e->color>>0)&0xFF,
            (e->color>>8)&0xFF,
            (e->color>>16)&0xFF, a);
        draw_circle_outline(e->x+sx,e->y+sy,e->radius,col_fade,16);
        /* Inner flash */
        if(t>0.6f){
            u8 a2=(u8)((t-0.6f)/0.4f*100);
            u32 flash=C2D_Color32(255,255,255,a2);
            C2D_DrawCircleSolid(e->x+sx,e->y+sy,0,e->radius*0.4f,flash);
        }
    }

    /* Orbs */
    for(int i=0;i<MAX_ORBS;i++){
        Orb *o=&orbs[i];
        if(!o->active) continue;
        u32 oc=(o->type==0)?COL_ORB_SHIELD:COL_ORB_POWER;
        float r=ORB_RADIUS+sinf(o->pulse)*2.0f;
        draw_circle_outline(o->x+sx,o->y+sy,r,oc,12);
        u8 ia=(u8)(100+sinf(o->pulse)*80);
        u32 fill=C2D_Color32(
            (oc>>0)&0xFF,(oc>>8)&0xFF,(oc>>16)&0xFF,ia);
        C2D_DrawCircleSolid(o->x+sx,o->y+sy,0,r*0.5f,fill);
    }

    /* Asteroids */
    for(int i=0;i<MAX_ASTEROIDS;i++){
        Asteroid *a=&asteroids[i];
        if(!a->active) continue;
        float r=a->radius;
        /* Draw jagged polygon */
        int segs=7;
        float prev_ax=a->x+sx+cosf(a->angle)*r;
        float prev_ay=a->y+sy+sinf(a->angle)*r;
        for(int k=1;k<=segs;k++){
            float ang=a->angle+(float)k*(float)M_PI*2.0f/(float)segs;
            float jagg=0.75f+randf()*0.0f; /* stable shape: use fixed jag per asteroid */
            float kr=r*(0.7f+0.3f*((k*7+i*3)%5)/4.0f);
            float nax=a->x+sx+cosf(ang)*kr;
            float nay=a->y+sy+sinf(ang)*kr;
            C2D_DrawLine(prev_ax,prev_ay,COL_ASTEROID,nax,nay,COL_ASTEROID,1.5f,0);
            (void)jagg;
            prev_ax=nax; prev_ay=nay;
        }
    }

    /* Particles */
    for(int i=0;i<MAX_PARTICLES;i++){
        Particle *p2=&particles[i];
        if(!p2->active) continue;
        float t=(float)p2->life/(float)p2->max_life;
        u8 a=(u8)(t*255);
        u32 col=C2D_Color32(
            (p2->color>>0)&0xFF,
            (p2->color>>8)&0xFF,
            (p2->color>>16)&0xFF, a);
        C2D_DrawCircleSolid(p2->x+sx,p2->y+sy,0,p2->size*t,col);
    }

    /* Enemies */
    for(int i=0;i<MAX_ENEMIES;i++){
        if(enemies[i].active){
            /* Translate by shake */
            enemies[i].x+=sx; enemies[i].y+=sy;
            draw_enemy(&enemies[i]);
            enemies[i].x-=sx; enemies[i].y-=sy;
        }
    }

    /* Bullets */
    for(int i=0;i<MAX_BULLETS;i++){
        Bullet *b=&bullets[i];
        if(!b->active) continue;
        u32 bc=b->enemy?COL_E_BULLET:COL_BULLET;
        float bx=b->x+sx, by=b->y+sy;
        C2D_DrawCircleSolid(bx,by,0,BULLET_RADIUS*0.8f,bc);
        /* Glow trail */
        u32 trail=C2D_Color32(
            (bc>>0)&0xFF,(bc>>8)&0xFF,(bc>>16)&0xFF,80);
        C2D_DrawLine(bx,by,trail,
                     bx-b->vx*3,by-b->vy*3,
                     C2D_Color32(0,0,0,0),1.5f,0);
    }

    /* Player ship */
    if(p_invuln==0 || (p_invuln/4)%2==0){
        float spx=px+sx, spy=py+sy;
        /* Shield circle */
        if(p_shields>0){
            float shield_alpha=0.15f+sinf((float)g_frame*0.1f)*0.05f;
            u32 sa=C2D_Color32(68,255,136,(u8)(shield_alpha*255));
            draw_circle_outline(spx,spy,SHIP_RADIUS+4,sa,20);
        }
        draw_ship(spx,spy,p_angle,COL_SHIP,1.0f);
        /* Boost glow */
        if(p_boosting){
            u32 bg2=C2D_Color32(0,200,255,120);
            draw_circle_outline(spx,spy,SHIP_RADIUS+8,bg2,12);
        }
    }
}

/* ─────────────────────────────────────────────────────────── */
/*  Render bottom screen (HUD)                                  */
/* ─────────────────────────────────────────────────────────── */

static char hud_buf[64];

static void render_bottom_hud(void){
    C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,COL_DARK);

    /* Title bar */
    C2D_DrawRectSolid(0,0,0,BOT_W,22,C2D_Color32(0,180,200,40));
    C2D_DrawLine(0,22,C2D_Color32(0,255,200,120),BOT_W,22,C2D_Color32(0,255,200,120),1.0f,0);

    /* Wave info */
    C2D_DrawRectSolid(5,28,0,BOT_W-10,1,C2D_Color32(0,200,255,60));

    /* Shields */
    for(int i=0;i<MAX_SHIELDS;i++){
        u32 col=(i<p_shields)?COL_SHIELD_ON:COL_SHIELD_OFF;
        draw_circle_outline(20+i*22.0f,50,8,col,16);
        if(i<p_shields) C2D_DrawCircleSolid(20+i*22.0f,50,0,4,col);
    }

    /* Boost bar */
    float bw=(BOT_W-20)*p_boost/BOOST_MAX;
    C2D_DrawRectSolid(10,70,0,BOT_W-20,8,C2D_Color32(0,80,120,200));
    C2D_DrawRectSolid(10,70,0,bw,8,COL_BOOST_BAR);
    C2D_DrawRectSolid(10,70,0,BOT_W-20,1,C2D_Color32(0,200,255,100));
    C2D_DrawRectSolid(10,77,0,BOT_W-20,1,C2D_Color32(0,200,255,100));

    /* Power level pips */
    for(int i=0;i<3;i++){
        u32 pc=(i<g_power_level)?COL_ORB_POWER:C2D_Color32(60,60,30,200);
        C2D_DrawCircleSolid(10+i*18.0f,90,0,7,pc);
    }

    /* Combo display */
    if(g_combo>1){
        float fade=(float)g_combo_timer/120.0f;
        u8 ca=(u8)(fade*255);
        u32 cc=C2D_Color32(255,136,0,ca);
        float cw=80.0f*(fade);
        C2D_DrawRectSolid(BOT_W/2-40,85,0,cw,6,cc);
    }

    /* Mini-map */
    float mm_x=BOT_W-75.0f, mm_y=30.0f;
    float mm_w=65.0f, mm_h=48.0f;
    C2D_DrawRectSolid(mm_x,mm_y,0,mm_w,mm_h,C2D_Color32(0,0,20,180));
    C2D_DrawLine(mm_x,mm_y,C2D_Color32(0,200,255,80),
                 mm_x+mm_w,mm_y,C2D_Color32(0,200,255,80),1,0);
    C2D_DrawLine(mm_x,mm_y+mm_h,C2D_Color32(0,200,255,80),
                 mm_x+mm_w,mm_y+mm_h,C2D_Color32(0,200,255,80),1,0);
    C2D_DrawLine(mm_x,mm_y,C2D_Color32(0,200,255,80),
                 mm_x,mm_y+mm_h,C2D_Color32(0,200,255,80),1,0);
    C2D_DrawLine(mm_x+mm_w,mm_y,C2D_Color32(0,200,255,80),
                 mm_x+mm_w,mm_y+mm_h,C2D_Color32(0,200,255,80),1,0);

    /* Player dot on minimap */
    float mpx=mm_x+px/SCREEN_W*mm_w;
    float mpy=mm_y+py/SCREEN_H*mm_h;
    C2D_DrawCircleSolid(mpx,mpy,0,2.5f,COL_SHIP);

    /* Enemies on minimap */
    for(int i=0;i<MAX_ENEMIES;i++){
        if(!enemies[i].active) continue;
        float ex2=enemies[i].x/SCREEN_W*mm_w+mm_x;
        float ey2=enemies[i].y/SCREEN_H*mm_h+mm_y;
        if(ex2>=mm_x && ex2<=mm_x+mm_w && ey2>=mm_y && ey2<=mm_y+mm_h){
            u32 ec=(enemies[i].type==2)?COL_ENEMY_C:COL_ENEMY_A;
            C2D_DrawCircleSolid(ex2,ey2,0,2.0f,ec);
        }
    }

    /* Asteroids on minimap */
    for(int i=0;i<MAX_ASTEROIDS;i++){
        if(!asteroids[i].active) continue;
        float ax2=asteroids[i].x/SCREEN_W*mm_w+mm_x;
        float ay2=asteroids[i].y/SCREEN_H*mm_h+mm_y;
        if(ax2>=mm_x && ax2<=mm_x+mm_w && ay2>=mm_y && ay2<=mm_y+mm_h){
            C2D_DrawCircleSolid(ax2,ay2,0,1.5f,COL_ASTEROID);
        }
    }

    /* Controls reminder */
    C2D_DrawLine(0,BOT_H-28,C2D_Color32(0,200,255,40),
                 BOT_W,BOT_H-28,C2D_Color32(0,200,255,40),1,0);
}

/* ─────────────────────────────────────────────────────────── */
/*  Text via C2D_TextBuf                                        */
/* ─────────────────────────────────────────────────────────── */

static C2D_TextBuf g_text_buf;
static C2D_Text    g_text_tmp;

static void draw_text(float x,float y,float sz,u32 col,const char *str){
    if(!g_text_buf || !str) return;
    C2D_TextBufClear(g_text_buf);
    if(!C2D_TextFontParse(&g_text_tmp,NULL,g_text_buf,str)) return;
    C2D_TextOptimize(&g_text_tmp);
    C2D_DrawText(&g_text_tmp,C2D_WithColor,x,y,0,sz,sz,col);
}

/* ─────────────────────────────────────────────────────────── */
/*  Screens                                                     */
/* ─────────────────────────────────────────────────────────── */

static void render_title(void){
    C2D_DrawRectSolid(0,0,0,SCREEN_W,SCREEN_H,COL_BG);
    for(int i=0;i<MAX_STARS;i++){
        float alpha=0.3f+sinf(stars[i].twinkle)*0.3f;
        u32 col=C2D_Color32(255,255,255,(u8)(alpha*255));
        C2D_DrawCircleSolid(stars[i].x,stars[i].y,0,stars[i].size,col);
    }
    /* Animated title ships */
    float t=(float)g_frame*0.02f;
    draw_ship(SCREEN_W/2.0f+sinf(t)*60,80,sinf(t)*0.4f,COL_SHIP,1.4f);
    draw_ship(SCREEN_W/2.0f-sinf(t*0.7f)*80,115,sinf(t*0.7f+1)*0.3f,
              C2D_Color32(100,255,200,180),1.1f);

    /* Title glow */
    float glow=sinf((float)g_frame*0.08f)*0.2f+0.8f;
    u32 title_col=C2D_Color32(0,(u8)(200*glow),(u8)(255*glow),255);
    draw_text(88,25,0.85f,title_col,"COSMIC DRIFT");
    draw_text(90,27,0.85f,C2D_Color32(0,255,200,60),"COSMIC DRIFT");

    draw_text(115,150,0.5f,COL_WHITE,"Press A or Start to Play");
    draw_text(123,165,0.45f,C2D_Color32(150,150,255,200),"Select to Quit");

    /* Controls blurb */
    draw_text(60,185,0.38f,C2D_Color32(120,200,255,180),
              "Circle Pad: Move   A/ZR: Shoot   B: Boost");
    draw_text(100,198,0.38f,C2D_Color32(120,200,255,180),
              "Survive waves. Collect orbs.");

    if(g_hiscore>0){
        snprintf(hud_buf,sizeof(hud_buf),"HI-SCORE: %d",g_hiscore);
        draw_text(145,210,0.42f,COL_YELLOW,hud_buf);
    }
}

static void render_gameover(void){
    C2D_DrawRectSolid(0,0,0,SCREEN_W,SCREEN_H,C2D_Color32(10,0,0,255));
    for(int i=0;i<MAX_STARS;i++){
        float alpha=0.2f+sinf(stars[i].twinkle)*0.2f;
        u32 col=C2D_Color32(255,100,100,(u8)(alpha*255));
        C2D_DrawCircleSolid(stars[i].x,stars[i].y,0,stars[i].size,col);
    }
    draw_text(130,30,0.75f,COL_RED,"GAME OVER");
    snprintf(hud_buf,sizeof(hud_buf),"SCORE: %d",g_score);
    draw_text(160,80,0.55f,COL_WHITE,hud_buf);
    snprintf(hud_buf,sizeof(hud_buf),"WAVE: %d",g_wave);
    draw_text(168,100,0.5f,C2D_Color32(180,180,255,220),hud_buf);
    snprintf(hud_buf,sizeof(hud_buf),"KILLS: %d",g_kills);
    draw_text(165,118,0.5f,C2D_Color32(180,180,255,220),hud_buf);
    if(g_new_hi){
        float glow=sinf((float)g_frame*0.15f)*0.3f+0.7f;
        u32 yc=C2D_Color32(255,(u8)(220*glow),0,255);
        draw_text(130,145,0.55f,yc,"NEW HIGH SCORE!");
    }
    snprintf(hud_buf,sizeof(hud_buf),"HI-SCORE: %d",g_hiscore);
    draw_text(148,170,0.48f,COL_YELLOW,hud_buf);
    draw_text(120,200,0.45f,COL_WHITE,"A/Start: Play Again   B: Title");
}

static void render_paused(void){
    /* Dim overlay */
    C2D_DrawRectSolid(0,0,0,SCREEN_W,SCREEN_H,C2D_Color32(0,0,20,160));
    draw_text(162,90,0.65f,COL_WHITE,"PAUSED");
    draw_text(128,130,0.45f,C2D_Color32(180,220,255,220),"Start: Resume");
    draw_text(136,150,0.45f,C2D_Color32(180,220,255,220),"B: Quit to Title");
}

/* Bottom HUD text overlay */
static void render_bottom_text(void){
    /* Score */
    snprintf(hud_buf,sizeof(hud_buf),"SCORE %06d",g_score);
    draw_text(5,4,0.45f,C2D_Color32(0,255,200,255),hud_buf);

    /* Wave */
    snprintf(hud_buf,sizeof(hud_buf),"WAVE %d",g_wave);
    draw_text(220,4,0.45f,C2D_Color32(0,200,255,255),hud_buf);

    /* Shield label */
    draw_text(8,38,0.38f,C2D_Color32(100,200,150,200),"SHIELDS");

    /* Boost label */
    draw_text(8,60,0.38f,C2D_Color32(0,180,255,200),"BOOST");

    /* Power label */
    draw_text(60,84,0.38f,C2D_Color32(220,200,0,200),"PWR");

    /* Combo */
    if(g_combo>1){
        snprintf(hud_buf,sizeof(hud_buf),"COMBO x%d",g_combo);
        float fade=(float)g_combo_timer/120.0f;
        u8 ca=(u8)(fade*255);
        draw_text(BOT_W/2-25,100,0.48f,C2D_Color32(255,136,0,ca),hud_buf);
    }

    /* Controls reminder */
    draw_text(10,BOT_H-24,0.35f,C2D_Color32(80,150,200,180),
              "A/ZR:Shoot  B:Boost  Start:Pause");
}

/* ─────────────────────────────────────────────────────────── */
/*  Main                                                        */
/* ─────────────────────────────────────────────────────────── */

int main(void){
    srand((unsigned)time(NULL));

    gfxInitDefault();
    if(!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)){
        gfxExit();
        return 1;
    }
    if(!C2D_Init(C2D_DEFAULT_MAX_OBJECTS)){
        C3D_Fini();
        gfxExit();
        return 1;
    }
    C2D_Prepare();

    /* Create render targets */
    C3D_RenderTarget *top    = C2D_CreateScreenTarget(GFX_TOP,    GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    if(!top || !bottom){
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 1;
    }

    /* Text buffer */
    g_text_buf = C2D_TextBufNew(512);
    if(!g_text_buf){
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 1;
    }

    init_stars();

    /* Main loop */
    while(aptMainLoop()){
        hidScanInput();
        u32 held = hidKeysHeld();
        u32 down = hidKeysDown();

        circlePosition cp;
        hidCircleRead(&cp);

        /* ── State machine ── */
        switch(g_state){
            case STATE_TITLE:
                g_frame++;
                for(int i=0;i<MAX_STARS;i++) stars[i].twinkle+=0.05f;
                if((down&KEY_A)||(down&KEY_START)){
                    reset_game();
                    g_state=STATE_PLAY;
                }
                if(down&KEY_SELECT) goto done;
                break;

            case STATE_PLAY:
                update_game(held,down,cp);
                if(down&KEY_START) g_state=STATE_PAUSED;
                break;

            case STATE_PAUSED:
                if(down&KEY_START) g_state=STATE_PLAY;
                if(down&KEY_B)     g_state=STATE_TITLE;
                break;

            case STATE_GAMEOVER:
                g_frame++;
                for(int i=0;i<MAX_STARS;i++) stars[i].twinkle+=0.04f;
                if((down&KEY_A)||(down&KEY_START)){
                    reset_game(); g_state=STATE_PLAY;
                }
                if(down&KEY_B) g_state=STATE_TITLE;
                break;

            default: break;
        }

        /* ── Render ── */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        /* Top screen */
        C2D_TargetClear(top, COL_BG);
        C2D_SceneBegin(top);

        switch(g_state){
            case STATE_TITLE:    render_title();   break;
            case STATE_GAMEOVER: render_gameover(); break;
            case STATE_PLAY:
            case STATE_PAUSED:
                render_top();
                if(g_state==STATE_PAUSED) render_paused();
                break;
            default: break;
        }

        /* Bottom screen */
        C2D_TargetClear(bottom, COL_DARK);
        C2D_SceneBegin(bottom);

        if(g_state==STATE_PLAY || g_state==STATE_PAUSED){
            render_bottom_hud();
            render_bottom_text();
        } else if(g_state==STATE_TITLE){
            C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,COL_BG);
            draw_text(60,90,0.48f,C2D_Color32(0,200,255,200),
                      "SURVIVE ENDLESS WAVES");
            draw_text(50,115,0.42f,C2D_Color32(150,200,255,180),
                      "Collect GREEN orbs = shields");
            draw_text(50,133,0.42f,C2D_Color32(255,230,100,180),
                      "Collect YELLOW orbs = power up");
            draw_text(50,151,0.42f,C2D_Color32(100,200,255,180),
                      "Hold B to boost through enemies");
            draw_text(30,185,0.38f,C2D_Color32(80,120,180,200),
                      "Built with devkitARM + libctru + citro2d");
        } else if(g_state==STATE_GAMEOVER){
            C2D_DrawRectSolid(0,0,0,BOT_W,BOT_H,C2D_Color32(10,0,0,255));
            snprintf(hud_buf,sizeof(hud_buf),"Final Score: %d",g_score);
            draw_text(90,80,0.52f,COL_WHITE,hud_buf);
            snprintf(hud_buf,sizeof(hud_buf),"Best:        %d",g_hiscore);
            draw_text(90,105,0.52f,COL_YELLOW,hud_buf);
            draw_text(80,160,0.42f,COL_RED,"Thanks for playing COSMIC DRIFT");
        }

        C3D_FrameEnd(0);
    }

done:
    C2D_TextBufDelete(g_text_buf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
