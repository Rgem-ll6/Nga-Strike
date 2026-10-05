#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#define SCREEN_WIDTH         1280
#define SCREEN_HEIGHT        720
#define GROUND_SIZE          200.0f
#define MAP_LIMIT            48.0f
#define MOVE_SPEED           8.0f
#define MOUSE_SENS           0.003f
#define PLAYER_EYE_HEIGHT    2.0f
#define PLAYER_RADIUS        0.4f
#define PLAYER_MAX_HEALTH    100.0f
#define GRAVITY             -25.0f
#define JUMP_SPEED           9.0f
#define MUZZLE_FLASH_TIME    0.05f
#define ENEMY_MAX_HEALTH     100.0f
#define ENEMY_SPEED          2.8f
#define ENEMY_SHOOT_RANGE    18.0f
#define ENEMY_SHOOT_CD       1.35f
#define ENEMY_SHOOT_DMG      4.0f
#define ENEMY_AIM_TIME       0.6f
#define ENEMY_RESPAWN        4.0f
#define HITMARKER_TIME       0.32f
#define WEAPON_COUNT         3

#define MAX_PICKUPS           8
#define MAX_DAMAGE_INDICATORS 8
#define MAX_DAMAGE_NUMBERS    16
#define PICKUP_RESPAWN_TIME   15.0f
#define PICKUP_HEALTH_AMOUNT  35.0f
#define PICKUP_AMMO_AMOUNT    45
#define PICKUP_RADIUS         1.2f
#define DAMAGE_INDICATOR_TIME 1.1f
#define DAMAGE_NUMBER_TIME    0.9f

typedef struct {
    Vector3 position;
    Vector3 velocity;
    bool    grounded;
    float   health;
} Player;

typedef struct {
    Vector3 center;
    Vector3 halfExtents;
    Color   color;
} Box;

typedef struct {
    Vector3 position;
    Vector3 spawnPoint;
    float   health;
    float   hurtTimer;
    float   shootTimer;
    float   muzzleFlash;
    float   aimTimer;
    float   respawnTimer;
    bool    alive;
} Enemy;

typedef struct {
    const char* name;
    int   magSize;
    int   ammoInMag;
    int   ammoReserve;
    int   reserveMax;
    float fireRate;
    float reloadTime;
    float bodyDamage;
    float headDamage;
    float recoilPitch;
    float recoilYaw;
    float recoilBack;
    float adsFov;
    float adsSens;
    bool  autoFire;
    bool  hasScope;
} Weapon;

typedef enum { PICKUP_AMMO, PICKUP_HEALTH } PickupType;

typedef struct {
    Vector3    position;
    PickupType type;
    bool       active;
    float      respawnTimer;
    float      bobTimer;
} Pickup;

typedef struct {
    Vector3 attackerPos;
    float   timer;
} DamageIndicator;

typedef struct {
    Vector3 position;
    float   damage;
    float   timer;
    int     type;
} DamageNumber;

static float RandomFloat(void) {
    return (float)GetRandomValue(0, 1000000) / 1000000.0f;
}

static bool AABBvsAABB(Vector3 aPos, Vector3 aHalf, Vector3 bPos, Vector3 bHalf) {
    return (fabsf(aPos.x - bPos.x) < aHalf.x + bHalf.x) &&
           (fabsf(aPos.y - bPos.y) < aHalf.y + bHalf.y) &&
           (fabsf(aPos.z - bPos.z) < aHalf.z + bHalf.z);
}

static Vector2 WorldToScreen(Vector3 worldPos, Camera3D cam, bool* visible) {
    Vector3 forward = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right   = Vector3Normalize(Vector3CrossProduct(forward, cam.up));
    Vector3 up      = Vector3CrossProduct(right, forward);

    Vector3 toPoint = Vector3Subtract(worldPos, cam.position);

    float z = Vector3DotProduct(toPoint, forward);
    if (z <= 0.05f) {
        *visible = false;
        return (Vector2){ 0, 0 };
    }

    float x = Vector3DotProduct(toPoint, right);
    float y = Vector3DotProduct(toPoint, up);

    float fovRad = cam.fovy * DEG2RAD;
    float f = ((float)GetScreenHeight()) / (2.0f * tanf(fovRad * 0.5f));

    *visible = true;
    return (Vector2){
        (float)GetScreenWidth()  * 0.5f + (x / z) * f,
        (float)GetScreenHeight() * 0.5f - (y / z) * f
    };
}

static void SpawnDamageIndicator(DamageIndicator* arr, int count, Vector3 attacker) {
    int slot = -1;
    float oldest = -1.0f;
    for (int i = 0; i < count; i++) {
        if (arr[i].timer <= 0.0f) { slot = i; break; }
        if (arr[i].timer > oldest) { oldest = arr[i].timer; slot = i; }
    }
    if (slot < 0) slot = 0;
    arr[slot].attackerPos = attacker;
    arr[slot].timer = DAMAGE_INDICATOR_TIME;
}

static void SpawnDamageNumber(DamageNumber* arr, int count,
                              Vector3 worldPos, float damage, int type) {
    int slot = -1;
    float oldest = -1.0f;
    for (int i = 0; i < count; i++) {
        if (arr[i].timer <= 0.0f) { slot = i; break; }
        if (arr[i].timer > oldest) { oldest = arr[i].timer; slot = i; }
    }
    if (slot < 0) slot = 0;
    arr[slot].position = worldPos;
    arr[slot].damage   = damage;
    arr[slot].timer    = DAMAGE_NUMBER_TIME;
    arr[slot].type     = type;
}

static void DrawPickups(Pickup* pickups, int count, float time) {
    for (int i = 0; i < count; i++) {
        if (!pickups[i].active) continue;

        float bob = sinf(pickups[i].bobTimer * 2.6f) * 0.15f;
        Vector3 pos = { pickups[i].position.x,
                        pickups[i].position.y + bob,
                        pickups[i].position.z };

        rlPushMatrix();
        rlTranslatef(pos.x, pos.y, pos.z);
        rlRotatef(pickups[i].bobTimer * 45.0f, 0, 1, 0);

        if (pickups[i].type == PICKUP_AMMO) {
            Color body  = (Color){ 200, 165,  45, 255 };
            Color dark  = (Color){ 120,  95,  20, 255 };
            Color band  = (Color){ 240, 220, 120, 255 };
            DrawCubeV((Vector3){ 0 }, (Vector3){ 0.55f, 0.55f, 0.55f }, body);
            DrawCubeWiresV((Vector3){ 0 }, (Vector3){ 0.55f, 0.55f, 0.55f }, dark);
            DrawCubeV((Vector3){ 0, 0.28f, 0 }, (Vector3){ 0.58f, 0.03f, 0.58f }, band);
            DrawCubeV((Vector3){ 0, -0.28f, 0 }, (Vector3){ 0.58f, 0.03f, 0.58f }, band);
        } else {
            Color body  = (Color){ 210,  55,  55, 255 };
            Color dark  = (Color){ 120,  25,  25, 255 };
            Color white = (Color){ 240, 240, 240, 255 };
            DrawCubeV((Vector3){ 0 }, (Vector3){ 0.55f, 0.55f, 0.55f }, body);
            DrawCubeWiresV((Vector3){ 0 }, (Vector3){ 0.55f, 0.55f, 0.55f }, dark);
            DrawCubeV((Vector3){ 0, 0.28f, 0 }, (Vector3){ 0.36f, 0.02f, 0.12f }, white);
            DrawCubeV((Vector3){ 0, 0.28f, 0 }, (Vector3){ 0.12f, 0.02f, 0.36f }, white);
            DrawCubeV((Vector3){ 0.28f, 0, 0 }, (Vector3){ 0.02f, 0.12f, 0.36f }, white);
            DrawCubeV((Vector3){ 0.28f, 0, 0 }, (Vector3){ 0.02f, 0.36f, 0.12f }, white);
            DrawCubeV((Vector3){ -0.28f, 0, 0 }, (Vector3){ 0.02f, 0.12f, 0.36f }, white);
            DrawCubeV((Vector3){ -0.28f, 0, 0 }, (Vector3){ 0.02f, 0.36f, 0.12f }, white);
        }

        rlPopMatrix();
    }
}

static void DrawDamageIndicators(DamageIndicator* inds, int count,
                                 Player* player, float yaw) {
    if (count <= 0) return;

    float cx = GetScreenWidth()  * 0.5f;
    float cy = GetScreenHeight() * 0.5f;
    float radius = 180.0f;
    float arcHalf = 0.30f;

    for (int i = 0; i < count; i++) {
        if (inds[i].timer <= 0.0f) continue;

        float t = inds[i].timer / DAMAGE_INDICATOR_TIME;
        unsigned char a = (unsigned char)(210 * t);
        Color col = { 235, 40, 40, a };

        Vector3 toEnemy = {
            inds[i].attackerPos.x - player->position.x,
            0.0f,
            inds[i].attackerPos.z - player->position.z
        };

        float vx = toEnemy.x * (-cosf(yaw)) + toEnemy.z * sinf(yaw);
        float vz = toEnemy.x * sinf(yaw) + toEnemy.z * cosf(yaw);
        float screenAngle = atan2f(vx, vz);

        int segments = 14;
        float startA = screenAngle - arcHalf;
        float endA   = screenAngle + arcHalf;
        Vector2 prev = { 0 };
        for (int s = 0; s <= segments; s++) {
            float ang = startA + (endA - startA) * (float)s / (float)segments;
            Vector2 p = {
                cx + sinf(ang) * radius,
                cy - cosf(ang) * radius
            };
            if (s > 0) DrawLineEx(prev, p, 9.0f, col);
            prev = p;
        }
    }
}

static void DrawDamageNumbers(DamageNumber* arr, int count, Camera3D camera) {
    for (int i = 0; i < count; i++) {
        if (arr[i].timer <= 0.0f) continue;

        float t = arr[i].timer / DAMAGE_NUMBER_TIME;
        float rise = (1.0f - t) * 0.9f;

        Vector3 worldPos = {
            arr[i].position.x,
            arr[i].position.y + 0.5f + rise,
            arr[i].position.z
        };

        bool visible = false;
        Vector2 sp = WorldToScreen(worldPos, camera, &visible);
        if (!visible) continue;
        if (sp.x < -80 || sp.x > SCREEN_WIDTH  + 80) continue;
        if (sp.y < -80 || sp.y > SCREEN_HEIGHT + 80) continue;

        unsigned char alpha = (unsigned char)(255 * (t < 0.25f ? t / 0.25f : 1.0f));

        Color col;
        int size;
        const char* txt;

        if (arr[i].type == 2) {
            col = (Color){ 255, 70, 70, alpha };
            size = 34;
            txt = TextFormat("%d  KILL", (int)arr[i].damage);
        } else if (arr[i].type == 1) {
            col = (Color){ 255, 215, 70, alpha };
            size = 28;
            txt = TextFormat("%d  HEAD", (int)arr[i].damage);
        } else {
            col = (Color){ 245, 245, 245, alpha };
            size = 22;
            txt = TextFormat("%d", (int)arr[i].damage);
        }

        int tw = MeasureText(txt, size);
        int tx = (int)sp.x - tw / 2;
        int ty = (int)sp.y;

        DrawText(txt, tx + 2, ty + 2, size, (Color){ 0, 0, 0, alpha });
        DrawText(txt, tx, ty, size, col);
    }
}

