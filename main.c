#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psprtc.h>
#include <pspaudio.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

PSP_MODULE_INFO("PSP 3D Shooter", 0, 1, 6);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define printf pspDebugScreenPrintf
#define BUF_WIDTH 512
#define SCR_WIDTH 480
#define SCR_HEIGHT 272

#define MAX_BULLETS 30
#define MAX_ENEMIES 15
#define MAX_PARTICLES 100
#define MAX_ENEMY_BULLETS 20

// Enemy types
typedef enum {
    ENEMY_BASIC,     // Flies straight toward player
    ENEMY_ZIGZAG,    // Moves side-to-side while approaching
    ENEMY_CIRCLER,   // Orbits around player position
    ENEMY_SHOOTER,   // Fires projectiles at player
    ENEMY_TANK,      // Slower, more health, worth more points
    ENEMY_SPEEDSTER  // Fast, erratic movement, less health
} EnemyType;

static unsigned int __attribute__((aligned(16))) list[262144];

struct Vertex {
    unsigned int color;
    float x, y, z;
};

typedef struct {
    float x, y, z;
    int health;
} Player;

typedef struct {
    float x, y, z;
    int active;
} Bullet;

typedef struct {
    float x, y, z;
    int active;
    float angle;
    EnemyType type;
    int health;
    int shootTimer;
    float moveTimer;  // For timing movement patterns
} Enemy;

typedef struct {
    float x, y, z, vx, vy, vz;
    int life, active;
    unsigned int color;
} Particle;

typedef struct {
    float x, y, z;
    int active;
} EnemyBullet;

typedef struct {
    Player player;
    Bullet bullets[MAX_BULLETS];
    Enemy enemies[MAX_ENEMIES];
    EnemyBullet enemyBullets[MAX_ENEMY_BULLETS];
    Particle particles[MAX_PARTICLES];
    int score, enemyTimer, shootTimer;
    float time;
    int gameOver;
} Game;

// Audio system
typedef struct {
    short* data;
    int samples;
    int position;
    int playing;
} Sound;

static Sound shootSound = {0};
static int audioChannel = -1;
static volatile int audioRunning = 1;

int exit_callback(int arg1, int arg2, void *common) {
    sceKernelExitGame();
    return 0;
}

int CallbackThread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int SetupCallbacks(void) {
    int thid = sceKernelCreateThread("update_thread", CallbackThread, 0x11, 0xFA0, 0, 0);
    if(thid >= 0) sceKernelStartThread(thid, 0, 0);
    return thid;
}

// Load WAV file (PCM 16-bit stereo 44100Hz)
int loadWav(const char* filename, Sound* sound) {
    FILE* f = fopen(filename, "rb");
    if (!f) return -1;

    // Get file size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    // Read entire file
    unsigned char* fileData = (unsigned char*)malloc(fileSize);
    if (!fileData) { fclose(f); return -1; }
    fread(fileData, 1, fileSize, f);
    fclose(f);

    // Find "data" chunk (skip RIFF header)
    int dataOffset = 12;
    while (dataOffset < fileSize - 8) {
        if (fileData[dataOffset] == 'd' && fileData[dataOffset+1] == 'a' &&
            fileData[dataOffset+2] == 't' && fileData[dataOffset+3] == 'a') {
            unsigned int dataSize = *(unsigned int*)&fileData[dataOffset + 4];
            sound->data = (short*)malloc(dataSize);
            if (!sound->data) { free(fileData); return -1; }
            memcpy(sound->data, &fileData[dataOffset + 8], dataSize);
            sound->samples = dataSize / 4;  // 16-bit stereo = 4 bytes per sample
            sound->position = 0;
            sound->playing = 0;
            free(fileData);
            return 0;
        }
        unsigned int chunkSize = *(unsigned int*)&fileData[dataOffset + 4];
        dataOffset += 8 + chunkSize;
    }

    free(fileData);
    return -1;
}

