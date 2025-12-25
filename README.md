# PSP 3D Shooter Game

An action-packed PlayStation Portable (PSP) 3D shoot 'em up game! Battle waves of enemies in a stunning 3D environment with a scrolling terrain and blue sky. This game showcases the PSP's 3D graphics capabilities and can be compiled and run on PSP emulators like PPSSPP.

## Features

### 3D Graphics Engine
- **Full 3D rendering** using PSP's Graphics Unit (GU)
- **Blue sky background** with proper perspective
- **Scrolling green terrain** with height variation (sine wave terrain)
- **3D camera system** with perspective projection
- **Depth buffering** for proper 3D object ordering
- **Smooth shading** and color blending

### Gameplay
- Fast-paced 3D shooting action
- **6 unique enemy types** with different behaviors, visuals, and point values:
  - **Basic** (Red Triangle, 10pts): Flies straight toward player
  - **Zigzag** (Purple Diamond, 12pts): Moves side-to-side while approaching
  - **Circler** (Green X, 20pts): Orbits around player while advancing
  - **Shooter** (Orange Square, 25pts): Fires projectiles at you
  - **Tank** (Blue Hexagon, 30pts): Slow but tough with 3 health points
  - **Speedster** (Yellow Star, 15pts): Fast, erratic movement, 1 health point
- Enemy spawning system with 3D AI movement
- **Enemy health system** - tougher enemies require multiple hits
- **Enemy projectiles** - dodge bullets from shooter-type enemies
- **Progressive difficulty** - enemies spawn faster and move quicker as your score increases
- 3D collision detection using distance calculations
- **Player-enemy collision** - take damage when enemies reach you
- **3D particle explosion effects** with gravity physics
- Score tracking system with variable point values per enemy type
- **Health system** - lose health on collision or from enemy bullets, game over at 0 health
- **Game over and restart** - press X to restart after game over
- Smooth 3D movement controls
- Professional 2D UI overlay with real-time stats

## Quick Start - Download Pre-built Game

**Don't want to build? Download the ready-to-play game file!**

The game is automatically built using GitHub Actions every time code is pushed. To download the pre-built `EBOOT.PBP`:

1. Go to the [Actions tab](../../actions) in this repository
2. Click on the latest successful workflow run (green checkmark)
3. Scroll down to "Artifacts" section
4. Download `psp-game-eboot`
5. Extract the ZIP file to get `EBOOT.PBP`
6. Load it in PPSSPP emulator (see "Running the Game" section below)

**That's it! No compilation needed!**

---

## Building From Source (Optional)

If you want to build the game yourself or modify the code, continue reading.

## Prerequisites

To compile this game, you need the PSP SDK (PSPSDK) installed on your system.

### Installing PSPSDK

#### On Ubuntu/Debian Linux:

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install build-essential autoconf automake cmake doxygen bison flex \
    libncurses5-dev libsdl1.2-dev libreadline-dev libusb-dev texinfo \
    libgmp3-dev libmpfr-dev libelf-dev libmpc-dev libfreetype6-dev \
    zlib1g-dev libtool libtool-bin git tcl unzip wget

# Clone and build the toolchain
git clone https://github.com/pspdev/psptoolchain.git
cd psptoolchain
./toolchain.sh
```

#### On macOS:

```bash
# Install dependencies via Homebrew
brew install cmake gmp mpfr libelf libmpc autoconf automake wget coreutils

# Clone and build the toolchain
git clone https://github.com/pspdev/psptoolchain.git
cd psptoolchain
./toolchain.sh
```

#### Set Environment Variables:

After installation, add these to your `.bashrc` or `.zshrc`:

```bash
export PSPDEV=/usr/local/pspdev
export PATH=$PATH:$PSPDEV/bin
```

Then reload your shell:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

### Using Docker (Easiest Method)

If you prefer not to install the toolchain directly, you can use Docker:

```bash
docker pull pspdev/pspdev
docker run -v $(pwd):/project -w /project pspdev/pspdev make
```

## Building the Game

Once you have the PSPSDK installed:

```bash
# Clone this repository
git clone <repository-url>
cd psp-game