static void UpdatePickups(Pickup* pickups, int count, Player* player,
                          Weapon* weapons, int wc, Sound sfx) {
    float dt = GetFrameTime();
    for (int i = 0; i < count; i++) {
        if (!pickups[i].active) {
            pickups[i].respawnTimer -= dt;
            if (pickups[i].respawnTimer <= 0.0f) {
                pickups[i].active = true;
                pickups[i].bobTimer = 0.0f;
            }
            continue;
        }

        pickups[i].bobTimer += dt;

        float dx = player->position.x - pickups[i].position.x;
        float dz = player->position.z - pickups[i].position.z;
        float dy = (player->position.y + 1.0f) - pickups[i].position.y;
        float distSq = dx*dx + dy*dy + dz*dz;
        if (distSq > PICKUP_RADIUS * PICKUP_RADIUS) continue;

        bool picked = false;

        if (pickups[i].type == PICKUP_AMMO) {
            bool anyBelowMax = false;
            for (int k = 0; k < wc; k++) {
                if (weapons[k].ammoReserve < weapons[k].reserveMax) {
                    anyBelowMax = true;
                    break;
                }
            }
            if (anyBelowMax) {
                for (int k = 0; k < wc; k++) {
                    weapons[k].ammoReserve += PICKUP_AMMO_AMOUNT;
                    if (weapons[k].ammoReserve > weapons[k].reserveMax) {
                        weapons[k].ammoReserve = weapons[k].reserveMax;
                    }
                }
                picked = true;
            }
        } else {
            if (player->health < PLAYER_MAX_HEALTH) {
                player->health += PICKUP_HEALTH_AMOUNT;
                if (player->health > PLAYER_MAX_HEALTH) {
                    player->health = PLAYER_MAX_HEALTH;
                }
                picked = true;
            }
        }

        if (picked) {
            pickups[i].active = false;
            pickups[i].respawnTimer = PICKUP_RESPAWN_TIME;
            SetSoundPitch(sfx, 1.55f + RandomFloat() * 0.15f);
            PlaySound(sfx);
        }
    }
}