// Audio thread
int audioThread(SceSize args, void* argp) {
    short buffer[2048 * 2];  // Stereo buffer

    while (audioRunning) {
        memset(buffer, 0, sizeof(buffer));

        if (shootSound.playing && shootSound.data) {
            int remaining = shootSound.samples - shootSound.position;
            int toPlay = (remaining > 2048) ? 2048 : remaining;

            if (toPlay > 0) {
                memcpy(buffer, &shootSound.data[shootSound.position * 2], toPlay * 4);
                shootSound.position += toPlay;
            }

            if (shootSound.position >= shootSound.samples) {
                shootSound.playing = 0;
                shootSound.position = 0;
            }
        }

        sceAudioOutputBlocking(audioChannel, PSP_AUDIO_VOLUME_MAX, buffer);
    }
    return 0;
}

// Initialize audio system
void initAudio(void) {
    audioChannel = sceAudioChReserve(-1, 2048, PSP_AUDIO_FORMAT_STEREO);
    if (audioChannel < 0) return;

    loadWav("shoot_1.wav", &shootSound);

    int thid = sceKernelCreateThread("audio_thread", audioThread, 0x12, 0x10000, 0, 0);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, 0);
    }
}

// Play shoot sound
void playShootSound(void) {
    shootSound.position = 0;
    shootSound.playing = 1;
}

int randInt(int max) {
    static unsigned int seed = 12345;
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed % max;
}

void initGame(Game* g) {
    int i;
    g->player.x = 0; g->player.y = 0; g->player.z = 0; g->player.health = 3;
    for(i = 0; i < MAX_BULLETS; i++) g->bullets[i].active = 0;
    for(i = 0; i < MAX_ENEMIES; i++) g->enemies[i].active = 0;
    for(i = 0; i < MAX_ENEMY_BULLETS; i++) g->enemyBullets[i].active = 0;
    for(i = 0; i < MAX_PARTICLES; i++) g->particles[i].active = 0;
    g->score = 0; g->enemyTimer = 0; g->shootTimer = 0; g->time = 0; g->gameOver = 0;
}

void shootBullet(Game* g) {
    if(g->shootTimer > 0) return;
    for(int i = 0; i < MAX_BULLETS; i++) {
        if(!g->bullets[i].active) {
            g->bullets[i].x = g->player.x;
            g->bullets[i].y = g->player.y;
            g->bullets[i].z = g->player.z - 1;
            g->bullets[i].active = 1;
            g->shootTimer = 8;
            playShootSound();
            break;
        }
    }
}

void spawnEnemy(Game* g) {
    for(int i = 0; i < MAX_ENEMIES; i++) {
        if(!g->enemies[i].active) {
            g->enemies[i].x = (randInt(600) - 300) / 100.0f;
            g->enemies[i].y = (randInt(200) - 100) / 100.0f;
            g->enemies[i].z = -10;
            g->enemies[i].angle = 0;
            g->enemies[i].moveTimer = 0;
            g->enemies[i].shootTimer = 0;

            // Randomly assign enemy type
            g->enemies[i].type = (EnemyType)(randInt(6));

            // Set health based on type
            switch(g->enemies[i].type) {
                case ENEMY_TANK:
                    g->enemies[i].health = 3;
                    break;
                case ENEMY_SPEEDSTER:
                    g->enemies[i].health = 1;
                    break;
                default:
                    g->enemies[i].health = 2;
                    break;
            }

            g->enemies[i].active = 1;
            break;
        }
    }
}

void explode(Game* g, float x, float y, float z) {
    unsigned int colors[] = {0xFF0000FF, 0xFF0088FF, 0xFF00FFFF};
    for(int i = 0, n = 0; i < MAX_PARTICLES && n < 15; i++) {
        if(!g->particles[i].active) {
            g->particles[i].x = x; g->particles[i].y = y; g->particles[i].z = z;
            g->particles[i].vx = (randInt(200) - 100) / 200.0f;
            g->particles[i].vy = (randInt(200) - 100) / 200.0f;
            g->particles[i].vz = (randInt(200) - 100) / 200.0f;
            g->particles[i].life = 30 + randInt(20);
            g->particles[i].color = colors[randInt(3)];
            g->particles[i].active = 1;
            n++;
        }
    }
}

