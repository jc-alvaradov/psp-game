# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a PSP (PlayStation Portable) 3D shoot 'em up game written in C using the PSPSDK. The game features a full 3D graphics engine with terrain rendering, 6 unique enemy types with AI behaviors, particle effects, collision detection, and progressive difficulty scaling.

## Build System

### Building the Game

```bash
make              # Build the game (creates EBOOT.PBP)
make clean        # Clean build artifacts
```

The Makefile uses PSPSDK's build system (`build.mak`). Key build configuration:
- **Target:** `psp-game`
- **Source:** `main.c` (single source file)
- **Libraries:** `-lpspgu -lpspgum -lm` (PSP Graphics Unit, GUM matrix utilities, math)
- **Output:** `EBOOT.PBP` (PSP executable format)
- **Compiler flags:** `-O2 -G0 -Wall`

### Docker Build (Alternative)

```bash
docker pull pspdev/pspdev
docker run -v $(pwd):/project -w /project pspdev/pspdev make
```

### CI/CD

GitHub Actions automatically builds the game on pushes to `main`, `master`, or `claude/**` branches. Artifacts are uploaded as `psp-game-eboot`.

## Testing

### Running the Game

Use the PPSSPP emulator (https://www.ppsspp.org/):
1. Build or download `EBOOT.PBP`
2. Open PPSSPP and load the file
3. Test gameplay with controls

**Controls:**
- D-Pad: Move ship
- X Button: Shoot
- START: Exit game

## Code Architecture

### Single-File Structure

The entire game is contained in `main.c` (~715 lines). This is intentional for PSP homebrew simplicity.

### Core Game Loop

Located in `main()` function (main.c:586-714):
1. **Initialization:** GU setup (main.c:591-608), display lists, callbacks
2. **Input:** `sceCtrlReadBufferPositive()` for controller state (main.c:619)
3. **Update:** `updateGame()` (main.c:260-357) - movement, AI, collision, spawning
4. **Render:** 3D graphics using PSP GU commands (main.c:647-690)
5. **Display:** Frame buffer swap (main.c:706)

### Key Data Structures

All game state is encapsulated in the `Game` struct:
- `Player`: Position (x,y,z), health
- `Bullet[MAX_BULLETS]`: Player projectiles
- `Enemy[MAX_ENEMIES]`: Enemy entities with type, health, AI state
- `EnemyBullet[MAX_ENEMY_BULLETS]`: Enemy projectiles
- `Particle[MAX_PARTICLES]`: Explosion effects
- Game state: score, timers, gameOver flag

### Enemy System

Six enemy types defined in `EnemyType` enum (main.c:24-31):
- `ENEMY_BASIC`: Straight movement
- `ENEMY_ZIGZAG`: Sine wave pattern
- `ENEMY_CIRCLER`: Orbital movement around player
- `ENEMY_SHOOTER`: Fires projectiles at player
- `ENEMY_TANK`: 3 health, slow
- `ENEMY_SPEEDSTER`: Fast, erratic movement

Key functions:
- `updateEnemy()` (main.c:190-246): Movement AI per type
- `drawEnemy()` (main.c:462-584): Visual rendering per type
- `spawnEnemy()` (main.c:130-160): Spawning and health initialization
- `getEnemyPoints()` (main.c:248-258): Point values (10-30 points)

### 3D Graphics Pipeline

The PSP GU (Graphics Unit) is initialized in `main()` (main.c:591-608):
1. **Display lists:** 262KB aligned buffer for GU commands (main.c:33)
2. **Projection matrix:** Perspective with FOV, aspect ratio, near/far planes
3. **Depth buffering:** Enabled for proper 3D ordering
4. **Shading:** Smooth shading with color blending

**Key drawing functions:**
- `drawTerrain()` (main.c:415-439): Scrolling green mesh with sine waves
- `drawPlayer()` (main.c:441-460): Gray cube body + orange wings
- `drawEnemy()` (main.c:462-584): Type-specific geometry per enemy
- `drawCube()` (main.c:359-413): General cube rendering for bullets/particles

**Rendering order per frame:**
1. Clear color/depth buffers (sky color: 0xFFFFE0C0 - light blue)
2. Draw terrain - scrolling green mesh
3. Draw player ship
4. Draw player bullets (yellow cubes)
5. Draw enemies - type-specific geometry
6. Draw enemy bullets (red cubes)
7. Draw particles - colored cubes
8. Draw 2D UI overlay (`pspDebugScreenSetXY()`, `printf()`)

### Collision Detection

3D distance-based collision:
- **Bullet-Enemy:** Distance check between bullet and enemy positions
- **Player-Enemy:** Distance check triggers damage when enemies reach player
- **Player-EnemyBullet:** Distance check for incoming projectiles

### Progressive Difficulty

Difficulty scales with score (see `main()` game loop, main.c:637-642):
- **Enemy spawn rate:** Decreases from 80 frames to minimum 30 frames based on score
- **Enemy speed:** Base speed increases with score (main.c:280-281), capped at 0.06f

### Particle System

Explosions spawn 15 particles with:
- Random velocities in 3D space
- Gravity simulation (downward acceleration)
- Limited lifetime (30-50 frames)
- Color variety (red, orange, yellow)

## PSP-Specific Conventions

### PSP Module Info

Required metadata at top of `main.c`:
```c
PSP_MODULE_INFO("PSP 3D Shooter", 0, 1, 6);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
```

### Screen Resolution

- **Display buffer width:** 512 pixels
- **Visible screen:** 480x272 pixels
- All 2D UI coordinates must fit within visible area

### Exit Callback

PSP requires proper exit handling via `SetupCallbacks()` to allow HOME button functionality.

### Random Number Generation

Custom `randInt()` function uses linear congruential generator (no stdlib `rand()` on PSP).

## Development Guidelines

### Adding New Enemy Types

1. Add enum value to `EnemyType`
2. Implement movement behavior in `updateEnemy()` switch statement
3. Add visual representation in `drawEnemy()` switch statement
4. Set health value in `spawnEnemy()` switch statement
5. Add point value in collision detection code (main game loop)

### Adding New Features

When extending the game:
- Maintain single-file structure for PSP homebrew conventions
- Use `sceGuGetMemory()` for dynamic vertex allocation (don't use malloc)
- Keep game state in the `Game` struct
- Use fixed-size arrays (no dynamic allocation on PSP)
- Test memory alignment for GU buffers (16-byte alignment required)

### Memory Constraints

PSP has limited memory. Current limits:
- `MAX_BULLETS`: 30
- `MAX_ENEMIES`: 15
- `MAX_PARTICLES`: 100
- `MAX_ENEMY_BULLETS`: 20

Increase cautiously and test on hardware/emulator.

### Coordinate System

- **X-axis:** Left (-) to Right (+)
- **Y-axis:** Down (-) to Up (+)
- **Z-axis:** Into screen (-) to Out of screen (+)
- Player always at z=0, enemies spawn at z=-10, bullets move toward -z

## Common Modifications

### Adjusting Difficulty

- Enemy spawn rate: Modify timer logic in main loop (main.c:637-638, currently `80 - (score/50)`)
- Enemy speed: Adjust `enemySpeed` calculation (main.c:280-281, currently `0.025f + (score/5000.0f)`)
- Player health: Change `g->player.health = 3` in `initGame()` (main.c:108)

### Visual Tweaks

- Sky color: Modify `sceGuClearColor(0xFFFFE0C0)` (main.c:649)
- Terrain color: Change `0xFF00CC00` in `drawTerrain()` (main.c:429-434)
- Terrain wave pattern: Adjust `sinf(x1 * 0.3f + z1 * 0.3f + time) * 0.3f` (main.c:427)

### Adding Weapons/Power-ups

See `FEATURE_SUGGESTIONS.md` for detailed implementation ideas including power-up system, multiple weapon types, boss battles, and more.