static void DrawEnemy(Enemy* e, Vector3 playerPos) {
    bool hurt = e->hurtTimer > 0.0f;
    Color skinCol  = hurt ? WHITE : (Color){ 214, 168, 128, 255 };
    Color bodyCol  = hurt ? WHITE : (Color){  52,  82, 138, 255 };
    Color pantCol  = hurt ? WHITE : (Color){  38,  42,  52, 255 };
    Color darkCol  = hurt ? WHITE : (Color){  18,  20,  26, 255 };
    Color gunCol   = hurt ? WHITE : (Color){  34,  36,  42, 255 };
    Color gunSteel = hurt ? WHITE : (Color){  90,  95, 105, 255 };
    Color hairCol  = hurt ? WHITE : (Color){  45,  32,  24, 255 };

    Vector3 toPlayer = Vector3Subtract(playerPos, e->position);
    toPlayer.y = 0.0f;
    float angle = atan2f(toPlayer.x, toPlayer.z) * RAD2DEG;

    rlPushMatrix();
    rlTranslatef(e->position.x, e->position.y, e->position.z);
    rlRotatef(angle, 0.0f, 1.0f, 0.0f);

    DrawCubeV((Vector3){ 0, 0.72f, 0 }, (Vector3){ 0.30f, 0.30f, 0.28f }, skinCol);
    DrawCubeWiresV((Vector3){ 0, 0.72f, 0 }, (Vector3){ 0.30f, 0.30f, 0.28f }, darkCol);
    DrawCubeV((Vector3){ 0, 0.90f, 0 }, (Vector3){ 0.32f, 0.06f, 0.30f }, hairCol);
    DrawCubeV((Vector3){ -0.07f, 0.74f, 0.145f }, (Vector3){ 0.04f, 0.05f, 0.01f }, darkCol);
    DrawCubeV((Vector3){  0.07f, 0.74f, 0.145f }, (Vector3){ 0.04f, 0.05f, 0.01f }, darkCol);
    DrawCubeV((Vector3){ 0, 0.54f, 0 }, (Vector3){ 0.12f, 0.08f, 0.12f }, skinCol);

    DrawCubeV((Vector3){ 0, 0.20f, 0 }, (Vector3){ 0.54f, 0.66f, 0.34f }, bodyCol);
    DrawCubeWiresV((Vector3){ 0, 0.20f, 0 }, (Vector3){ 0.54f, 0.66f, 0.34f }, darkCol);
    DrawCubeV((Vector3){ 0, -0.10f, 0 }, (Vector3){ 0.55f, 0.06f, 0.35f }, darkCol);

    DrawCubeV((Vector3){ -0.34f, 0.40f, 0 }, (Vector3){ 0.16f, 0.20f, 0.28f }, bodyCol);
    DrawCubeV((Vector3){  0.34f, 0.40f, 0 }, (Vector3){ 0.16f, 0.20f, 0.28f }, bodyCol);

    DrawCubeV((Vector3){ -0.36f, 0.10f, 0 }, (Vector3){ 0.14f, 0.45f, 0.14f }, bodyCol);
    DrawCubeV((Vector3){  0.36f, 0.10f, 0 }, (Vector3){ 0.14f, 0.45f, 0.14f }, bodyCol);

    DrawCubeV((Vector3){ -0.24f, -0.10f, 0.18f }, (Vector3){ 0.13f, 0.13f, 0.30f }, skinCol);
    DrawCubeV((Vector3){  0.24f, -0.10f, 0.18f }, (Vector3){ 0.13f, 0.13f, 0.30f }, skinCol);

    DrawCubeV((Vector3){ -0.14f, -0.10f, 0.42f }, (Vector3){ 0.12f, 0.12f, 0.12f }, skinCol);
    DrawCubeV((Vector3){  0.14f, -0.10f, 0.42f }, (Vector3){ 0.12f, 0.12f, 0.12f }, skinCol);

    DrawCubeV((Vector3){ -0.14f, -0.55f, 0 }, (Vector3){ 0.20f, 0.58f, 0.22f }, pantCol);
    DrawCubeV((Vector3){  0.14f, -0.55f, 0 }, (Vector3){ 0.20f, 0.58f, 0.22f }, pantCol);
    DrawCubeWiresV((Vector3){ -0.14f, -0.55f, 0 }, (Vector3){ 0.20f, 0.58f, 0.22f }, darkCol);
    DrawCubeWiresV((Vector3){  0.14f, -0.55f, 0 }, (Vector3){ 0.20f, 0.58f, 0.22f }, darkCol);

    DrawCubeV((Vector3){ -0.14f, -0.87f, 0.04f }, (Vector3){ 0.22f, 0.10f, 0.32f }, darkCol);
    DrawCubeV((Vector3){  0.14f, -0.87f, 0.04f }, (Vector3){ 0.22f, 0.10f, 0.32f }, darkCol);

    DrawCubeV((Vector3){ 0, 0.02f, 0.55f }, (Vector3){ 0.10f, 0.13f, 0.60f }, gunCol);
    DrawCubeWiresV((Vector3){ 0, 0.02f, 0.55f }, (Vector3){ 0.10f, 0.13f, 0.60f }, darkCol);
    DrawCylinderEx(
        (Vector3){ 0, 0.03f, 0.85f },
        (Vector3){ 0, 0.03f, 1.15f },
        0.022f, 0.022f, 8, gunSteel);
    DrawCubeV((Vector3){ 0, -0.10f, 0.42f }, (Vector3){ 0.08f, 0.18f, 0.10f }, gunCol);
    DrawCubeV((Vector3){ 0, 0.02f, 0.18f }, (Vector3){ 0.08f, 0.16f, 0.14f }, gunCol);
    DrawCubeV((Vector3){ 0, 0.10f, 0.60f }, (Vector3){ 0.06f, 0.06f, 0.06f }, darkCol);

    if (e->muzzleFlash > 0.0f) {
        float t = e->muzzleFlash / 0.06f;
        float s = 0.06f * (0.5f + 0.5f * t) * (0.7f + RandomFloat() * 0.6f);
        Vector3 fp = { 0, 0.03f, 1.22f };
        DrawSphere(fp, s * 2.2f, (Color){ 255, 140, 40, 255 });
        DrawSphere(fp, s * 1.2f, (Color){ 255, 210, 90, 255 });
        DrawSphere(fp, s * 0.6f, (Color){ 255, 255, 240, 255 });
        DrawCubeV(fp, (Vector3){ s * 6.0f, s * 0.6f, s * 0.6f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(fp, (Vector3){ s * 0.6f, s * 6.0f, s * 0.6f }, (Color){ 255, 230, 150, 255 });
    }

    rlPopMatrix();

    Vector3 barPos = { e->position.x, e->position.y + 1.05f, e->position.z };
    DrawCube(barPos, 0.9f, 0.08f, 0.08f, BLACK);
    float hpFrac = e->health / ENEMY_MAX_HEALTH;
    if (hpFrac < 0.0f) hpFrac = 0.0f;
    Vector3 fill = { barPos.x - 0.45f + 0.45f * hpFrac, barPos.y, barPos.z };
    DrawCube(fill, 0.9f * hpFrac, 0.08f, 0.08f, RED);
}

static void DrawPistolViewmodel(Vector3 off, float muzzleTimer, float reloadT) {
    Color skin      = (Color){ 228, 178, 138, 255 };
    Color skinShade = (Color){ 178, 132,  98, 255 };
    Color skinLine  = (Color){ 110,  75,  55, 255 };

    Color gunSlide  = (Color){  62,  65,  72, 255 };
    Color gunFrame  = (Color){  34,  36,  40, 255 };
    Color gunDark   = (Color){  16,  18,  22, 255 };
    Color gunSteel  = (Color){  95,  98, 105, 255 };
    Color gunSerra  = (Color){  24,  26,  30, 255 };

    if (reloadT > 0.0f) {
        float dip = sinf(reloadT * PI);
        off.y -= dip * 0.28f;
        off.z += dip * 0.05f;
        off.x += dip * 0.04f;
    }

    Vector3 armPos  = Vector3Add((Vector3){ 0.38f, -0.62f, -0.42f }, off);
    Vector3 armSize = { 0.22f, 0.22f, 0.75f };
    DrawCubeV(armPos, armSize, skin);
    DrawCubeWiresV(armPos, armSize, skinLine);

    Vector3 armShadow = Vector3Add(armPos, (Vector3){ 0, -0.10f, 0 });
    DrawCubeV(armShadow, (Vector3){ 0.22f, 0.02f, 0.75f }, skinShade);

    Vector3 handPos  = Vector3Add((Vector3){ 0.32f, -0.50f, -0.88f }, off);
    Vector3 handSize = { 0.24f, 0.24f, 0.24f };
    DrawCubeV(handPos, handSize, skin);
    DrawCubeWiresV(handPos, handSize, skinLine);

    Vector3 gripPos  = Vector3Add((Vector3){ 0.32f, -0.62f, -0.90f }, off);
    Vector3 gripSize = { 0.12f, 0.28f, 0.14f };
    DrawCubeV(gripPos, gripSize, gunFrame);
    DrawCubeWiresV(gripPos, gripSize, gunDark);

    Vector3 framePos  = Vector3Add((Vector3){ 0.30f, -0.42f, -1.00f }, off);
    Vector3 frameSize = { 0.11f, 0.06f, 0.30f };
    DrawCubeV(framePos, frameSize, gunFrame);

    Vector3 slidePos  = Vector3Add((Vector3){ 0.30f, -0.34f, -1.14f }, off);
    Vector3 slideSize = { 0.13f, 0.14f, 0.55f };
    DrawCubeV(slidePos, slideSize, gunSlide);
    DrawCubeWiresV(slidePos, slideSize, gunDark);

    for (int i = 0; i < 3; i++) {
        Vector3 s = Vector3Add(slidePos, (Vector3){ 0, 0, 0.19f - i * 0.035f });
        DrawCubeV(s, (Vector3){ 0.132f, 0.14f, 0.012f }, gunSerra);
    }

    Vector3 topStrip = Vector3Add(slidePos, (Vector3){ 0, 0.072f, 0 });
    DrawCubeV(topStrip, (Vector3){ 0.09f, 0.004f, 0.55f }, gunDark);

    Vector3 ejPort = Vector3Add(slidePos, (Vector3){ 0.055f, 0.03f, -0.06f });
    DrawCubeV(ejPort, (Vector3){ 0.03f, 0.06f, 0.10f }, gunDark);

    Vector3 rSight = Vector3Add((Vector3){ 0.30f, -0.24f, -0.90f }, off);
    DrawCubeV(rSight, (Vector3){ 0.11f, 0.03f, 0.03f }, gunDark);

    Vector3 fSight = Vector3Add((Vector3){ 0.30f, -0.24f, -1.38f }, off);
    DrawCubeV(fSight, (Vector3){ 0.03f, 0.03f, 0.03f }, gunDark);

    Vector3 tgPos = Vector3Add((Vector3){ 0.30f, -0.49f, -0.94f }, off);
    DrawCubeV(tgPos, (Vector3){ 0.075f, 0.035f, 0.14f }, gunFrame);

    Vector3 trigger = Vector3Add((Vector3){ 0.30f, -0.46f, -0.94f }, off);
    DrawCubeV(trigger, (Vector3){ 0.03f, 0.05f, 0.02f }, gunSteel);

    Vector3 barrelA = Vector3Add((Vector3){ 0.30f, -0.32f, -1.41f }, off);
    Vector3 barrelB = Vector3Add((Vector3){ 0.30f, -0.32f, -1.47f }, off);
    DrawCylinderEx(barrelA, barrelB, 0.035f, 0.035f, 12, gunSteel);

    Vector3 magPos = Vector3Add((Vector3){ 0.32f, -0.79f, -0.90f }, off);
    if (reloadT < 0.5f) {
        DrawCubeV(magPos, (Vector3){ 0.115f, 0.045f, 0.13f }, gunDark);
    }

    if (muzzleTimer > 0.0f) {
        float t = muzzleTimer / MUZZLE_FLASH_TIME;
        float flicker = 0.7f + RandomFloat() * 0.6f;
        float s = 0.05f * flicker * (0.4f + 0.6f * t);

        Vector3 flashPos = Vector3Add(off, (Vector3){ 0.30f, -0.32f, -1.52f });

        DrawSphere(flashPos, s * 2.4f, (Color){ 255, 130, 30, 255 });
        DrawSphere(flashPos, s * 1.4f, (Color){ 255, 200, 80, 255 });
        DrawSphere(flashPos, s * 0.7f, (Color){ 255, 255, 240, 255 });

        DrawCubeV(flashPos, (Vector3){ s * 7.0f, s * 0.5f, s * 0.5f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(flashPos, (Vector3){ s * 0.5f, s * 7.0f, s * 0.5f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(flashPos, (Vector3){ s * 3.0f, s * 0.4f, s * 0.4f }, (Color){ 255, 255, 220, 255 });
    }
}

static void DrawAKRViewmodel(Vector3 off, float muzzleTimer, float reloadT) {
    Color skin      = (Color){ 228, 178, 138, 255 };
    Color skinShade = (Color){ 178, 132,  98, 255 };
    Color skinLine  = (Color){ 110,  75,  55, 255 };

    Color gunBody   = (Color){  42,  44,  46, 255 };
    Color gunBody2  = (Color){  28,  30,  32, 255 };
    Color gunDark   = (Color){  14,  16,  18, 255 };
    Color gunSteel  = (Color){  78,  82,  86, 255 };
    Color woodCol   = (Color){ 110,  68,  38, 255 };
    Color woodDark  = (Color){  72,  44,  24, 255 };

    if (reloadT > 0.0f) {
        float dip = sinf(reloadT * PI);
        off.y -= dip * 0.30f;
        off.z += dip * 0.06f;
        off.x += dip * 0.04f;
    }

    Vector3 armPos  = Vector3Add((Vector3){ 0.40f, -0.64f, -0.42f }, off);
    Vector3 armSize = { 0.22f, 0.22f, 0.78f };
    DrawCubeV(armPos, armSize, skin);
    DrawCubeWiresV(armPos, armSize, skinLine);

    Vector3 armShadow = Vector3Add(armPos, (Vector3){ 0, -0.10f, 0 });
    DrawCubeV(armShadow, (Vector3){ 0.22f, 0.02f, 0.78f }, skinShade);

    Vector3 handPos  = Vector3Add((Vector3){ 0.34f, -0.52f, -0.92f }, off);
    Vector3 handSize = { 0.24f, 0.24f, 0.24f };
    DrawCubeV(handPos, handSize, skin);
    DrawCubeWiresV(handPos, handSize, skinLine);

    Vector3 gripPos  = Vector3Add((Vector3){ 0.32f, -0.66f, -0.94f }, off);
    Vector3 gripSize = { 0.13f, 0.30f, 0.15f };
    DrawCubeV(gripPos, gripSize, woodCol);
    DrawCubeWiresV(gripPos, gripSize, woodDark);

    Vector3 receiverPos  = Vector3Add((Vector3){ 0.32f, -0.42f, -1.28f }, off);
    Vector3 receiverSize = { 0.16f, 0.20f, 0.95f };
    DrawCubeV(receiverPos, receiverSize, gunBody);
    DrawCubeWiresV(receiverPos, receiverSize, gunDark);

    Vector3 coverPos = Vector3Add((Vector3){ 0.32f, -0.30f, -1.28f }, off);
    DrawCubeV(coverPos, (Vector3){ 0.14f, 0.03f, 0.95f }, gunBody2);

    Vector3 handguardPos  = Vector3Add((Vector3){ 0.32f, -0.40f, -1.85f }, off);
    Vector3 handguardSize = { 0.14f, 0.16f, 0.42f };
    DrawCubeV(handguardPos, handguardSize, woodCol);
    DrawCubeWiresV(handguardPos, handguardSize, woodDark);

    Vector3 gasBlockPos = Vector3Add((Vector3){ 0.32f, -0.32f, -2.05f }, off);
    DrawCubeV(gasBlockPos, (Vector3){ 0.10f, 0.10f, 0.12f }, gunSteel);

    Vector3 gasTubeA = Vector3Add((Vector3){ 0.32f, -0.32f, -1.65f }, off);
    Vector3 gasTubeB = Vector3Add((Vector3){ 0.32f, -0.32f, -2.05f }, off);
    DrawCylinderEx(gasTubeA, gasTubeB, 0.025f, 0.025f, 8, gunSteel);

    Vector3 barrelA = Vector3Add((Vector3){ 0.32f, -0.42f, -2.05f }, off);
    Vector3 barrelB = Vector3Add((Vector3){ 0.32f, -0.42f, -2.55f }, off);
    DrawCylinderEx(barrelA, barrelB, 0.028f, 0.028f, 10, gunSteel);

    Vector3 brakePos = Vector3Add((Vector3){ 0.32f, -0.42f, -2.60f }, off);
    DrawCubeV(brakePos, (Vector3){ 0.07f, 0.07f, 0.10f }, gunDark);

    Vector3 frontSightBase = Vector3Add((Vector3){ 0.32f, -0.32f, -2.40f }, off);
    DrawCubeV(frontSightBase, (Vector3){ 0.06f, 0.10f, 0.06f }, gunSteel);
    Vector3 frontSightPost = Vector3Add((Vector3){ 0.32f, -0.24f, -2.40f }, off);
    DrawCubeV(frontSightPost, (Vector3){ 0.015f, 0.06f, 0.015f }, gunDark);

    Vector3 rearSightPos = Vector3Add((Vector3){ 0.32f, -0.28f, -1.10f }, off);
    DrawCubeV(rearSightPos, (Vector3){ 0.11f, 0.04f, 0.04f }, gunDark);

    Vector3 magBase = Vector3Add((Vector3){ 0.32f, -0.60f, -1.15f }, off);
    DrawCubeV(magBase, (Vector3){ 0.13f, 0.16f, 0.20f }, gunBody2);
    Vector3 magMid = Vector3Add(magBase, (Vector3){ 0, -0.15f, 0.02f });
    DrawCubeV(magMid, (Vector3){ 0.13f, 0.16f, 0.20f }, gunBody2);
    Vector3 magEnd = Vector3Add(magBase, (Vector3){ 0, -0.30f, 0.08f });
    DrawCubeV(magEnd, (Vector3){ 0.13f, 0.16f, 0.20f }, gunBody2);

    Vector3 stockPos = Vector3Add((Vector3){ 0.34f, -0.46f, -0.55f }, off);
    DrawCubeV(stockPos, (Vector3){ 0.14f, 0.22f, 0.50f }, woodCol);
    DrawCubeWiresV(stockPos, (Vector3){ 0.14f, 0.22f, 0.50f }, woodDark);

    Vector3 buttPos = Vector3Add((Vector3){ 0.34f, -0.46f, -0.32f }, off);
    DrawCubeV(buttPos, (Vector3){ 0.16f, 0.24f, 0.04f }, gunDark);

    Vector3 tgPos = Vector3Add((Vector3){ 0.32f, -0.54f, -1.00f }, off);
    DrawCubeV(tgPos, (Vector3){ 0.09f, 0.035f, 0.16f }, gunBody2);

    Vector3 trigger = Vector3Add((Vector3){ 0.32f, -0.51f, -1.00f }, off);
    DrawCubeV(trigger, (Vector3){ 0.03f, 0.05f, 0.02f }, gunSteel);

    Vector3 charge = Vector3Add((Vector3){ 0.43f, -0.36f, -1.05f }, off);
    DrawCubeV(charge, (Vector3){ 0.06f, 0.03f, 0.10f }, gunSteel);

    if (muzzleTimer > 0.0f) {
        float t = muzzleTimer / MUZZLE_FLASH_TIME;
        float flicker = 0.7f + RandomFloat() * 0.6f;
        float s = 0.075f * flicker * (0.4f + 0.6f * t);

        Vector3 flashPos = Vector3Add(off, (Vector3){ 0.32f, -0.42f, -2.68f });

        DrawSphere(flashPos, s * 2.5f, (Color){ 255, 130, 30, 255 });
        DrawSphere(flashPos, s * 1.5f, (Color){ 255, 200, 80, 255 });
        DrawSphere(flashPos, s * 0.7f, (Color){ 255, 255, 240, 255 });

        DrawCubeV(flashPos, (Vector3){ s * 8.0f, s * 0.5f, s * 0.5f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(flashPos, (Vector3){ s * 0.5f, s * 8.0f, s * 0.5f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(flashPos, (Vector3){ s * 3.5f, s * 0.4f, s * 0.4f }, (Color){ 255, 255, 220, 255 });
    }
}

static void DrawSniperViewmodel(Vector3 off, float muzzleTimer, float reloadT) {
    Color skin      = (Color){ 228, 178, 138, 255 };
    Color skinShade = (Color){ 178, 132,  98, 255 };
    Color skinLine  = (Color){ 110,  75,  55, 255 };

    Color gunBody   = (Color){  38,  42,  34, 255 };
    Color gunBody2  = (Color){  28,  32,  26, 255 };
    Color gunDark   = (Color){  12,  14,  12, 255 };
    Color gunSteel  = (Color){  70,  74,  68, 255 };
    Color scopeCol  = (Color){  22,  24,  22, 255 };
    Color scopeLens = (Color){  60, 110, 160, 255 };
    Color woodCol   = (Color){  92,  62,  38, 255 };

    if (reloadT > 0.0f) {
        float dip = sinf(reloadT * PI);
        off.y -= dip * 0.32f;
        off.z += dip * 0.08f;
        off.x += dip * 0.05f;
    }

    Vector3 armPos  = Vector3Add((Vector3){ 0.42f, -0.66f, -0.42f }, off);
    Vector3 armSize = { 0.22f, 0.22f, 0.78f };
    DrawCubeV(armPos, armSize, skin);
    DrawCubeWiresV(armPos, armSize, skinLine);

    Vector3 armShadow = Vector3Add(armPos, (Vector3){ 0, -0.10f, 0 });
    DrawCubeV(armShadow, (Vector3){ 0.22f, 0.02f, 0.78f }, skinShade);

    Vector3 handPos  = Vector3Add((Vector3){ 0.36f, -0.54f, -0.92f }, off);
    Vector3 handSize = { 0.24f, 0.24f, 0.24f };
    DrawCubeV(handPos, handSize, skin);
    DrawCubeWiresV(handPos, handSize, skinLine);

    Vector3 gripPos  = Vector3Add((Vector3){ 0.34f, -0.66f, -0.94f }, off);
    Vector3 gripSize = { 0.13f, 0.30f, 0.16f };
    DrawCubeV(gripPos, gripSize, woodCol);
    DrawCubeWiresV(gripPos, gripSize, gunDark);

    Vector3 receiverPos  = Vector3Add((Vector3){ 0.32f, -0.40f, -1.35f }, off);
    Vector3 receiverSize = { 0.18f, 0.22f, 1.30f };
    DrawCubeV(receiverPos, receiverSize, gunBody);
    DrawCubeWiresV(receiverPos, receiverSize, gunDark);

    Vector3 topRailPos = Vector3Add((Vector3){ 0.32f, -0.28f, -1.35f }, off);
    DrawCubeV(topRailPos, (Vector3){ 0.10f, 0.02f, 1.30f }, gunBody2);

    Vector3 stockPos  = Vector3Add((Vector3){ 0.34f, -0.46f, -0.50f }, off);
    Vector3 stockSize = { 0.18f, 0.24f, 0.55f };
    DrawCubeV(stockPos, stockSize, woodCol);
    DrawCubeWiresV(stockPos, stockSize, gunDark);

    Vector3 cheekPos = Vector3Add((Vector3){ 0.34f, -0.30f, -0.50f }, off);
    DrawCubeV(cheekPos, (Vector3){ 0.20f, 0.10f, 0.50f }, woodCol);

    Vector3 barrelPos  = Vector3Add((Vector3){ 0.32f, -0.42f, -2.25f }, off);
    Vector3 barrelSize = { 0.085f, 0.085f, 0.60f };
    DrawCubeV(barrelPos, barrelSize, gunSteel);
    DrawCubeWiresV(barrelPos, barrelSize, gunDark);

    Vector3 brakeA = Vector3Add((Vector3){ 0.32f, -0.42f, -2.55f }, off);
    Vector3 brakeB = Vector3Add((Vector3){ 0.32f, -0.42f, -2.72f }, off);
    DrawCylinderEx(brakeA, brakeB, 0.07f, 0.07f, 10, gunDark);

    Vector3 scopeBodyPos  = Vector3Add((Vector3){ 0.32f, -0.18f, -1.30f }, off);
    Vector3 scopeBodySize = { 0.11f, 0.11f, 0.62f };
    DrawCubeV(scopeBodyPos, scopeBodySize, scopeCol);
    DrawCubeWiresV(scopeBodyPos, scopeBodySize, gunDark);

    Vector3 scopeMountA = Vector3Add((Vector3){ 0.32f, -0.26f, -1.05f }, off);
    DrawCubeV(scopeMountA, (Vector3){ 0.07f, 0.10f, 0.06f }, gunDark);
    Vector3 scopeMountB = Vector3Add((Vector3){ 0.32f, -0.26f, -1.55f }, off);
    DrawCubeV(scopeMountB, (Vector3){ 0.07f, 0.10f, 0.06f }, gunDark);

    Vector3 scopeFront  = Vector3Add((Vector3){ 0.32f, -0.18f, -1.68f }, off);
    Vector3 scopeFrontS = { 0.14f, 0.14f, 0.08f };
    DrawCubeV(scopeFront, scopeFrontS, scopeCol);
    DrawCubeWiresV(scopeFront, scopeFrontS, gunDark);

    Vector3 scopeLensA = Vector3Add((Vector3){ 0.32f, -0.18f, -1.73f }, off);
    Vector3 scopeLensB = Vector3Add((Vector3){ 0.32f, -0.18f, -1.735f }, off);
    DrawCylinderEx(scopeLensA, scopeLensB, 0.055f, 0.055f, 12, scopeLens);

    Vector3 scopeRear  = Vector3Add((Vector3){ 0.32f, -0.18f, -0.92f }, off);
    Vector3 scopeRearS = { 0.13f, 0.13f, 0.08f };
    DrawCubeV(scopeRear, scopeRearS, scopeCol);
    DrawCubeWiresV(scopeRear, scopeRearS, gunDark);

    Vector3 scopeEyeA = Vector3Add((Vector3){ 0.32f, -0.18f, -0.87f }, off);
    Vector3 scopeEyeB = Vector3Add((Vector3){ 0.32f, -0.18f, -0.865f }, off);
    DrawCylinderEx(scopeEyeA, scopeEyeB, 0.05f, 0.05f, 12, scopeLens);

    Vector3 boltPos = Vector3Add((Vector3){ 0.44f, -0.36f, -1.10f }, off);
    DrawCubeV(boltPos, (Vector3){ 0.14f, 0.04f, 0.04f }, gunSteel);
    Vector3 boltKnob = Vector3Add((Vector3){ 0.52f, -0.36f, -1.10f }, off);
    DrawSphere(boltKnob, 0.035f, gunSteel);

    Vector3 magPos  = Vector3Add((Vector3){ 0.32f, -0.62f, -1.22f }, off);
    Vector3 magSize = { 0.12f, 0.22f, 0.24f };
    if (reloadT < 0.55f) {
        DrawCubeV(magPos, magSize, gunBody2);
        DrawCubeWiresV(magPos, magSize, gunDark);
    }

    Vector3 tgPos = Vector3Add((Vector3){ 0.32f, -0.52f, -0.98f }, off);
    DrawCubeV(tgPos, (Vector3){ 0.085f, 0.035f, 0.16f }, gunBody2);

    Vector3 trigger = Vector3Add((Vector3){ 0.32f, -0.49f, -0.98f }, off);
    DrawCubeV(trigger, (Vector3){ 0.03f, 0.05f, 0.02f }, gunSteel);

    Vector3 bipodTopL = Vector3Add((Vector3){ 0.32f, -0.52f, -1.90f }, off);
    Vector3 bipodBotL = Vector3Add((Vector3){ 0.22f, -0.72f, -1.86f }, off);
    DrawCylinderEx(bipodTopL, bipodBotL, 0.014f, 0.014f, 6, gunDark);
    Vector3 bipodTopR = Vector3Add((Vector3){ 0.32f, -0.52f, -1.90f }, off);
    Vector3 bipodBotR = Vector3Add((Vector3){ 0.42f, -0.72f, -1.86f }, off);
    DrawCylinderEx(bipodTopR, bipodBotR, 0.014f, 0.014f, 6, gunDark);

    if (muzzleTimer > 0.0f) {
        float t = muzzleTimer / MUZZLE_FLASH_TIME;
        float flicker = 0.7f + RandomFloat() * 0.6f;
        float s = 0.10f * flicker * (0.4f + 0.6f * t);

        Vector3 flashPos = Vector3Add(off, (Vector3){ 0.32f, -0.42f, -2.78f });

        DrawSphere(flashPos, s * 2.6f, (Color){ 255, 130, 30, 255 });
        DrawSphere(flashPos, s * 1.6f, (Color){ 255, 200, 80, 255 });
        DrawSphere(flashPos, s * 0.8f, (Color){ 255, 255, 240, 255 });

        DrawCubeV(flashPos, (Vector3){ s * 9.0f, s * 0.6f, s * 0.6f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(flashPos, (Vector3){ s * 0.6f, s * 9.0f, s * 0.6f }, (Color){ 255, 230, 150, 255 });
        DrawCubeV(flashPos, (Vector3){ s * 4.0f, s * 0.5f, s * 0.5f }, (Color){ 255, 255, 220, 255 });
    }
}

static void DrawWeaponViewmodel(int index, Vector3 off, float muzzleTimer, float reloadT) {
    if (index == 0)      DrawPistolViewmodel(off, muzzleTimer, reloadT);
    else if (index == 1) DrawAKRViewmodel(off, muzzleTimer, reloadT);
    else                 DrawSniperViewmodel(off, muzzleTimer, reloadT);
}

static void DrawScopeOverlay(void) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    float scopeSize = (float)sh * 0.86f;
    float left   = (sw - scopeSize) * 0.5f;
    float top    = (sh - scopeSize) * 0.5f;
    float right  = left + scopeSize;
    float bottom = top + scopeSize;

    DrawRectangle(0, 0, sw, (int)top, BLACK);
    DrawRectangle(0, (int)bottom, sw, sh - (int)bottom, BLACK);
    DrawRectangle(0, (int)top, (int)left, (int)scopeSize, BLACK);
    DrawRectangle((int)right, (int)top, sw - (int)right, (int)scopeSize, BLACK);

    Vector2 center = { sw * 0.5f, sh * 0.5f };
    float r = scopeSize * 0.5f;

    DrawCircleLinesV(center, r,        BLACK);
    DrawCircleLinesV(center, r - 2.0f, (Color){ 60, 60, 60, 255 });
    DrawCircleLinesV(center, r - 4.0f, (Color){ 25, 25, 25, 255 });

    Color lineCol = (Color){ 15, 15, 15, 255 };

    DrawLine((int)(center.x - r + 6), (int)center.y, (int)(center.x - 8), (int)center.y, lineCol);
    DrawLine((int)(center.x + 8),     (int)center.y, (int)(center.x + r - 6), (int)center.y, lineCol);
    DrawLine((int)center.x, (int)(center.y - r + 6), (int)center.x, (int)(center.y - 8), lineCol);
    DrawLine((int)center.x, (int)(center.y + 8),     (int)center.x, (int)(center.y + r - 6), lineCol);

    for (int i = 1; i <= 5; i++) {
        int off = i * 22;
        DrawLine((int)(center.x - 4), (int)(center.y + off), (int)(center.x + 4), (int)(center.y + off), lineCol);
        DrawLine((int)(center.x - 4), (int)(center.y - off), (int)(center.x + 4), (int)(center.y - off), lineCol);
        DrawLine((int)(center.x + off), (int)(center.y - 4), (int)(center.x + off), (int)(center.y + 4), lineCol);
        DrawLine((int)(center.x - off), (int)(center.y - 4), (int)(center.x - off), (int)(center.y + 4), lineCol);
    }

    DrawCircleV(center, 1.6f, (Color){ 220, 40, 40, 255 });
}

static void DrawHUD(Player* player, Weapon* w, bool reloading) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(20, 20, 260, 32, (Color){ 0, 0, 0, 140 });
    DrawRectangleLines(20, 20, 260, 32, (Color){ 180, 180, 180, 200 });

    float hpFrac = player->health / PLAYER_MAX_HEALTH;
    if (hpFrac < 0) hpFrac = 0;
    Color hpCol = (Color){
        (unsigned char)(220 * (1.0f - hpFrac) + 60 * hpFrac),
        (unsigned char)(60  * (1.0f - hpFrac) + 200 * hpFrac),
        60, 255
    };
    DrawRectangle(24, 24, (int)(252 * hpFrac), 24, hpCol);
    const char* hpTxt = TextFormat("%d", (int)player->health);
    DrawText(hpTxt, 30, 28, 20, RAYWHITE);

    DrawText(w->name, sw - 30 - MeasureText(w->name, 28), sh - 120, 28,
             (Color){ 200, 200, 200, 255 });

    const char* ammoTxt;
    Color ammoCol = RAYWHITE;
    if (reloading) {
        ammoTxt = "RELOADING";
        ammoCol = (Color){ 255, 200, 80, 255 };
    } else {
        ammoTxt = TextFormat("%d / %d", w->ammoInMag, w->ammoReserve);
        if (w->ammoInMag == 0) ammoCol = (Color){ 255, 90, 90, 255 };
        else if (w->ammoInMag <= 5) ammoCol = (Color){ 255, 200, 80, 255 };
    }
    int txtW = MeasureText(ammoTxt, 42);
    DrawText(ammoTxt, sw - txtW - 30, sh - 70, 42, ammoCol);
}

static void DrawHitMarker(float timer, int type) {
    if (timer <= 0.0f) return;

    float t = timer / HITMARKER_TIME;
    unsigned char a = (unsigned char)(255 * t);
    int cx = GetScreenWidth()  / 2;
    int cy = GetScreenHeight() / 2;

    Color col;
    float size;
    float gap;
    float thick;

    if (type == 3) {
        col   = (Color){ 255, 70, 70, a };
        size  = 18.0f + (1.0f - t) * 8.0f;
        gap   = 6.0f;
        thick = 3.0f;
    } else if (type == 2) {
        col   = (Color){ 255, 215, 70, a };
        size  = 15.0f + (1.0f - t) * 5.0f;
        gap   = 5.0f;
        thick = 2.5f;
    } else {
        col   = (Color){ 245, 245, 245, a };
        size  = 11.0f;
        gap   = 4.0f;
        thick = 2.0f;
    }

    Vector2 c = { (float)cx, (float)cy };

    DrawLineEx((Vector2){ c.x - size, c.y - size }, (Vector2){ c.x - gap, c.y - gap }, thick, col);
    DrawLineEx((Vector2){ c.x + size, c.y - size }, (Vector2){ c.x + gap, c.y - gap }, thick, col);
    DrawLineEx((Vector2){ c.x - size, c.y + size }, (Vector2){ c.x - gap, c.y + gap }, thick, col);
    DrawLineEx((Vector2){ c.x + size, c.y + size }, (Vector2){ c.x + gap, c.y + gap }, thick, col);
}

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Nigger Strike");
    InitAudioDevice();
    SetExitKey(KEY_NULL);
    DisableCursor();
    SetTargetFPS(144);

    Sound sfxShot         = LoadSound("/home/Rhema/Projects/UI/shot.mp3");
    Sound sfxShotAKR      = LoadSound("/home/Rhema/Projects/UI/shot.mp3");
    Sound sfxShotSniper   = LoadSound("/home/Rhema/Projects/UI/shot_sniper.mp3");
    Sound sfxReload       = LoadSound("/home/Rhema/Projects/UI/reload.mp3");
    Sound sfxFootstep     = LoadSound("/home/Rhema/Projects/UI/footstep.mp3");
    Sound sfxJump         = LoadSound("/home/Rhema/Projects/UI/jump.mp3");
    Sound sfxHit          = LoadSound("/home/Rhema/Projects/UI/hit.mp3");
    Sound sfxEnemyHurt    = LoadSound("/home/Rhema/Projects/UI/enemy_hurt.mp3");
    Sound sfxEnemyDie     = LoadSound("/home/Rhema/Projects/UI/enemy_die.mp3");
    Sound sfxPlayerHurt   = LoadSound("/home/Rhema/Projects/UI/player_hurt.mp3");

    Sound shotSounds[WEAPON_COUNT] = { sfxShot, sfxShotAKR, sfxShotSniper };

    for (int i = 0; i < WEAPON_COUNT; i++) {
        if (shotSounds[i].frameCount == 0) {
            TraceLog(LOG_WARNING, "Weapon %d shot sound missing, falling back to pistol", i);
            shotSounds[i] = sfxShot;
        }
    }

    SetSoundVolume(sfxShot,       0.75f);
    SetSoundVolume(sfxShotAKR,    0.75f);
    SetSoundVolume(sfxShotSniper, 0.85f);
    SetSoundVolume(sfxReload,     0.70f);
    SetSoundVolume(sfxFootstep,   0.25f);
    SetSoundVolume(sfxJump,       0.45f);
    SetSoundVolume(sfxHit,        0.45f);
    SetSoundVolume(sfxEnemyHurt,  0.55f);
    SetSoundVolume(sfxEnemyDie,   0.65f);
    SetSoundVolume(sfxPlayerHurt, 0.70f);

    Player player = { 0 };
    player.position = (Vector3){ 0.0f, 0.0f, 0.0f };
    player.grounded = true;
    player.health   = PLAYER_MAX_HEALTH;

    Weapon weapons[WEAPON_COUNT] = {
        {
            "PISTOL",
            30, 30, 210, 210,
            0.11f, 1.4f,
            34.0f, 100.0f,
            0.014f, 0.010f, 0.075f,
            55.0f, 0.75f,
            true, false
        },
        {
            "AKR",
            30, 30, 180, 180,
            0.10f, 2.1f,
            50.0f, 100.0f,
            0.018f, 0.011f, 0.090f,
            48.0f, 0.55f,
            true, false
        },
        {
            "SNIPER",
            5, 5, 25, 25,
            1.30f, 2.8f,
            250.0f, 500.0f,
            0.055f, 0.020f, 0.240f,
            20.0f, 0.30f,
            false, true
        }
    };
    int currentWeapon = 0;
    int switchKeys[WEAPON_COUNT] = { KEY_ONE, KEY_TWO, KEY_THREE };

    bool  adsActive = false;
    float adsBlend  = 0.0f;

    Color colConcrete = (Color){ 120, 115, 105, 255 };
    Color colConcrete2= (Color){ 140, 135, 125, 255 };
    Color colWood     = (Color){ 130,  80,  50, 255 };
    Color colWoodDark = (Color){  70,  40,  22, 255 };
    Color colWoodCrate= (Color){ 120,  80,  50, 255 };
    Color colPillar   = (Color){ 110, 100,  90, 255 };
    Color colPlatform = (Color){ 100,  95,  85, 255 };
    Color colStair    = (Color){  90,  85,  75, 255 };
    Color colRailing  = (Color){  60,  58,  54, 255 };

    Box boxes[] = {
        { {   0, 3, -50 }, { 50, 3, 0.5 }, colConcrete },
        { {   0, 3,  50 }, { 50, 3, 0.5 }, colConcrete },
        { {  50, 3,   0 }, { 0.5, 3, 50 }, colConcrete },
        { { -50, 3,   0 }, { 0.5, 3, 50 }, colConcrete },

        { { -29, 2.5, -19 }, { 2.0, 2.5, 0.3 }, colWood },
        { { -21, 2.5, -19 }, { 2.0, 2.5, 0.3 }, colWood },
        { { -25,    0.75, -31 }, { 6.0,  0.75, 0.3 }, colWood },
        { { -25,    4.0,  -31 }, { 6.0,  1.0,  0.3 }, colWood },
        { { -29.75, 2.25, -31 }, { 1.25, 0.75, 0.3 }, colWood },
        { { -25,    2.25, -31 }, { 2.0,  0.75, 0.3 }, colWood },
        { { -20.25, 2.25, -31 }, { 1.25, 0.75, 0.3 }, colWood },
        { { -31, 0.75, -25 }, { 0.3, 0.75, 6.0 }, colWood },
        { { -31, 4.0,  -25 }, { 0.3, 1.0,  6.0 }, colWood },
        { { -31, 2.25, -30.5 }, { 0.3, 0.75, 0.5 }, colWood },
        { { -31, 2.25, -25   }, { 0.3, 0.75, 3.0 }, colWood },
        { { -31, 2.25, -19.5 }, { 0.3, 0.75, 0.5 }, colWood },
        { { -19, 0.75, -25 }, { 0.3, 0.75, 6.0 }, colWood },
        { { -19, 4.0,  -25 }, { 0.3, 1.0,  6.0 }, colWood },
        { { -19, 2.25, -30.5 }, { 0.3, 0.75, 0.5 }, colWood },
        { { -19, 2.25, -25   }, { 0.3, 0.75, 3.0 }, colWood },
        { { -19, 2.25, -19.5 }, { 0.3, 0.75, 0.5 }, colWood },
        { { -25, 5.3, -25 }, { 6.5, 0.3, 6.5 }, colWoodDark },

        { {  22, 2.5, 20 }, { 1.5, 2.5, 0.3 }, colWood },
        { {  28, 2.5, 20 }, { 1.5, 2.5, 0.3 }, colWood },
        { {  25, 0.75, 30 }, { 5.0, 0.75, 0.3 }, colWood },
        { {  25, 4.0,  30 }, { 5.0, 1.0,  0.3 }, colWood },
        { {  22, 2.25, 30 }, { 2.0, 0.75, 0.3 }, colWood },
        { {  28, 2.25, 30 }, { 2.0, 0.75, 0.3 }, colWood },
        { {  20, 0.75, 25 }, { 0.3, 0.75, 5.0 }, colWood },
        { {  20, 4.0,  25 }, { 0.3, 1.0,  5.0 }, colWood },
        { {  20, 2.25, 22 }, { 0.3, 0.75, 2.0 }, colWood },
        { {  20, 2.25, 28 }, { 0.3, 0.75, 2.0 }, colWood },
        { {  30, 0.75, 25 }, { 0.3, 0.75, 5.0 }, colWood },
        { {  30, 4.0,  25 }, { 0.3, 1.0,  5.0 }, colWood },
        { {  30, 2.25, 22 }, { 0.3, 0.75, 2.0 }, colWood },
        { {  30, 2.25, 28 }, { 0.3, 0.75, 2.0 }, colWood },
        { {  25, 5.3, 25 }, { 5.5, 0.3, 5.5 }, colWoodDark },

        { { -30, 2.0, 22 }, { 3.0, 2.0, 0.3 }, colWood },
        { { -30, 2.0, 28 }, { 3.0, 2.0, 0.3 }, colWood },
        { { -33, 2.0, 25 }, { 0.3, 2.0, 3.0 }, colWood },
        { { -27, 2.0, 25 }, { 0.3, 2.0, 3.0 }, colWood },
        { { -30, 4.3, 25 }, { 3.5, 0.3, 3.5 }, colWoodDark },

        { { 36.5, 2.85, 25 }, { 4.5, 0.15, 4.0 }, colPlatform },
        { { 36.5, 3.3, 21 }, { 4.5, 0.3, 0.1 }, colRailing },
        { { 36.5, 3.3, 29 }, { 4.5, 0.3, 0.1 }, colRailing },
        { {  41,  3.3, 25 }, { 0.1, 0.3, 4.0 }, colRailing },
        { { 29.5, 0.25, 25 }, { 0.5, 0.25, 2.5 }, colStair },
        { { 30.0, 0.75, 25 }, { 0.5, 0.25, 2.5 }, colStair },
        { { 30.5, 1.25, 25 }, { 0.5, 0.25, 2.5 }, colStair },
        { { 31.0, 1.75, 25 }, { 0.5, 0.25, 2.5 }, colStair },
        { { 31.5, 2.25, 25 }, { 0.5, 0.25, 2.5 }, colStair },
        { { 32.0, 2.75, 25 }, { 1.0, 0.25, 2.5 }, colStair },

        { { -10, 1.5,  10 }, { 4.0, 1.5, 0.3 }, colConcrete2 },
        { {  10, 1.5, -10 }, { 4.0, 1.5, 0.3 }, colConcrete2 },
        { { -10, 1.5, -10 }, { 0.3, 1.5, 4.0 }, colConcrete2 },
        { {  10, 1.5,  10 }, { 0.3, 1.5, 4.0 }, colConcrete2 },

        { { -35, 0.75,  15 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { { -33, 0.75,  15 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { { -35, 2.25,  15 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { {  35, 0.75, -15 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { {  37, 0.75, -15 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { {   0, 0.75,  30 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { {   0, 0.75, -30 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { { -20, 0.75,   5 }, { 0.75, 0.75, 0.75 }, colWoodCrate },
        { {  20, 0.75,  -5 }, { 0.75, 0.75, 0.75 }, colWoodCrate },

        { { -40, 1, -40 }, { 1.5, 1.0, 1.5 }, colPillar },
        { {  40, 1,  40 }, { 1.5, 1.0, 1.5 }, colPillar },
        { { -40, 1,  40 }, { 1.5, 1.0, 1.5 }, colPillar },
        { {  40, 1, -40 }, { 1.5, 1.0, 1.5 }, colPillar },
    };
    int boxCount = sizeof(boxes) / sizeof(boxes[0]);

    Enemy enemies[] = {
        { {  20.0f, 0.9f, -20.0f }, {  20.0f, 0.9f, -20.0f }, ENEMY_MAX_HEALTH, 0, 0, 0, 0, 0, true },
        { { -20.0f, 0.9f,  20.0f }, { -20.0f, 0.9f,  20.0f }, ENEMY_MAX_HEALTH, 0, 0, 0, 0, 0, true },
        { {   0.0f, 0.9f, -35.0f }, {   0.0f, 0.9f, -35.0f }, ENEMY_MAX_HEALTH, 0, 0, 0, 0, 0, true },
    };
    int enemyCount = sizeof(enemies) / sizeof(enemies[0]);

    Pickup pickups[MAX_PICKUPS] = {
        { { -30, 1.0f,  -5 }, PICKUP_AMMO,   true, 0, 0 },
        { {  30, 1.0f,   5 }, PICKUP_AMMO,   true, 0, 0 },
        { {   0, 1.0f,   0 }, PICKUP_AMMO,   true, 0, 0 },
        { { -25, 1.0f, -25 }, PICKUP_AMMO,   true, 0, 0 },
        { {  25, 1.0f,  25 }, PICKUP_HEALTH, true, 0, 0 },
        { { -30, 1.0f,  25 }, PICKUP_HEALTH, true, 0, 0 },
        { {  36.5f, 4.5f, 25 }, PICKUP_HEALTH, true, 0, 0 },
        { {   0, 1.0f,  35 }, PICKUP_AMMO,   true, 0, 0 },
    };

    DamageIndicator damageIndicators[MAX_DAMAGE_INDICATORS] = { 0 };
    DamageNumber    damageNumbers[MAX_DAMAGE_NUMBERS]       = { 0 };

    Camera3D camera = { 0 };
    camera.position   = (Vector3){ 0.0f, PLAYER_EYE_HEIGHT, 0.0f };
    camera.up         = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy       = 70.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float yaw   = 0.0f;
    float pitch = 0.0f;

    Vector2 sway = { 0 };
    float bobTime = 0.0f;
    float footstepTimer = 0.0f;

    float recoilBack   = 0.0f;
    float fireCooldown = 0.0f;
    float muzzleTimer  = 0.0f;

    bool  reloading   = false;
    float reloadTimer = 0.0f;
    float switchTimer = 0.0f;

    Vector3 lastHitPos   = { 0 };
    float   lastHitTimer = 0.0f;
    float   damageFlash  = 0.0f;

    float hitMarkerTimer = 0.0f;
    int   hitMarkerType  = 0;

    bool paused     = false;
    bool shouldQuit = false;

    RenderTexture2D viewRT = LoadRenderTexture(SCREEN_WIDTH, SCREEN_HEIGHT);
    SetTextureFilter(viewRT.texture, TEXTURE_FILTER_BILINEAR);

    Camera3D viewCam = { 0 };
    viewCam.position   = (Vector3){ 0, 0, 0 };
    viewCam.target     = (Vector3){ 0, 0, -1 };
    viewCam.up         = (Vector3){ 0, 1, 0 };
    viewCam.fovy       = 70.0f;
    viewCam.projection = CAMERA_PERSPECTIVE;

    Rectangle panel = {
        (SCREEN_WIDTH  - 420) / 2.0f,
        (SCREEN_HEIGHT - 340) / 2.0f,
        420, 340
    };
    Rectangle resumeBtn = { panel.x + 60, panel.y + 150, panel.width - 120, 60 };
    Rectangle quitBtn   = { panel.x + 60, panel.y + 230, panel.width - 120, 60 };

    while (!WindowShouldClose() && !shouldQuit) {
        float dt = GetFrameTime();

        Weapon* w = &weapons[currentWeapon];

        if (IsKeyPressed(KEY_ESCAPE)) {
            paused = !paused;
            if (paused) EnableCursor();
            else         DisableCursor();
        }

        if (paused) {
            BeginDrawing();
                ClearBackground((Color){ 135, 206, 235, 255 });
                BeginMode3D(camera);
                    DrawPlane((Vector3){ 0 }, (Vector2){ GROUND_SIZE, GROUND_SIZE },
                              (Color){ 185, 165, 125, 255 });
                    for (int i = 0; i < boxCount; i++) {
                        DrawCube(boxes[i].center,
                                 boxes[i].halfExtents.x * 2.0f,
                                 boxes[i].halfExtents.y * 2.0f,
                                 boxes[i].halfExtents.z * 2.0f,
                                 boxes[i].color);
                        DrawCubeWires(boxes[i].center,
                                      boxes[i].halfExtents.x * 2.0f,
                                      boxes[i].halfExtents.y * 2.0f,
                                      boxes[i].halfExtents.z * 2.0f, BLACK);
                    }
                    DrawPickups(pickups, MAX_PICKUPS, GetTime());
                    for (int i = 0; i < enemyCount; i++) {
                        if (!enemies[i].alive) continue;
                        DrawEnemy(&enemies[i], player.position);
                    }
                EndMode3D();

                BeginTextureMode(viewRT);
                    ClearBackground(BLANK);
                    BeginMode3D(viewCam);
                        DrawWeaponViewmodel(currentWeapon,
                                            (Vector3){ sway.x, sway.y, recoilBack },
                                            -1.0f, 0.0f);
                    EndMode3D();
                EndTextureMode();

                DrawTexturePro(viewRT.texture,
                    (Rectangle){ 0, 0, (float)viewRT.texture.width, -(float)viewRT.texture.height },
                    (Rectangle){ 0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT },
                    (Vector2){ 0, 0 }, 0.0f, WHITE);

                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (Color){ 0, 0, 0, 160 });
                DrawRectangleRec(panel, (Color){ 28, 30, 36, 245 });
                DrawRectangleLinesEx(panel, 3.0f, (Color){ 90, 95, 105, 255 });

                const char* title = "PAUSED";
                int titleW = MeasureText(title, 48);
                DrawText(title, panel.x + (panel.width - titleW) / 2, panel.y + 40, 48, RAYWHITE);
                DrawRectangle(panel.x + 60, panel.y + 105, panel.width - 120, 2,
                              (Color){ 90, 95, 105, 255 });

                Vector2 mouse = GetMousePosition();

                bool hoverResume = CheckCollisionPointRec(mouse, resumeBtn);
                Color resumeBg = hoverResume
                    ? (Color){ 70, 140, 90, 255 }
                    : (Color){ 45, 85, 60, 255 };
                DrawRectangleRec(resumeBtn, resumeBg);
                DrawRectangleLinesEx(resumeBtn, 2.0f, (Color){ 110, 200, 140, 255 });
                const char* resumeTxt = "RESUME";
                int resumeW = MeasureText(resumeTxt, 28);
                DrawText(resumeTxt, resumeBtn.x + (resumeBtn.width - resumeW) / 2,
                         resumeBtn.y + (resumeBtn.height - 28) / 2, 28, RAYWHITE);

                bool hoverQuit = CheckCollisionPointRec(mouse, quitBtn);
                Color quitBg = hoverQuit
                    ? (Color){ 170, 55, 55, 255 }
                    : (Color){ 90, 30, 30, 255 };
                DrawRectangleRec(quitBtn, quitBg);
                DrawRectangleLinesEx(quitBtn, 2.0f, (Color){ 220, 90, 90, 255 });
                const char* quitTxt = "QUIT";
                int quitW = MeasureText(quitTxt, 28);
                DrawText(quitTxt, quitBtn.x + (quitBtn.width - quitW) / 2,
                         quitBtn.y + (quitBtn.height - 28) / 2, 28, RAYWHITE);

                if (hoverResume && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    paused = false;
                    DisableCursor();
                }
                if (hoverQuit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    shouldQuit = true;
                }
            EndDrawing();
            continue;
        }

        for (int k = 0; k < WEAPON_COUNT; k++) {
            if (IsKeyPressed(switchKeys[k]) && currentWeapon != k) {
                weapons[currentWeapon].ammoInMag   = w->ammoInMag;
                weapons[currentWeapon].ammoReserve = w->ammoReserve;
                currentWeapon = k;
                w = &weapons[k];
                reloading    = false;
                reloadTimer  = 0.0f;
                fireCooldown = 0.4f;
                switchTimer  = 0.4f;
                break;
            }
        }

        adsActive = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);

        float targetAdsBlend = adsActive ? 1.0f : 0.0f;
        adsBlend += (targetAdsBlend - adsBlend) * (1.0f - expf(-18.0f * dt));

        float baseFov = 70.0f;
        camera.fovy = baseFov + (w->adsFov - baseFov) * adsBlend;

        float sensMult = 1.0f + (w->adsSens - 1.0f) * adsBlend;
        float effSens  = MOUSE_SENS * sensMult;

        Vector2 mouseDelta = GetMouseDelta();
        yaw   -= mouseDelta.x * effSens;
        pitch -= mouseDelta.y * effSens;
        if (pitch >  1.5f) pitch =  1.5f;
        if (pitch < -1.5f) pitch = -1.5f;

        Vector3 flatForward = { sinf(yaw), 0.0f, cosf(yaw) };
        Vector3 right       = { -cosf(yaw), 0.0f, sinf(yaw) };

        Vector3 move = { 0 };
        if (IsKeyDown(KEY_W)) move = Vector3Add(move, flatForward);
        if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, flatForward);
        if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
        if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);

        bool moving = false;
        float moveSpeed = MOVE_SPEED * (1.0f - 0.45f * adsBlend);
        if (Vector3Length(move) > 0.0f) {
            move = Vector3Normalize(move);
            move = Vector3Scale(move, moveSpeed * dt);
            moving = true;
        }

        if (IsKeyPressed(KEY_SPACE) && player.grounded) {
            player.velocity.y = JUMP_SPEED;
            player.grounded = false;
            PlaySound(sfxJump);
        }

        player.velocity.y += GRAVITY * dt;

        Vector3 playerHalf = { PLAYER_RADIUS, PLAYER_EYE_HEIGHT * 0.5f, PLAYER_RADIUS };
        Vector3 playerCenter = {
            player.position.x,
            player.position.y + playerHalf.y,
            player.position.z
        };

        playerCenter.x += move.x;
        for (int i = 0; i < boxCount; i++) {
            if (AABBvsAABB(playerCenter, playerHalf, boxes[i].center, boxes[i].halfExtents)) {
                playerCenter.x -= move.x;
                break;
            }
        }
        player.position.x = playerCenter.x;

        playerCenter.z += move.z;
        for (int i = 0; i < boxCount; i++) {
            if (AABBvsAABB(playerCenter, playerHalf, boxes[i].center, boxes[i].halfExtents)) {
                playerCenter.z -= move.z;
                break;
            }
        }
        player.position.z = playerCenter.z;

        if (player.position.x < -MAP_LIMIT) player.position.x = -MAP_LIMIT;
        if (player.position.x >  MAP_LIMIT) player.position.x =  MAP_LIMIT;
        if (player.position.z < -MAP_LIMIT) player.position.z = -MAP_LIMIT;
        if (player.position.z >  MAP_LIMIT) player.position.z =  MAP_LIMIT;

        player.position.y += player.velocity.y * dt;
        playerCenter.y = player.position.y + playerHalf.y;

        player.grounded = false;
        for (int i = 0; i < boxCount; i++) {
            if (AABBvsAABB(playerCenter, playerHalf, boxes[i].center, boxes[i].halfExtents)) {
                if (player.velocity.y <= 0.0f) {
                    player.position.y = boxes[i].center.y + boxes[i].halfExtents.y;
                    player.velocity.y = 0.0f;
                    player.grounded = true;
                } else {
                    player.position.y = boxes[i].center.y - boxes[i].halfExtents.y - PLAYER_EYE_HEIGHT;
                    player.velocity.y = 0.0f;
                }
                break;
            }
        }

        if (player.position.y <= 0.0f) {
            player.position.y = 0.0f;
            player.velocity.y = 0.0f;
            player.grounded = true;
        }

        UpdatePickups(pickups, MAX_PICKUPS, &player, weapons, WEAPON_COUNT, sfxJump);

        camera.position = (Vector3){
            player.position.x,
            player.position.y + PLAYER_EYE_HEIGHT,
            player.position.z
        };

        Vector3 forward = {
            cosf(pitch) * sinf(yaw),
            sinf(pitch),
            cosf(pitch) * cosf(yaw)
        };
        camera.target = Vector3Add(camera.position, forward);

        fireCooldown   -= dt;
        muzzleTimer    -= dt;
        lastHitTimer   -= dt;
        damageFlash    -= dt;
        hitMarkerTimer -= dt;
        switchTimer    -= dt;

        for (int i = 0; i < MAX_DAMAGE_INDICATORS; i++) {
            if (damageIndicators[i].timer > 0.0f)
                damageIndicators[i].timer -= dt;
        }
        for (int i = 0; i < MAX_DAMAGE_NUMBERS; i++) {
            if (damageNumbers[i].timer > 0.0f)
                damageNumbers[i].timer -= dt;
        }

        if (reloading) {
            reloadTimer -= dt;
            if (reloadTimer <= 0.0f) {
                int need = w->magSize - w->ammoInMag;
                int give = (need < w->ammoReserve) ? need : w->ammoReserve;
                w->ammoInMag   += give;
                w->ammoReserve -= give;
                reloading    = false;
            }
        }

        if (IsKeyPressed(KEY_R) && !reloading && w->ammoInMag < w->magSize && w->ammoReserve > 0) {
            reloading   = true;
            reloadTimer = w->reloadTime;
            SetSoundPitch(sfxReload, 0.95f + RandomFloat() * 0.1f);
            PlaySound(sfxReload);
        }

        bool wantFire = w->autoFire
            ? IsMouseButtonDown(MOUSE_BUTTON_LEFT)
            : IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        if (wantFire && fireCooldown <= 0.0f && !reloading && w->ammoInMag > 0 && switchTimer <= 0.0f) {
            fireCooldown = w->fireRate;
            muzzleTimer  = MUZZLE_FLASH_TIME;
            w->ammoInMag--;

            Sound shot = shotSounds[currentWeapon];
            SetSoundPitch(shot, 0.96f + RandomFloat() * 0.08f);
            PlaySound(shot);

            Ray ray = { camera.position, forward };
            float nearest = 1e9f;
            Vector3 nearestPos = { 0 };
            bool didHit = false;
            int hitEnemy = -1;

            for (int i = 0; i < boxCount; i++) {
                BoundingBox bb = {
                    Vector3Subtract(boxes[i].center, boxes[i].halfExtents),
                    Vector3Add(boxes[i].center, boxes[i].halfExtents)
                };
                RayCollision hit = GetRayCollisionBox(ray, bb);
                if (hit.hit && hit.distance < nearest) {
                    nearest    = hit.distance;
                    nearestPos = hit.point;
                    didHit     = true;
                    hitEnemy   = -1;
                }
            }

            for (int i = 0; i < enemyCount; i++) {
                if (!enemies[i].alive) continue;
                Vector3 eHalf = { 0.35f, 0.9f, 0.35f };
                BoundingBox bb = {
                    Vector3Subtract(enemies[i].position, eHalf),
                    Vector3Add(enemies[i].position, eHalf)
                };
                RayCollision hit = GetRayCollisionBox(ray, bb);
                if (hit.hit && hit.distance < nearest) {
                    nearest    = hit.distance;
                    nearestPos = hit.point;
                    didHit     = true;
                    hitEnemy   = i;
                }
            }

            if (didHit) {
                lastHitPos   = nearestPos;
                lastHitTimer = 0.35f;
            }

            if (hitEnemy >= 0) {
                float headY = enemies[hitEnemy].position.y + 0.72f;
                bool headshot = (nearestPos.y >= headY - 0.16f &&
                                 nearestPos.y <= headY + 0.16f);
                float dmg = headshot ? w->headDamage : w->bodyDamage;

                enemies[hitEnemy].health -= dmg;
                enemies[hitEnemy].hurtTimer = 0.18f;

                bool killed = (enemies[hitEnemy].health <= 0.0f);

                int numType = 0;
                if (killed)         numType = 2;
                else if (headshot)  numType = 1;

                SpawnDamageNumber(damageNumbers, MAX_DAMAGE_NUMBERS,
                                  nearestPos, dmg, numType);

                if (killed) {
                    enemies[hitEnemy].alive = false;
                    enemies[hitEnemy].respawnTimer = ENEMY_RESPAWN;
                    SetSoundPitch(sfxEnemyDie, 0.9f + RandomFloat() * 0.2f);
                    PlaySound(sfxEnemyDie);
                } else {
                    SetSoundPitch(sfxEnemyHurt, 0.9f + RandomFloat() * 0.2f);
                    PlaySound(sfxEnemyHurt);
                }

                if (killed) {
                    hitMarkerType  = 3;
                    hitMarkerTimer = HITMARKER_TIME;
                } else if (headshot) {
                    hitMarkerType  = 2;
                    hitMarkerTimer = HITMARKER_TIME;
                } else {
                    hitMarkerType  = 1;
                    hitMarkerTimer = HITMARKER_TIME;
                }
            } else if (didHit) {
                SetSoundPitch(sfxHit, 0.9f + RandomFloat() * 0.2f);
                PlaySound(sfxHit);
            }

            pitch += w->recoilPitch + RandomFloat() * (w->recoilPitch * 0.6f);
            yaw   += (RandomFloat() - 0.5f) * w->recoilYaw;
            recoilBack += w->recoilBack;
        }

        float decay = 1.0f - expf(-16.0f * dt);
        recoilBack = Lerp(recoilBack, 0.0f, decay);

        float targetSwayX = -mouseDelta.x * 0.0016f;
        float targetSwayY = -mouseDelta.y * 0.0016f;
        sway.x += (targetSwayX - sway.x) * 0.18f;
        sway.y += (targetSwayY - sway.y) * 0.18f;

        float bobX = 0.0f;
        float bobY = 0.0f;
        if (moving && player.grounded) {
            bobTime += dt * moveSpeed;
            bobX = sinf(bobTime * 1.0f) * 0.006f;
            bobY = fabsf(cosf(bobTime * 1.0f)) * 0.006f;

            footstepTimer += dt;
            if (footstepTimer >= 0.42f) {
                footstepTimer = 0.0f;
                SetSoundPitch(sfxFootstep, 0.9f + RandomFloat() * 0.2f);
                PlaySound(sfxFootstep);
            }
        } else {
            bobTime = 0.0f;
            footstepTimer = 0.35f;
        }

        float reloadT = 0.0f;
        if (reloading) {
            reloadT = 1.0f - (reloadTimer / w->reloadTime);
        }

        Vector3 viewOffset = { sway.x + bobX, sway.y + bobY, recoilBack };

        for (int i = 0; i < enemyCount; i++) {
            Enemy* e = &enemies[i];

            if (e->hurtTimer   > 0.0f) e->hurtTimer   -= dt;
            if (e->muzzleFlash > 0.0f) e->muzzleFlash -= dt;
            if (e->shootTimer  > 0.0f) e->shootTimer  -= dt;

            if (!e->alive) {
                e->respawnTimer -= dt;
                if (e->respawnTimer <= 0.0f) {
                    e->alive      = true;
                    e->health     = ENEMY_MAX_HEALTH;
                    e->position   = e->spawnPoint;
                    e->shootTimer = 0.0f;
                    e->aimTimer   = 0.0f;
                }
                continue;
            }

            Vector3 toPlayer = {
                player.position.x - e->position.x,
                0.0f,
                player.position.z - e->position.z
            };
            float dist = Vector3Length(toPlayer);

            bool losBlocked = false;
            if (dist > 0.001f) {
                Vector3 dir = Vector3Scale(toPlayer, 1.0f / dist);
                Ray losRay = { e->position, dir };
                for (int j = 0; j < boxCount; j++) {
                    BoundingBox bb = {
                        Vector3Subtract(boxes[j].center, boxes[j].halfExtents),
                        Vector3Add(boxes[j].center, boxes[j].halfExtents)
                    };
                    RayCollision rc = GetRayCollisionBox(losRay, bb);
                    if (rc.hit && rc.distance < dist) { losBlocked = true; break; }
                }
            }

            bool inRange = (dist <= ENEMY_SHOOT_RANGE) && !losBlocked;

            if (!inRange) {
                e->aimTimer = 0.0f;

                if (dist > 0.01f) {
                    Vector3 dir = Vector3Normalize(toPlayer);
                    Vector3 enemyHalf = { 0.35f, 0.9f, 0.35f };

                    Vector3 tryPos = e->position;
                    tryPos.x += dir.x * ENEMY_SPEED * dt;
                    bool blockedX = false;
                    for (int j = 0; j < boxCount; j++) {
                        if (AABBvsAABB(tryPos, enemyHalf, boxes[j].center, boxes[j].halfExtents)) {
                            blockedX = true; break;
                        }
                    }
                    if (!blockedX) e->position.x = tryPos.x;

                    tryPos = e->position;
                    tryPos.z += dir.z * ENEMY_SPEED * dt;
                    bool blockedZ = false;
                    for (int j = 0; j < boxCount; j++) {
                        if (AABBvsAABB(tryPos, enemyHalf, boxes[j].center, boxes[j].halfExtents)) {
                            blockedZ = true; break;
                        }
                    }
                    if (!blockedZ) e->position.z = tryPos.z;
                }
            } else {
                if (e->aimTimer < ENEMY_AIM_TIME) {
                    e->aimTimer += dt;
                } else if (e->shootTimer <= 0.0f) {
                    e->shootTimer  = ENEMY_SHOOT_CD + RandomFloat() * 0.35f;
                    e->muzzleFlash = 0.06f;

                    float accuracy = 1.0f - (dist / ENEMY_SHOOT_RANGE) * 0.35f;
                    if (RandomFloat() < accuracy) {
                        player.health -= ENEMY_SHOOT_DMG;
                        damageFlash = 0.35f;

                        SpawnDamageIndicator(damageIndicators, MAX_DAMAGE_INDICATORS,
                                             e->position);

                        SetSoundPitch(sfxPlayerHurt, 0.85f + RandomFloat() * 0.15f);
                        PlaySound(sfxPlayerHurt);
                    }
                }
            }
        }

        if (player.health <= 0.0f) {
            player.health = PLAYER_MAX_HEALTH;
            player.position = (Vector3){ 0.0f, 0.0f, 0.0f };
            player.velocity = (Vector3){ 0 };
            for (int i = 0; i < WEAPON_COUNT; i++) {
                weapons[i].ammoInMag   = weapons[i].magSize;
                weapons[i].ammoReserve = weapons[i].reserveMax;
            }
            reloading   = false;
            damageFlash = 0.5f;
        }

        BeginDrawing();
            ClearBackground((Color){ 135, 206, 235, 255 });

            BeginMode3D(camera);
                DrawPlane((Vector3){ 0 }, (Vector2){ GROUND_SIZE, GROUND_SIZE },
                          (Color){ 185, 165, 125, 255 });

                for (int i = 0; i < boxCount; i++) {
                    DrawCube(boxes[i].center,
                             boxes[i].halfExtents.x * 2.0f,
                             boxes[i].halfExtents.y * 2.0f,
                             boxes[i].halfExtents.z * 2.0f,
                             boxes[i].color);
                    DrawCubeWires(boxes[i].center,
                                  boxes[i].halfExtents.x * 2.0f,
                                  boxes[i].halfExtents.y * 2.0f,
                                  boxes[i].halfExtents.z * 2.0f, BLACK);
                }

                DrawPickups(pickups, MAX_PICKUPS, GetTime());

                for (int i = 0; i < enemyCount; i++) {
                    if (!enemies[i].alive) continue;
                    DrawEnemy(&enemies[i], player.position);
                }

                if (lastHitTimer > 0.0f) {
                    float t = lastHitTimer / 0.35f;
                    float pulse = 0.05f + 0.05f * sinf(t * 40.0f);
                    DrawSphere(lastHitPos, pulse + 0.02f, (Color){ 255, 80, 40, 255 });
                    DrawSphereWires(lastHitPos, pulse + 0.12f, 8, 8,
                                    (Color){ 255, 200, 100, (unsigned char)(255 * t) });
                }
            EndMode3D();

            bool scopedNow = w->hasScope && adsBlend > 0.7f;

            if (!scopedNow) {
                int cx = SCREEN_WIDTH  / 2;
                int cy = SCREEN_HEIGHT / 2;
                DrawLine(cx - 10, cy, cx + 10, cy, RAYWHITE);
                DrawLine(cx, cy - 10, cx, cy + 10, RAYWHITE);

                BeginTextureMode(viewRT);
                    ClearBackground(BLANK);
                    BeginMode3D(viewCam);
                        DrawWeaponViewmodel(currentWeapon, viewOffset, muzzleTimer, reloadT);
                    EndMode3D();
                EndTextureMode();

                DrawTexturePro(viewRT.texture,
                    (Rectangle){ 0, 0, (float)viewRT.texture.width, -(float)viewRT.texture.height },
                    (Rectangle){ 0, 0, (float)SCREEN_WIDTH, (float)SCREEN_HEIGHT },
                    (Vector2){ 0, 0 }, 0.0f, WHITE);
            }

            if (scopedNow) {
                DrawScopeOverlay();
            }

            if (damageFlash > 0.0f) {
                unsigned char a = (unsigned char)(160 * (damageFlash / 0.5f));
                DrawRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (Color){ 200, 0, 0, a });
            }

            DrawDamageIndicators(damageIndicators, MAX_DAMAGE_INDICATORS, &player, yaw);
            DrawDamageNumbers(damageNumbers, MAX_DAMAGE_NUMBERS, camera);
            DrawHitMarker(hitMarkerTimer, hitMarkerType);
            DrawHUD(&player, w, reloading);

            const char* helpTxt = "1 PISTOL  2 AKR  3 SNIPER  |  RMB ADS  |  ESC MENU";
            DrawText(helpTxt, 20, SCREEN_HEIGHT - 40, 18, (Color){ 220, 220, 220, 180 });

            DrawFPS(SCREEN_WIDTH - 90, 10);
        EndDrawing();
    }

    UnloadSound(sfxShot);
    UnloadSound(sfxShotAKR);
    UnloadSound(sfxShotSniper);
    UnloadSound(sfxReload);
    UnloadSound(sfxFootstep);
    UnloadSound(sfxJump);
    UnloadSound(sfxHit);
    UnloadSound(sfxEnemyHurt);
    UnloadSound(sfxEnemyDie);
    UnloadSound(sfxPlayerHurt);
    UnloadRenderTexture(viewRT);
    CloseAudioDevice();
    EnableCursor();
    CloseWindow();
    return 0;
}