void shootEnemyBullet(Game* g, float x, float y, float z) {
    for(int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if(!g->enemyBullets[i].active) {
            g->enemyBullets[i].x = x;
            g->enemyBullets[i].y = y;
            g->enemyBullets[i].z = z;
            g->enemyBullets[i].active = 1;
            break;
        }
    }
}

void updateEnemy(Enemy* e, Game* g, float baseSpeed) {
    if(e->shootTimer > 0) e->shootTimer--;
    e->moveTimer += 0.05f;

    switch(e->type) {
        case ENEMY_BASIC:
            // Flies straight toward player
            e->z += baseSpeed; // 3d?
            e->angle += 0.05f;
            break;

        case ENEMY_ZIGZAG:
            // Moves side-to-side while approaching
            e->z += baseSpeed;
            e->x += sinf(e->moveTimer * 3.0f) * 0.05f;
            e->angle += 0.08f;
            break;

        case ENEMY_CIRCLER:
            // Orbits around player position while slowly approaching
            e->z += baseSpeed * 0.7f;
            {
                float radius = 2.0f;
                float targetX = g->player.x + cosf(e->moveTimer) * radius;
                float targetY = g->player.y + sinf(e->moveTimer) * radius;
                e->x += (targetX - e->x) * 0.02f;
                e->y += (targetY - e->y) * 0.02f;
            }
            e->angle += 0.1f;
            break;

        case ENEMY_SHOOTER:
            // Slower movement, fires projectiles
            e->z += baseSpeed * 0.6f;
            e->angle += 0.05f;
            // Shoot at player every 60 frames
            if(e->shootTimer <= 0 && e->z > -8 && e->z < 0) {
                shootEnemyBullet(g, e->x, e->y, e->z);
                e->shootTimer = 60;
            }
            break;

        case ENEMY_TANK:
            // Slow but tough
            e->z += baseSpeed * 0.5f;
            e->angle += 0.03f;
            break;

        case ENEMY_SPEEDSTER:
            // Fast erratic movement
            e->z += baseSpeed * 1.5f;
            e->x += sinf(e->moveTimer * 5.0f) * 0.08f;
            e->y += cosf(e->moveTimer * 4.0f) * 0.06f;
            e->angle += 0.15f;
            break;
    }
}

int getEnemyPoints(EnemyType type) {
    switch(type) {
        case ENEMY_TANK: return 30;
        case ENEMY_SHOOTER: return 25;
        case ENEMY_CIRCLER: return 20;
        case ENEMY_SPEEDSTER: return 15;
        case ENEMY_ZIGZAG: return 12;
        case ENEMY_BASIC:
        default: return 10;
    }
}