# Build the game
make

# This will create EBOOT.PBP, which is the executable file for PSP
```

### Build Output

After a successful build, you'll have:
- `EBOOT.PBP` - The PSP executable file

## Running the Game

### Using PPSSPP Emulator

1. **Download PPSSPP:**
   - Website: https://www.ppsspp.org/
   - Available for Windows, macOS, Linux, Android, and iOS

2. **Run the game:**
   - Open PPSSPP
   - Navigate to the folder containing `EBOOT.PBP`
   - Select the file to run the game

### On Real PSP Hardware

1. Copy `EBOOT.PBP` to your PSP's memory stick:
   - Create folder structure: `PSP/GAME/PSPGame/`
   - Place `EBOOT.PBP` in this folder
2. Navigate to "Game" on your PSP menu
3. Select "Memory Stick"
4. Run "PSP Game Demo"

## Controls

- **D-Pad Up/Down/Left/Right:** Move your ship
- **X Button (Cross):** Shoot bullets
- **START Button:** Exit the game

## Project Structure

```
psp-game/
├── .github/
│   └── workflows/
│       └── build-psp-game.yml  # GitHub Actions build workflow
├── main.c                       # Main game source code
├── Makefile                     # Build configuration
├── .gitignore                   # Git ignore file
└── README.md                    # This file
```

## Game Description

This is a full 3D shoot 'em up game that demonstrates:
- **PSP 3D Graphics Engine** with GU (Graphics Unit) initialization
- **3D terrain rendering** with mesh generation and scrolling
- **Perspective camera system** with matrix transformations
- **3D model rendering** for player ship, enemies, bullets, and particles
- Controller input handling for 3D movement
- **3D collision detection** using distance calculations
- Particle physics with 3D gravity simulation
- Dynamic enemy AI with 3D pathfinding
- Score and health tracking with 2D UI overlay

**Gameplay:** Control your 3D spaceship (gray body with orange wings) and battle against 6 different enemy types, each with unique behaviors and appearances! From basic red triangles to tough blue tanks and fast yellow speedsters, enemies spawn from the distance with varied attack patterns. Watch out for orange shooters that fire projectiles back at you! The game features a beautiful scrolling green terrain below with sine-wave height variation, and a bright blue sky above. When you destroy an enemy, it explodes into a spectacular 3D shower of colored particles (red, orange, yellow) with realistic gravity physics!

**Challenge:** Avoid colliding with enemies or getting hit by enemy bullets - you'll take damage! You have 3 health points - lose them all and it's game over. Different enemy types require different strategies: tanks need multiple hits, speedsters are hard to track, and circlers orbit around you. As your score increases, enemies spawn faster and move quicker toward you, creating an intense difficulty curve that tests your reflexes and tactical skills. Score varies by enemy type (10-30 points), so prioritize high-value targets! After game over, press X to restart and try to beat your high score!

The game runs in full 3D with proper depth buffering and perspective projection.

## Troubleshooting

### "psp-config: command not found"

Make sure the PSPDEV environment variable is set and the PSP SDK bin directory is in your PATH.

### Build errors

Ensure all dependencies are installed and the PSP SDK is properly built.

### EBOOT.PBP won't run in emulator

Make sure you're using a recent version of PPSSPP. Older versions may have compatibility issues.

## Clean Build

To clean build artifacts:

```bash
make clean
```

## License

This is a demonstration project for educational purposes.

## Credits

### Audio Assets

- **shoot_1.wav** - "4 projectile launches" by Michel Baradari ([apollo-music.de](http://apollo-music.de)), licensed under [CC-BY 3.0](https://creativecommons.org/licenses/by/3.0/). Source: [OpenGameArt.org](https://opengameart.org/content/4-projectile-launches)

## Resources

- [PSP Development Wiki](https://github.com/pspdev/pspdev)
- [PSPSDK Documentation](http://psp.jim.sh/pspsdk-doc/)
- [PPSSPP Emulator](https://www.ppsspp.org/)
- [PSP Homebrew Community](https://www.reddit.com/r/PSP/)