void updateGame(Game* g) {
    if(g->shootTimer > 0) g->shootTimer--;

    // Update bullets
    for(int i = 0; i < MAX_BULLETS; i++) {
        if(g->bullets[i].active) {
            g->bullets[i].z -= 0.3f;
            if(g->bullets[i].z < -15) g->bullets[i].active = 0;
        }
    }

    // Update enemy bullets
    for(int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if(g->enemyBullets[i].active) {
            g->enemyBullets[i].z += 0.15f;
            if(g->enemyBullets[i].z > 5) g->enemyBullets[i].active = 0;
        }
    }

    // Update enemies (faster as score increases)
    float enemySpeed = 0.025f + (g->score / 5000.0f);
    if(enemySpeed > 0.06f) enemySpeed = 0.06f;
    for(int i = 0; i < MAX_ENEMIES; i++) {
        if(g->enemies[i].active) {
            updateEnemy(&g->enemies[i], g, enemySpeed);
            if(g->enemies[i].z > 5) g->enemies[i].active = 0;
        }
    }

    // Update particles
    for(int i = 0; i < MAX_PARTICLES; i++) {
        if(g->particles[i].active) {
            g->particles[i].x += g->particles[i].vx;
            g->particles[i].y += g->particles[i].vy;
            g->particles[i].z += g->particles[i].vz;
            g->particles[i].vy -= 0.01f;
            if(--g->particles[i].life <= 0) g->particles[i].active = 0;
        }
    }

    // Bullet-Enemy collision (with health system)
    for(int i = 0; i < MAX_BULLETS; i++) {
        if(g->bullets[i].active) {
            for(int j = 0; j < MAX_ENEMIES; j++) {
                if(g->enemies[j].active) {
                    float dx = g->bullets[i].x - g->enemies[j].x;
                    float dy = g->bullets[i].y - g->enemies[j].y;
                    float dz = g->bullets[i].z - g->enemies[j].z;
                    if(dx*dx + dy*dy + dz*dz < 0.5f) {
                        g->bullets[i].active = 0;
                        g->enemies[j].health--;
                        if(g->enemies[j].health <= 0) {
                            g->enemies[j].active = 0;
                            g->score += getEnemyPoints(g->enemies[j].type);
                            explode(g, g->enemies[j].x, g->enemies[j].y, g->enemies[j].z);
                        }
                        break;
                    }
                }
            }
        }
    }

    // Enemy bullet-Player collision
    for(int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if(g->enemyBullets[i].active) {
            float dx = g->player.x - g->enemyBullets[i].x;
            float dy = g->player.y - g->enemyBullets[i].y;
            float dz = g->player.z - g->enemyBullets[i].z;
            if(dx*dx + dy*dy + dz*dz < 0.4f) {
                g->enemyBullets[i].active = 0;
                g->player.health--;
                if(g->player.health <= 0) {
                    g->gameOver = 1;
                }
            }
        }
    }

    // Player-Enemy collision
    for(int i = 0; i < MAX_ENEMIES; i++) {
        if(g->enemies[i].active) {
            float dx = g->player.x - g->enemies[i].x;
            float dy = g->player.y - g->enemies[i].y;
            float dz = g->player.z - g->enemies[i].z;
            if(dx*dx + dy*dy + dz*dz < 0.8f) {
                g->enemies[i].active = 0;
                g->player.health--;
                explode(g, g->enemies[i].x, g->enemies[i].y, g->enemies[i].z);
                if(g->player.health <= 0) {
                    g->gameOver = 1;
                }
            }
        }
    }

    g->time += 0.016f;
}

void countEntities(Game* g, int* enemies, int* bullets, int* eBullets, int* particles) {
    *enemies = *bullets = *eBullets = *particles = 0;
    for(int i = 0; i < MAX_ENEMIES; i++) if(g->enemies[i].active) (*enemies)++;
    for(int i = 0; i < MAX_BULLETS; i++) if(g->bullets[i].active) (*bullets)++;
    for(int i = 0; i < MAX_ENEMY_BULLETS; i++) if(g->enemyBullets[i].active) (*eBullets)++;
    for(int i = 0; i < MAX_PARTICLES; i++) if(g->particles[i].active) (*particles)++;
}

void drawCube(float x, float y, float z, float size, unsigned int color) {
    struct Vertex* v = (struct Vertex*)sceGuGetMemory(36 * sizeof(struct Vertex));
    float s = size;

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 pos = {x, y, z};
    sceGumTranslate(&pos);

    int idx = 0;
    // Front
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = s;
    // Back
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = -s;
    // Top
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = -s;
    // Bottom
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = s;
    // Left
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = -s; v[idx].y = s; v[idx++].z = -s;
    // Right
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = -s; v[idx++].z = s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = -s;
    v[idx].color = color; v[idx].x = s; v[idx].y = s; v[idx++].z = s;

    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 36, 0, v);
}

void drawTerrain(float time) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    for(int i = -8; i < 8; i++) {
        for(int j = -8; j < 8; j++) {
            struct Vertex* v = (struct Vertex*)sceGuGetMemory(6 * sizeof(struct Vertex));

            float x1 = i * 2.0f, x2 = (i+1) * 2.0f;
            float z1 = j * 2.0f + fmodf(time * 2, 2.0f);
            float z2 = (j+1) * 2.0f + fmodf(time * 2, 2.0f);
            float y = -2.0f;
            float h = sinf(x1 * 0.3f + z1 * 0.3f + time) * 0.3f;

            v[0].color = 0xFF00CC00; v[0].x = x1; v[0].y = y+h; v[0].z = z1;
            v[1].color = 0xFF00CC00; v[1].x = x2; v[1].y = y+h; v[1].z = z1;
            v[2].color = 0xFF00CC00; v[2].x = x1; v[2].y = y+h; v[2].z = z2;
            v[3].color = 0xFF00CC00; v[3].x = x2; v[3].y = y+h; v[3].z = z1;
            v[4].color = 0xFF00CC00; v[4].x = x2; v[4].y = y+h; v[4].z = z2;
            v[5].color = 0xFF00CC00; v[5].x = x1; v[5].y = y+h; v[5].z = z2;

            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 6, 0, v);
        }
    }
}

void drawPlayer(Player* p) {
    // Body
    drawCube(p->x, p->y, p->z, 0.25f, 0xFFDDDDDD);

    // Wings
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 pos = {p->x, p->y, p->z};
    sceGumTranslate(&pos);

    struct Vertex* v = (struct Vertex*)sceGuGetMemory(6 * sizeof(struct Vertex));
    v[0].color = 0xFF0080FF; v[0].x = -0.6f; v[0].y = 0; v[0].z = 0;
    v[1].color = 0xFF0080FF; v[1].x = -0.2f; v[1].y = 0; v[1].z = -0.2f;
    v[2].color = 0xFF0080FF; v[2].x = -0.2f; v[2].y = 0; v[2].z = 0.2f;
    v[3].color = 0xFF0080FF; v[3].x = 0.6f; v[3].y = 0; v[3].z = 0;
    v[4].color = 0xFF0080FF; v[4].x = 0.2f; v[4].y = 0; v[4].z = 0.2f;
    v[5].color = 0xFF0080FF; v[5].x = 0.2f; v[5].y = 0; v[5].z = -0.2f;

    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 6, 0, v);
}

void drawEnemy(Enemy* e) {
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 pos = {e->x, e->y, e->z};
    sceGumTranslate(&pos);
    sceGumRotateY(e->angle);

    struct Vertex* v;
    unsigned int color;

    switch(e->type) {
        case ENEMY_BASIC:
            // Red triangle (original)
            color = 0xFF0000FF;
            v = (struct Vertex*)sceGuGetMemory(3 * sizeof(struct Vertex));
            v[0].color = color; v[0].x = 0; v[0].y = 0.4f; v[0].z = 0;
            v[1].color = color; v[1].x = -0.4f; v[1].y = -0.4f; v[1].z = 0;
            v[2].color = color; v[2].x = 0.4f; v[2].y = -0.4f; v[2].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 3, 0, v);
            break;

        case ENEMY_ZIGZAG:
            // Purple diamond
            color = 0xFFFF00FF;
            v = (struct Vertex*)sceGuGetMemory(6 * sizeof(struct Vertex));
            v[0].color = color; v[0].x = 0; v[0].y = 0.4f; v[0].z = 0;
            v[1].color = color; v[1].x = -0.3f; v[1].y = 0; v[1].z = 0;
            v[2].color = color; v[2].x = 0; v[2].y = -0.4f; v[2].z = 0;
            v[3].color = color; v[3].x = 0; v[3].y = 0.4f; v[3].z = 0;
            v[4].color = color; v[4].x = 0; v[4].y = -0.4f; v[4].z = 0;
            v[5].color = color; v[5].x = 0.3f; v[5].y = 0; v[5].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 6, 0, v);
            break;

        case ENEMY_CIRCLER:
            // Green spinning X shape
            color = 0xFF00FF00;
            v = (struct Vertex*)sceGuGetMemory(12 * sizeof(struct Vertex));
            // First diagonal line
            v[0].color = color; v[0].x = -0.4f; v[0].y = -0.4f; v[0].z = 0;
            v[1].color = color; v[1].x = -0.2f; v[1].y = -0.2f; v[1].z = 0;
            v[2].color = color; v[2].x = 0.4f; v[2].y = 0.4f; v[2].z = 0;
            v[3].color = color; v[3].x = -0.2f; v[3].y = -0.2f; v[3].z = 0;
            v[4].color = color; v[4].x = 0.4f; v[4].y = 0.4f; v[4].z = 0;
            v[5].color = color; v[5].x = 0.2f; v[5].y = 0.2f; v[5].z = 0;
            // Second diagonal line
            v[6].color = color; v[6].x = 0.4f; v[6].y = -0.4f; v[6].z = 0;
            v[7].color = color; v[7].x = 0.2f; v[7].y = -0.2f; v[7].z = 0;
            v[8].color = color; v[8].x = -0.4f; v[8].y = 0.4f; v[8].z = 0;
            v[9].color = color; v[9].x = 0.2f; v[9].y = -0.2f; v[9].z = 0;
            v[10].color = color; v[10].x = -0.4f; v[10].y = 0.4f; v[10].z = 0;
            v[11].color = color; v[11].x = -0.2f; v[11].y = 0.2f; v[11].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 12, 0, v);
            break;

        case ENEMY_SHOOTER:
            // Orange square with center
            color = 0xFF0088FF;
            v = (struct Vertex*)sceGuGetMemory(12 * sizeof(struct Vertex));
            // Outer square
            v[0].color = color; v[0].x = -0.3f; v[0].y = 0.3f; v[0].z = 0;
            v[1].color = color; v[1].x = 0.3f; v[1].y = 0.3f; v[1].z = 0;
            v[2].color = color; v[2].x = 0.3f; v[2].y = -0.3f; v[2].z = 0;
            v[3].color = color; v[3].x = -0.3f; v[3].y = 0.3f; v[3].z = 0;
            v[4].color = color; v[4].x = 0.3f; v[4].y = -0.3f; v[4].z = 0;
            v[5].color = color; v[5].x = -0.3f; v[5].y = -0.3f; v[5].z = 0;
            // Center triangle
            v[6].color = 0xFFFFFFFF; v[6].x = 0; v[6].y = 0.2f; v[6].z = 0;
            v[7].color = 0xFFFFFFFF; v[7].x = -0.15f; v[7].y = -0.1f; v[7].z = 0;
            v[8].color = 0xFFFFFFFF; v[8].x = 0.15f; v[8].y = -0.1f; v[8].z = 0;
            // Small dot
            v[9].color = 0xFFFFFFFF; v[9].x = 0; v[9].y = 0; v[9].z = 0.05f;
            v[10].color = 0xFFFFFFFF; v[10].x = 0.05f; v[10].y = 0; v[10].z = 0;
            v[11].color = 0xFFFFFFFF; v[11].x = 0; v[11].y = 0.05f; v[11].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 12, 0, v);
            break;

        case ENEMY_TANK:
            // Large blue hexagon
            color = 0xFFFFAA00;
            v = (struct Vertex*)sceGuGetMemory(12 * sizeof(struct Vertex));
            v[0].color = color; v[0].x = 0; v[0].y = 0; v[0].z = 0;
            v[1].color = color; v[1].x = 0; v[1].y = 0.5f; v[1].z = 0;
            v[2].color = color; v[2].x = 0.4f; v[2].y = 0.25f; v[2].z = 0;
            v[3].color = color; v[3].x = 0; v[3].y = 0; v[3].z = 0;
            v[4].color = color; v[4].x = 0.4f; v[4].y = 0.25f; v[4].z = 0;
            v[5].color = color; v[5].x = 0.4f; v[5].y = -0.25f; v[5].z = 0;
            v[6].color = color; v[6].x = 0; v[6].y = 0; v[6].z = 0;
            v[7].color = color; v[7].x = 0.4f; v[7].y = -0.25f; v[7].z = 0;
            v[8].color = color; v[8].x = 0; v[8].y = -0.5f; v[8].z = 0;
            v[9].color = color; v[9].x = 0; v[9].y = 0; v[9].z = 0;
            v[10].color = color; v[10].x = 0; v[10].y = -0.5f; v[10].z = 0;
            v[11].color = color; v[11].x = -0.4f; v[11].y = -0.25f; v[11].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 12, 0, v);
            // Mirror
            v = (struct Vertex*)sceGuGetMemory(6 * sizeof(struct Vertex));
            v[0].color = color; v[0].x = 0; v[0].y = 0; v[0].z = 0;
            v[1].color = color; v[1].x = -0.4f; v[1].y = -0.25f; v[1].z = 0;
            v[2].color = color; v[2].x = -0.4f; v[2].y = 0.25f; v[2].z = 0;
            v[3].color = color; v[3].x = 0; v[3].y = 0; v[3].z = 0;
            v[4].color = color; v[4].x = -0.4f; v[4].y = 0.25f; v[4].z = 0;
            v[5].color = color; v[5].x = 0; v[5].y = 0.5f; v[5].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 6, 0, v);
            break;

        case ENEMY_SPEEDSTER:
            // Small yellow star
            color = 0xFF00FFFF;
            v = (struct Vertex*)sceGuGetMemory(9 * sizeof(struct Vertex));
            // Three triangles forming a star
            v[0].color = color; v[0].x = 0; v[0].y = 0.35f; v[0].z = 0;
            v[1].color = color; v[1].x = -0.1f; v[1].y = 0.05f; v[1].z = 0;
            v[2].color = color; v[2].x = 0.1f; v[2].y = 0.05f; v[2].z = 0;
            v[3].color = color; v[3].x = -0.3f; v[3].y = -0.2f; v[3].z = 0;
            v[4].color = color; v[4].x = -0.05f; v[4].y = -0.05f; v[4].z = 0;
            v[5].color = color; v[5].x = 0; v[5].y = 0; v[5].z = 0;
            v[6].color = color; v[6].x = 0.3f; v[6].y = -0.2f; v[6].z = 0;
            v[7].color = color; v[7].x = 0; v[7].y = 0; v[7].z = 0;
            v[8].color = color; v[8].x = 0.05f; v[8].y = -0.05f; v[8].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 9, 0, v);
            break;
    }
}

int main(void) {
    SetupCallbacks();
    initAudio();

    // Init GU first, then debug screen
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_WIDTH);
    sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void*)0x88000, BUF_WIDTH);
    sceGuDepthBuffer((void*)0x110000, BUF_WIDTH);
    sceGuOffset(2048 - (SCR_WIDTH/2), 2048 - (SCR_HEIGHT/2));
    sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);
    sceGuDepthRange(0, 65535);
    sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_LEQUAL);
    sceGuDisable(GU_DEPTH_TEST);  // TEMP: disable depth test for debugging
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    // Init debug screen after GU setup
    pspDebugScreenInit();

    Game game;
    initGame(&game);

    SceCtrlData pad, oldPad;
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    memset(&oldPad, 0, sizeof(oldPad));

    // FPS timing variables
    u64 lastTick, currentTick;
    float fps = 0.0f;
    u32 tickResolution = sceRtcGetTickResolution();
    sceRtcGetCurrentTick(&lastTick);

    while(1) {
        // Calculate FPS
        sceRtcGetCurrentTick(&currentTick);
        float deltaTime = (currentTick - lastTick) / (float)tickResolution;
        if(deltaTime > 0) fps = 1.0f / deltaTime;
        lastTick = currentTick;

        sceCtrlReadBufferPositive(&pad, 1);
        if(pad.Buttons & PSP_CTRL_START) break;

        if(game.gameOver) {
            // Game Over - only allow restart
            if((pad.Buttons & PSP_CTRL_CROSS) && !(oldPad.Buttons & PSP_CTRL_CROSS)) {
                initGame(&game);
            }
        } else {
            // Normal gameplay
            // Input
            if(pad.Buttons & PSP_CTRL_UP && game.player.y < 1.5f) game.player.y += 0.06f;
            if(pad.Buttons & PSP_CTRL_DOWN && game.player.y > -1.5f) game.player.y -= 0.06f;
            if(pad.Buttons & PSP_CTRL_LEFT && game.player.x > -3.0f) game.player.x -= 0.08f;
            if(pad.Buttons & PSP_CTRL_RIGHT && game.player.x < 3.0f) game.player.x += 0.08f;
            if((pad.Buttons & PSP_CTRL_CROSS) && !(oldPad.Buttons & PSP_CTRL_CROSS)) shootBullet(&game);

            // Spawn enemies (faster as score increases)
            int spawnRate = 80 - (game.score / 50);
            if(spawnRate < 30) spawnRate = 30;
            if(++game.enemyTimer > spawnRate) {
                spawnEnemy(&game);
                game.enemyTimer = 0;
            }

            updateGame(&game);
        }

        // Render
        sceGuStart(GU_DIRECT, list);
        sceGuClearColor(0xFFFFE0C0);
        sceGuClearDepth(65535);
        sceGuClear(GU_COLOR_BUFFER_BIT|GU_DEPTH_BUFFER_BIT);

        // DEBUG: Draw a simple 2D red triangle to test if GU works at all
        {
            sceGumMatrixMode(GU_PROJECTION);
            sceGumLoadIdentity();
            sceGumOrtho(0, 480, 272, 0, -1, 1);
            sceGumMatrixMode(GU_VIEW);
            sceGumLoadIdentity();
            sceGumMatrixMode(GU_MODEL);
            sceGumLoadIdentity();

            struct Vertex* v = (struct Vertex*)sceGuGetMemory(3 * sizeof(struct Vertex));
            v[0].color = 0xFF0000FF; v[0].x = 240; v[0].y = 50;  v[0].z = 0;
            v[1].color = 0xFF0000FF; v[1].x = 340; v[1].y = 150; v[1].z = 0;
            v[2].color = 0xFF0000FF; v[2].x = 140; v[2].y = 150; v[2].z = 0;
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D, 3, 0, v);
        }

        // Setup 3D
        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(75.0f, 16.0f/9.0f, 0.5f, 1000.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        ScePspFVector3 eye = {game.player.x, game.player.y + 1.5f, game.player.z + 3.5f};
        ScePspFVector3 center = {game.player.x, game.player.y, game.player.z - 2};
        ScePspFVector3 up = {0, 1, 0};
        sceGumLookAt(&eye, &center, &up);

        // Draw scene
        drawTerrain(game.time);
        drawPlayer(&game.player);

        for(int i = 0; i < MAX_BULLETS; i++)
            if(game.bullets[i].active) drawCube(game.bullets[i].x, game.bullets[i].y, game.bullets[i].z, 0.08f, 0xFF00FFFF);

        for(int i = 0; i < MAX_ENEMIES; i++) {
            if(game.enemies[i].active) {
                drawEnemy(&game.enemies[i]);
            }
        }

        // Draw enemy bullets
        for(int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if(game.enemyBullets[i].active) {
                drawCube(game.enemyBullets[i].x, game.enemyBullets[i].y, game.enemyBullets[i].z, 0.08f, 0xFFFF0000);
            }
        }

        for(int i = 0; i < MAX_PARTICLES; i++)
            if(game.particles[i].active)
                drawCube(game.particles[i].x, game.particles[i].y, game.particles[i].z, 0.06f, game.particles[i].color);

        sceGuFinish();
        sceGuSync(0, 0);

        // UI
        pspDebugScreenSetXY(0, 0);
        pspDebugScreenSetBackColor(0x80000000);
        pspDebugScreenSetTextColor(0xFFFFFFFF);
        if(game.gameOver) {
            printf("GAME OVER!\n");
            printf("Final Score: %d\n", game.score);
            printf("Press X to Restart | START=Exit");
        } else {
            printf("Score: %d | Health: %d\n", game.score, game.player.health);
            printf("D-Pad=Move X=Shoot START=Exit");
        }

        // Debug info
        int enemyCount, bulletCount, eBulletCount, particleCount;
        countEntities(&game, &enemyCount, &bulletCount, &eBulletCount, &particleCount);
        pspDebugScreenSetXY(0, 30);
        pspDebugScreenSetTextColor(0xFF00FF00);  // Green
        printf("FPS: %.1f | Pos: (%.2f, %.2f, %.2f)\n", fps, game.player.x, game.player.y, game.player.z);
        printf("Enemies: %d | Bullets: %d | EBullets: %d | Particles: %d",
               enemyCount, bulletCount, eBulletCount, particleCount);

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();

        oldPad = pad;
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
