#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>
#include <cmath>
#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <thread>
#include <random>

// =============================================================================
//  CYBER-BREACH 3D: PROTOCOL ZERO
//  A 3D First-Person Sci-Fi Raycasting Shooter for the Terminal
// =============================================================================

namespace {

constexpr int SCREEN_W = 92;
constexpr int SCREEN_H = 28;
constexpr float FOV = 1.15f;           // ~66 degrees field of view
constexpr float MAX_DEPTH = 18.0f;     // Raycast draw distance
constexpr int MAP_W = 24;
constexpr int MAP_H = 24;

// Sound System (Non-blocking background audio via Windows Beep API)
void playSound(int freq, int durationMs) {
    std::thread([freq, durationMs]() {
        Beep(freq, durationMs);
    }).detach();
}

void playArpeggio() {
    std::thread([]() {
        Beep(523, 70); // C5
        Beep(659, 70); // E5
        Beep(784, 70); // G5
        Beep(1046, 110); // C6
    }).detach();
}

void playLaserSound() {
    std::thread([]() {
        Beep(1300, 25);
        Beep(900, 30);
        Beep(600, 35);
    }).detach();
}

void playHurtSound() {
    std::thread([]() {
        Beep(220, 50);
        Beep(150, 80);
    }).detach();
}

void playExplosionSound() {
    std::thread([]() {
        Beep(180, 60);
        Beep(120, 90);
        Beep(90, 120);
    }).detach();
}

// Facility Map (1: Bulkhead, 2: Server Rack, 3: Reactor Glass, D: Door, E: Exit Airlock)
std::string facilityMap = 
    "111111111111111111111111"
    "1......1.......1.......1"
    "1..P...1...2...1...3...1"
    "1......1...2...1...3...1"
    "1..11D11...11D11...11D11"
    "1..1...............1...1"
    "1..1...11111...111.1...1"
    "1..1...1...1...1.1.1...1"
    "1..D...1.C.1...1.1.D...1"
    "1..1...1...1...1.1.1...1"
    "1..1...11D11...1.1.1...1"
    "1..1...........1.1.1...1"
    "1..111111111...111.1...1"
    "1..............1.......1"
    "1..111111111...111111111"
    "1..1.......1...........1"
    "1..1...C...D...22222...1"
    "1..1.......1...2...2...1"
    "1..111111111...2.C.2...1"
    "1..............2...2...1"
    "1..1111D1111...22222...1"
    "1..1.......1...........1"
    "1..1...E...1...........1"
    "111111111111111111111111";

char getMapCell(int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return '1';
    return facilityMap[y * MAP_W + x];
}

void setMapCell(int x, int y, char c) {
    if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) {
        facilityMap[y * MAP_W + x] = c;
    }
}

// Entity Types
enum class SpriteType {
    DRONE,
    DATA_CORE,
    HEALTH_PACK,
    SHIELD_BOOST,
    PROJECTILE_PLAYER,
    PROJECTILE_ENEMY,
    EXPLOSION
};

struct Sprite {
    float x;
    float y;
    float vx = 0.0f;
    float vy = 0.0f;
    SpriteType type;
    int health = 100;
    bool active = true;
    float timer = 0.0f;
    float attackCooldown = 0.0f;
};

// Player State
struct Player {
    float x = 2.5f;
    float y = 2.5f;
    float angle = 0.0f; // Facing angle in radians
    float fov = FOV;
    int health = 100;
    int shield = 100;
    float shieldRegenTimer = 0.0f;
    float heat = 0.0f;
    bool overheated = false;
    int coresCollected = 0;
    const int totalCores = 3;
    bool firedThisFrame = false;
    float weaponRecoil = 0.0f;
    int hitMarkerTimer = 0;
    int score = 0;
};

} // namespace

int main() {
    // -------------------------------------------------------------------------
    // Initialize Windows Console for Virtual Terminal (ANSI / Truecolor)
    // -------------------------------------------------------------------------
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }

    // Set console code page to UTF-8
    SetConsoleOutputCP(CP_UTF8);

    // Hide console cursor for silky smooth flicker-free rendering
    std::cout << "\x1b[?25l" << std::flush;

    // -------------------------------------------------------------------------
    // Setup World & Entities
    // -------------------------------------------------------------------------
    Player player;
    std::vector<Sprite> sprites;

    // Spawn 3 Data Cores
    sprites.push_back({9.5f, 8.5f, 0, 0, SpriteType::DATA_CORE});
    sprites.push_back({7.5f, 17.5f, 0, 0, SpriteType::DATA_CORE});
    sprites.push_back({17.5f, 18.5f, 0, 0, SpriteType::DATA_CORE});

    // Spawn Security Patrol Drones
    sprites.push_back({15.5f, 2.5f, 0, 0, SpriteType::DRONE, 80});
    sprites.push_back({5.5f, 11.5f, 0, 0, SpriteType::DRONE, 80});
    sprites.push_back({14.5f, 11.5f, 0, 0, SpriteType::DRONE, 90});
    sprites.push_back({11.5f, 19.5f, 0, 0, SpriteType::DRONE, 100});
    sprites.push_back({18.5f, 8.5f, 0, 0, SpriteType::DRONE, 120});

    // Spawn Pickups
    sprites.push_back({1.5f, 5.5f, 0, 0, SpriteType::HEALTH_PACK});
    sprites.push_back({20.5f, 11.5f, 0, 0, SpriteType::SHIELD_BOOST});
    sprites.push_back({7.5f, 21.5f, 0, 0, SpriteType::HEALTH_PACK});

    // Intro Screen
    std::cout << "\x1b[H\x1b[2J";
    std::cout << "\x1b[1;36m"
              << "  ██████╗██╗   ██╗██████╗ ███████╗██████╗       ██████╗ ██████╗ ███████╗ █████╗  ██████╗██╗  ██╗\n"
              << " ██╔════╝╚██╗ ██╔╝██╔══██╗██╔════╝██╔══██╗      ██╔══██╗██╔══██╗██╔════╝██╔══██╗██╔════╝██║  ██║\n"
              << " ██║      ╚████╔╝ ██████╔╝█████╗  ██████╔╝█████╗██████╔╝██████╔╝█████╗  ███████║██║     ███████║\n"
              << " ██║       ╚██╔╝  ██╔══██╗██╔══╝  ██╔══██╗╚════╝██╔══██╗██╔══██╗██╔══╝  ██╔══██║██║     ██╔══██║\n"
              << " ╚██████╗   ██║   ██████╔╝███████╗██║  ██║      ██████╔╝██║  ██║███████╗██║  ██║╚██████╗██║  ██║\n"
              << "  ╚═════╝   ╚═╝   ╚═════╝ ╚══════╝╚═╝  ╚═╝      ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝\n"
              << "\x1b[0m\n";
    std::cout << "  \x1b[1;33mMISSION PROTOCOL: ZERO\x1b[0m\n";
    std::cout << "  Cyber-operative, you have breached the Deep Core Quantum Facility.\n";
    std::cout << "  Your mission: Infiltrate the complex, neutralize patrolling security drones,\n";
    std::cout << "  retrieve the \x1b[1;33m3 Encrypted Data Cores\x1b[0m, and reach the \x1b[1;32mAirlock Elevator (E)\x1b[0m.\n\n";
    std::cout << "  \x1b[1;37mTACTICAL CONTROLS:\x1b[0m\n";
    std::cout << "  \x1b[1;36m[W / S]\x1b[0m        Advance / Retreat\n";
    std::cout << "  \x1b[1;36m[A / D]\x1b[0m        Strafe Left / Right\n";
    std::cout << "  \x1b[1;36m[Q / E]\x1b[0m        Rotate Optical Sensors (or Left/Right Arrow Keys)\n";
    std::cout << "  \x1b[1;36m[SPACE]\x1b[0m        Discharge Plasma Cannon\n";
    std::cout << "  \x1b[1;36m[F]\x1b[0m            Interact / Cycle Security Airlock Doors\n";
    std::cout << "  \x1b[1;36m[M]\x1b[0m            Toggle Full Tactical Satellite Map\n";
    std::cout << "  \x1b[1;36m[ESC / X]\x1b[0m      Abort Mission\n\n";
    std::cout << "  \x1b[1;32m>> PRESS ANY KEY TO ENGAGE COMBAT PROTOCOL <<\x1b[0m\n";

    _getch();

    auto lastTime = std::chrono::steady_clock::now();
    bool running = true;
    bool victory = false;
    bool showFullMap = false;
    float depthBuffer[SCREEN_W];

    // Main Game Loop
    while (running && player.health > 0) {
        auto currentTime = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (dt > 0.1f) dt = 0.1f; // Clamp delta time to avoid large physics steps

        // ---------------------------------------------------------------------
        // Input Handling (Fluid simultaneous keystrokes via GetAsyncKeyState)
        // ---------------------------------------------------------------------
        float moveSpeed = 3.6f * dt;
        float rotSpeed = 2.4f * dt;
        player.firedThisFrame = false;

        // Rotation
        if ((GetAsyncKeyState('Q') & 0x8000) || (GetAsyncKeyState(VK_LEFT) & 0x8000)) {
            player.angle -= rotSpeed;
        }
        if ((GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState(VK_RIGHT) & 0x8000)) {
            player.angle += rotSpeed;
        }

        // Movement with Collision Detection & Sliding
        float dx = 0.0f;
        float dy = 0.0f;
        float dirX = std::cos(player.angle);
        float dirY = std::sin(player.angle);
        float strafeX = -dirY;
        float strafeY = dirX;

        if (GetAsyncKeyState('W') & 0x8000) {
            dx += dirX * moveSpeed;
            dy += dirY * moveSpeed;
        }
        if (GetAsyncKeyState('S') & 0x8000) {
            dx -= dirX * moveSpeed;
            dy -= dirY * moveSpeed;
        }
        if (GetAsyncKeyState('D') & 0x8000) {
            dx += strafeX * moveSpeed;
            dy += strafeY * moveSpeed;
        }
        if (GetAsyncKeyState('A') & 0x8000) {
            dx -= strafeX * moveSpeed;
            dy -= strafeY * moveSpeed;
        }

        // Sliding Collision
        float newX = player.x + dx;
        float newY = player.y + dy;
        char cellX = getMapCell(static_cast<int>(newX), static_cast<int>(player.y));
        char cellY = getMapCell(static_cast<int>(player.x), static_cast<int>(newY));

        if (cellX != '1' && cellX != '2' && cellX != '3' && cellX != 'D') player.x = newX;
        if (cellY != '1' && cellY != '2' && cellY != '3' && cellY != 'D') player.y = newY;

        // Weapon Firing
        if ((GetAsyncKeyState(VK_SPACE) & 0x8000) && !player.overheated && player.weaponRecoil <= 0.05f) {
            player.firedThisFrame = true;
            player.weaponRecoil = 1.0f;
            player.heat += 18.0f;
            if (player.heat >= 100.0f) {
                player.overheated = true;
                player.heat = 100.0f;
            }
            playLaserSound();

            // Spawn fast player plasma projectile
            sprites.push_back({
                player.x + dirX * 0.4f,
                player.y + dirY * 0.4f,
                dirX * 14.0f,
                dirY * 14.0f,
                SpriteType::PROJECTILE_PLAYER,
                1
            });
        }

        // Non-blocking keyboard events (Single presses)
        if (_kbhit()) {
            int ch = _getch();
            if (ch == 'f' || ch == 'F') {
                // Interact / Toggle door in front of player
                int frontX = static_cast<int>(player.x + dirX * 1.3f);
                int frontY = static_cast<int>(player.y + dirY * 1.3f);
                char frontCell = getMapCell(frontX, frontY);
                if (frontCell == 'D') {
                    setMapCell(frontX, frontY, '.');
                    playSound(900, 80);
                } else if (frontCell == '.') {
                    // Check if player is not standing in the door
                    if (static_cast<int>(player.x) != frontX || static_cast<int>(player.y) != frontY) {
                        setMapCell(frontX, frontY, 'D');
                        playSound(600, 80);
                    }
                }
            } else if (ch == 'm' || ch == 'M') {
                showFullMap = !showFullMap;
            } else if (ch == 27 || ch == 'x' || ch == 'X') {
                running = false;
            }
        }

        // Weapon Recoil Recovery & Heat Dissipation
        if (player.weaponRecoil > 0.0f) {
            player.weaponRecoil -= dt * 4.5f;
            if (player.weaponRecoil < 0.0f) player.weaponRecoil = 0.0f;
        }

        if (player.overheated) {
            player.heat -= dt * 25.0f;
            if (player.heat <= 0.0f) {
                player.heat = 0.0f;
                player.overheated = false;
            }
        } else {
            player.heat -= dt * 18.0f;
            if (player.heat < 0.0f) player.heat = 0.0f;
        }

        // Shield Regeneration
        if (player.shield < 100) {
            player.shieldRegenTimer += dt;
            if (player.shieldRegenTimer >= 2.5f) {
                player.shield += static_cast<int>(dt * 15.0f);
                if (player.shield > 100) player.shield = 100;
            }
        }

        if (player.hitMarkerTimer > 0) player.hitMarkerTimer--;

        // Check Victory at Airlock
        if (getMapCell(static_cast<int>(player.x), static_cast<int>(player.y)) == 'E') {
            if (player.coresCollected >= player.totalCores) {
                victory = true;
                break;
            }
        }

        // ---------------------------------------------------------------------
        // Entity & AI Update Loop
        // ---------------------------------------------------------------------
        for (auto& s : sprites) {
            if (!s.active) continue;

            // Projectile Physics
            if (s.type == SpriteType::PROJECTILE_PLAYER || s.type == SpriteType::PROJECTILE_ENEMY) {
                s.x += s.vx * dt;
                s.y += s.vy * dt;
                s.timer += dt;

                // Wall collision for projectiles
                char hitCell = getMapCell(static_cast<int>(s.x), static_cast<int>(s.y));
                if (hitCell == '1' || hitCell == '2' || hitCell == '3' || hitCell == 'D' || s.timer > 2.0f) {
                    s.active = false;
                    continue;
                }

                // Player projectile hitting enemies
                if (s.type == SpriteType::PROJECTILE_PLAYER) {
                    for (auto& target : sprites) {
                        if (target.active && target.type == SpriteType::DRONE) {
                            float distSq = (s.x - target.x) * (s.x - target.x) + (s.y - target.y) * (s.y - target.y);
                            if (distSq < 0.35f) {
                                s.active = false;
                                target.health -= 45;
                                player.hitMarkerTimer = 4;
                                playSound(800, 30);
                                if (target.health <= 0) {
                                    target.active = false;
                                    player.score += 250;
                                    playExplosionSound();
                                    // Spawn explosion sprite
                                    sprites.push_back({target.x, target.y, 0, 0, SpriteType::EXPLOSION});
                                }
                                break;
                            }
                        }
                    }
                }

                // Enemy projectile hitting player
                if (s.type == SpriteType::PROJECTILE_ENEMY) {
                    float distToPlayer = std::sqrt((s.x - player.x) * (s.x - player.x) + (s.y - player.y) * (s.y - player.y));
                    if (distToPlayer < 0.45f) {
                        s.active = false;
                        player.shieldRegenTimer = 0.0f;
                        int dmg = 20;
                        if (player.shield >= dmg) {
                            player.shield -= dmg;
                        } else {
                            int remaining = dmg - player.shield;
                            player.shield = 0;
                            player.health -= remaining;
                        }
                        playHurtSound();
                    }
                }
            }

            // Explosion Lifespan
            else if (s.type == SpriteType::EXPLOSION) {
                s.timer += dt;
                if (s.timer > 0.4f) s.active = false;
            }

            // Security Patrol Drone AI
            else if (s.type == SpriteType::DRONE) {
                float distToPlayer = std::sqrt((s.x - player.x) * (s.x - player.x) + (s.y - player.y) * (s.y - player.y));
                s.attackCooldown -= dt;

                // Line of Sight & Detection
                if (distToPlayer < 7.5f) {
                    // Check direct ray line of sight to player
                    bool lineOfSight = true;
                    int checkSteps = static_cast<int>(distToPlayer * 4.0f);
                    for (int step = 1; step < checkSteps; ++step) {
                        float t = static_cast<float>(step) / checkSteps;
                        float checkX = s.x + (player.x - s.x) * t;
                        float checkY = s.y + (player.y - s.y) * t;
                        char cell = getMapCell(static_cast<int>(checkX), static_cast<int>(checkY));
                        if (cell == '1' || cell == '2' || cell == '3' || cell == 'D') {
                            lineOfSight = false;
                            break;
                        }
                    }

                    if (lineOfSight) {
                        // Chase player
                        float chaseSpeed = 1.4f * dt;
                        float toPlayerX = (player.x - s.x) / distToPlayer;
                        float toPlayerY = (player.y - s.y) / distToPlayer;

                        if (distToPlayer > 2.0f) {
                            float nextX = s.x + toPlayerX * chaseSpeed;
                            float nextY = s.y + toPlayerY * chaseSpeed;
                            if (getMapCell(static_cast<int>(nextX), static_cast<int>(s.y)) == '.') s.x = nextX;
                            if (getMapCell(static_cast<int>(s.x), static_cast<int>(nextY)) == '.') s.y = nextY;
                        }

                        // Fire at player
                        if (s.attackCooldown <= 0.0f) {
                            s.attackCooldown = 1.8f;
                            playLaserSound();
                            sprites.push_back({
                                s.x + toPlayerX * 0.3f,
                                s.y + toPlayerY * 0.3f,
                                toPlayerX * 7.0f,
                                toPlayerY * 7.0f,
                                SpriteType::PROJECTILE_ENEMY,
                                1
                            });
                        }
                    }
                }
            }

            // Item Pickups
            else {
                float distToPlayer = std::sqrt((s.x - player.x) * (s.x - player.x) + (s.y - player.y) * (s.y - player.y));
                if (distToPlayer < 0.6f) {
                    if (s.type == SpriteType::DATA_CORE) {
                        s.active = false;
                        player.coresCollected++;
                        player.score += 500;
                        playArpeggio();
                    } else if (s.type == SpriteType::HEALTH_PACK && player.health < 100) {
                        s.active = false;
                        player.health = std::min(100, player.health + 40);
                        player.score += 50;
                        playArpeggio();
                    } else if (s.type == SpriteType::SHIELD_BOOST && player.shield < 100) {
                        s.active = false;
                        player.shield = 100;
                        player.score += 50;
                        playArpeggio();
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // 3D Raycasting Engine (DDA Algorithm)
        // ---------------------------------------------------------------------
        std::vector<std::string> screen(SCREEN_H, std::string(SCREEN_W, ' '));
        std::vector<std::string> colorScreen(SCREEN_H, std::string(SCREEN_W, '0'));

        float planeX = -std::sin(player.angle) * std::tan(player.fov / 2.0f);
        float planeY =  std::cos(player.angle) * std::tan(player.fov / 2.0f);

        for (int x = 0; x < SCREEN_W; ++x) {
            float cameraX = 2.0f * x / static_cast<float>(SCREEN_W) - 1.0f;
            float rayDirX = dirX + planeX * cameraX;
            float rayDirY = dirY + planeY * cameraX;

            int mapX = static_cast<int>(player.x);
            int mapY = static_cast<int>(player.y);

            float deltaDistX = (rayDirX == 0) ? 1e30f : std::abs(1.0f / rayDirX);
            float deltaDistY = (rayDirY == 0) ? 1e30f : std::abs(1.0f / rayDirY);

            float sideDistX;
            float sideDistY;
            int stepX;
            int stepY;

            if (rayDirX < 0) {
                stepX = -1;
                sideDistX = (player.x - mapX) * deltaDistX;
            } else {
                stepX = 1;
                sideDistX = (mapX + 1.0f - player.x) * deltaDistX;
            }

            if (rayDirY < 0) {
                stepY = -1;
                sideDistY = (player.y - mapY) * deltaDistY;
            } else {
                stepY = 1;
                sideDistY = (mapY + 1.0f - player.y) * deltaDistY;
            }

            bool hit = false;
            int side = 0; // 0 for X, 1 for Y
            char hitWallType = ' ';

            while (!hit) {
                if (sideDistX < sideDistY) {
                    sideDistX += deltaDistX;
                    mapX += stepX;
                    side = 0;
                } else {
                    sideDistY += deltaDistY;
                    mapY += stepY;
                    side = 1;
                }

                hitWallType = getMapCell(mapX, mapY);
                if (hitWallType != '.' && hitWallType != ' ' && hitWallType != 'P' && hitWallType != 'C') {
                    hit = true;
                }
            }

            float perpWallDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
            if (perpWallDist < 0.05f) perpWallDist = 0.05f;
            depthBuffer[x] = perpWallDist;

            int lineHeight = static_cast<int>(SCREEN_H / perpWallDist);
            int drawStart = -lineHeight / 2 + SCREEN_H / 2;
            int drawEnd   =  lineHeight / 2 + SCREEN_H / 2;
            if (drawStart < 0) drawStart = 0;
            if (drawEnd >= SCREEN_H) drawEnd = SCREEN_H - 1;

            // Character Texture & Depth Shading
            char wallChar;
            if (perpWallDist < 2.5f)      wallChar = (side == 0) ? '#' : '#';
            else if (perpWallDist < 4.5f) wallChar = (side == 0) ? '=' : '-';
            else if (perpWallDist < 7.0f) wallChar = (side == 0) ? '|' : ':';
            else if (perpWallDist < 10.0f) wallChar = '.';
            else wallChar = ' ';

            // Color coding for wall types
            // 1: Cyan bulkhead, 2: Magenta server, 3: Yellow reactor, D: Green door, E: Gold airlock
            char colorCode = '1'; // Default cyan
            if (hitWallType == '2') colorCode = '2'; // Magenta
            else if (hitWallType == '3') colorCode = '3'; // Yellow
            else if (hitWallType == 'D') { colorCode = '4'; wallChar = (perpWallDist < 5.0f) ? '+' : '|'; } // Green Door
            else if (hitWallType == 'E') { colorCode = '5'; wallChar = (perpWallDist < 5.0f) ? '$' : '*'; } // Gold Airlock

            if (side == 1) colorCode += 10; // Darker variant for East/West side shading

            for (int y = drawStart; y <= drawEnd; ++y) {
                screen[y][x] = wallChar;
                colorScreen[y][x] = colorCode;
            }

            // Ceiling and Floor Gradients
            for (int y = 0; y < drawStart; ++y) {
                screen[y][x] = ' ';
                colorScreen[y][x] = '0';
            }
            for (int y = drawEnd + 1; y < SCREEN_H; ++y) {
                float floorDist = SCREEN_H / (2.0f * y - SCREEN_H);
                if (floorDist < 2.5f && (x + y) % 6 == 0) {
                    screen[y][x] = '.';
                    colorScreen[y][x] = '6'; // Dim floor grid
                } else {
                    screen[y][x] = ' ';
                    colorScreen[y][x] = '0';
                }
            }
        }

        // ---------------------------------------------------------------------
        // 3D Sprite Rendering (Sorted by Distance)
        // ---------------------------------------------------------------------
        struct SpriteDistance {
            int index;
            float distSq;
        };
        std::vector<SpriteDistance> spriteOrder;
        for (std::size_t i = 0; i < sprites.size(); ++i) {
            if (sprites[i].active) {
                float d = (player.x - sprites[i].x) * (player.x - sprites[i].x) +
                          (player.y - sprites[i].y) * (player.y - sprites[i].y);
                spriteOrder.push_back({static_cast<int>(i), d});
            }
        }
        std::sort(spriteOrder.begin(), spriteOrder.end(), [](const SpriteDistance& a, const SpriteDistance& b) {
            return a.distSq > b.distSq;
        });

        for (const auto& sp : spriteOrder) {
            const auto& s = sprites[sp.index];
            float spriteX = s.x - player.x;
            float spriteY = s.y - player.y;

            float invDet = 1.0f / (planeX * dirY - dirX * planeY);
            float transformX = invDet * (dirY * spriteX - dirX * spriteY);
            float transformY = invDet * (-planeY * spriteX + planeX * spriteY);

            if (transformY <= 0.2f) continue; // Behind camera

            int spriteScreenX = static_cast<int>((SCREEN_W / 2.0f) * (1.0f + transformX / transformY));
            int spriteSize = static_cast<int>(std::abs(SCREEN_H / transformY));
            if (spriteSize < 1) spriteSize = 1;

            int drawStartY = -spriteSize / 2 + SCREEN_H / 2;
            int drawEndY   =  spriteSize / 2 + SCREEN_H / 2;
            if (drawStartY < 0) drawStartY = 0;
            if (drawEndY >= SCREEN_H) drawEndY = SCREEN_H - 1;

            int drawStartX = -spriteSize / 2 + spriteScreenX;
            int drawEndX   =  spriteSize / 2 + spriteScreenX;
            if (drawStartX < 0) drawStartX = 0;
            if (drawEndX >= SCREEN_W) drawEndX = SCREEN_W - 1;

            for (int stripe = drawStartX; stripe <= drawEndX; ++stripe) {
                if (stripe >= 0 && stripe < SCREEN_W && transformY < depthBuffer[stripe]) {
                    for (int y = drawStartY; y <= drawEndY; ++y) {
                        char sprChar = ' ';
                        char sprColor = '0';

                        if (s.type == SpriteType::DRONE) {
                            sprChar = (y == (drawStartY + drawEndY) / 2) ? 'O' : '#';
                            sprColor = '7'; // Red alert drone
                        } else if (s.type == SpriteType::DATA_CORE) {
                            sprChar = '*';
                            sprColor = '8'; // Pulsing Gold core
                        } else if (s.type == SpriteType::HEALTH_PACK) {
                            sprChar = '+';
                            sprColor = '9'; // Green medkit
                        } else if (s.type == SpriteType::SHIELD_BOOST) {
                            sprChar = '^';
                            sprColor = 'a'; // Blue shield
                        } else if (s.type == SpriteType::PROJECTILE_PLAYER) {
                            sprChar = '*';
                            sprColor = 'b'; // Cyan plasma
                        } else if (s.type == SpriteType::PROJECTILE_ENEMY) {
                            sprChar = '!';
                            sprColor = '7'; // Red enemy laser
                        } else if (s.type == SpriteType::EXPLOSION) {
                            sprChar = '%';
                            sprColor = 'c'; // Orange explosion
                        }

                        if (sprChar != ' ') {
                            screen[y][stripe] = sprChar;
                            colorScreen[y][stripe] = sprColor;
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // First-Person Weapon Sprite & Recoil
        // ---------------------------------------------------------------------
        int gunBaseY = SCREEN_H - 6 + static_cast<int>(player.weaponRecoil * 3.0f);
        int gunBaseX = SCREEN_W / 2 - 4;

        const std::vector<std::string> gunSprite = {
            "  /====\\  ",
            " ||[**]|| ",
            " || || || ",
            "  \\====/  ",
            "   |  |   "
        };

        // Draw muzzle flash if fired
        if (player.firedThisFrame) {
            if (gunBaseY - 2 >= 0) {
                screen[gunBaseY - 2][SCREEN_W / 2 - 1] = '*';
                colorScreen[gunBaseY - 2][SCREEN_W / 2 - 1] = 'b';
                screen[gunBaseY - 1][SCREEN_W / 2 - 2] = '<';
                screen[gunBaseY - 1][SCREEN_W / 2]     = '>';
                colorScreen[gunBaseY - 1][SCREEN_W / 2 - 2] = 'b';
                colorScreen[gunBaseY - 1][SCREEN_W / 2]     = 'b';
            }
        }

        for (std::size_t gy = 0; gy < gunSprite.size(); ++gy) {
            int targetY = gunBaseY + static_cast<int>(gy);
            if (targetY >= 0 && targetY < SCREEN_H) {
                for (std::size_t gx = 0; gx < gunSprite[gy].size(); ++gx) {
                    if (gunSprite[gy][gx] != ' ') {
                        int targetX = gunBaseX + static_cast<int>(gx);
                        if (targetX >= 0 && targetX < SCREEN_W) {
                            screen[targetY][targetX] = gunSprite[gy][gx];
                            colorScreen[targetY][targetX] = (player.overheated ? 'c' : 'b');
                        }
                    }
                }
            }
        }

        // Crosshair & Hitmarker
        int midX = SCREEN_W / 2;
        int midY = SCREEN_H / 2;
        screen[midY][midX] = (player.hitMarkerTimer > 0) ? 'X' : '+';
        colorScreen[midY][midX] = (player.hitMarkerTimer > 0) ? '7' : '1';

        // ---------------------------------------------------------------------
        // Tactical Mini-Map (Upper-Right HUD)
        // ---------------------------------------------------------------------
        if (!showFullMap) {
            constexpr int MM_SIZE = 9;
            int mmStartX = SCREEN_W - MM_SIZE - 2;
            int mmStartY = 1;

            int px = static_cast<int>(player.x);
            int py = static_cast<int>(player.y);

            for (int dy = -MM_SIZE / 2; dy <= MM_SIZE / 2; ++dy) {
                for (int dx = -MM_SIZE / 2; dx <= MM_SIZE / 2; ++dx) {
                    int worldX = px + dx;
                    int worldY = py + dy;
                    int screenX = mmStartX + dx + MM_SIZE / 2;
                    int screenY = mmStartY + dy + MM_SIZE / 2;

                    if (dx == 0 && dy == 0) {
                        // Player direction arrow
                        char pChar = '^';
                        float ang = player.angle;
                        while (ang < 0) ang += 6.28318f;
                        while (ang >= 6.28318f) ang -= 6.28318f;
                        if (ang >= 0.78f && ang < 2.35f) pChar = 'v';
                        else if (ang >= 2.35f && ang < 3.92f) pChar = '<';
                        else if (ang >= 3.92f && ang < 5.49f) pChar = '^';
                        else pChar = '>';

                        screen[screenY][screenX] = pChar;
                        colorScreen[screenY][screenX] = '8';
                    } else {
                        char cell = getMapCell(worldX, worldY);
                        if (cell == '1' || cell == '2' || cell == '3') {
                            screen[screenY][screenX] = '#';
                            colorScreen[screenY][screenX] = '1';
                        } else if (cell == 'D') {
                            screen[screenY][screenX] = '=';
                            colorScreen[screenY][screenX] = '4';
                        } else if (cell == 'E') {
                            screen[screenY][screenX] = 'E';
                            colorScreen[screenY][screenX] = '5';
                        } else {
                            // Check if a drone is nearby
                            bool hasDrone = false;
                            for (const auto& s : sprites) {
                                if (s.active && s.type == SpriteType::DRONE &&
                                    static_cast<int>(s.x) == worldX && static_cast<int>(s.y) == worldY) {
                                    hasDrone = true;
                                    break;
                                }
                            }
                            if (hasDrone) {
                                screen[screenY][screenX] = '!';
                                colorScreen[screenY][screenX] = '7';
                            } else {
                                screen[screenY][screenX] = '.';
                                colorScreen[screenY][screenX] = '6';
                            }
                        }
                    }
                }
            }
        } else {
            // Full Tactical Holographic Schematic Overlay
            int mapOffsetX = (SCREEN_W - MAP_W) / 2;
            int mapOffsetY = (SCREEN_H - MAP_H) / 2;

            for (int my = 0; my < MAP_H; ++my) {
                for (int mx = 0; mx < MAP_W; ++mx) {
                    int scX = mapOffsetX + mx;
                    int scY = mapOffsetY + my;
                    if (scX >= 0 && scX < SCREEN_W && scY >= 0 && scY < SCREEN_H) {
                        if (static_cast<int>(player.x) == mx && static_cast<int>(player.y) == my) {
                            screen[scY][scX] = '@';
                            colorScreen[scY][scX] = '8'; // Player in bright gold
                        } else {
                            // Check if drone or core here
                            char ent = ' ';
                            char col = '0';
                            for (const auto& sp : sprites) {
                                if (sp.active && static_cast<int>(sp.x) == mx && static_cast<int>(sp.y) == my) {
                                    if (sp.type == SpriteType::DRONE) { ent = '!'; col = '7'; break; }
                                    if (sp.type == SpriteType::DATA_CORE) { ent = '*'; col = '8'; break; }
                                    if (sp.type == SpriteType::HEALTH_PACK) { ent = '+'; col = '9'; break; }
                                    if (sp.type == SpriteType::SHIELD_BOOST) { ent = '^'; col = 'a'; break; }
                                }
                            }
                            if (ent != ' ') {
                                screen[scY][scX] = ent;
                                colorScreen[scY][scX] = col;
                            } else {
                                char cell = getMapCell(mx, my);
                                if (cell == '1' || cell == '2' || cell == '3') {
                                    screen[scY][scX] = '#';
                                    colorScreen[scY][scX] = '1';
                                } else if (cell == 'D') {
                                    screen[scY][scX] = 'D';
                                    colorScreen[scY][scX] = '4';
                                } else if (cell == 'E') {
                                    screen[scY][scX] = 'E';
                                    colorScreen[scY][scX] = '5';
                                } else {
                                    screen[scY][scX] = '.';
                                    colorScreen[scY][scX] = '6';
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // Render Frame with ANSI Color Mapping
        // ---------------------------------------------------------------------
        std::string frameBuffer;
        frameBuffer.reserve(SCREEN_W * SCREEN_H * 8);
        frameBuffer += "\x1b[H"; // Cursor to top-left

        // Top Status Header
        frameBuffer += "\x1b[1;36m[CYBER-BREACH 3D]\x1b[0m  ";
        frameBuffer += "HP: \x1b[1;32m" + std::to_string(player.health) + "%\x1b[0m ";
        frameBuffer += "SHIELD: \x1b[1;36m" + std::to_string(player.shield) + "%\x1b[0m ";
        frameBuffer += "HEAT: " + std::string(player.overheated ? "\x1b[1;31m[OVERHEAT]\x1b[0m " : "\x1b[1;33m" + std::to_string(static_cast<int>(player.heat)) + "%\x1b[0m ");
        frameBuffer += "CORES: \x1b[1;33m" + std::to_string(player.coresCollected) + "/" + std::to_string(player.totalCores) + "\x1b[0m ";
        frameBuffer += "SCORE: \x1b[1;37m" + std::to_string(player.score) + "\x1b[0m\n";

        char lastColor = ' ';
        for (int y = 0; y < SCREEN_H; ++y) {
            frameBuffer += " ";
            for (int x = 0; x < SCREEN_W; ++x) {
                char col = colorScreen[y][x];
                if (col != lastColor) {
                    lastColor = col;
                    switch (col) {
                        case '0': frameBuffer += "\x1b[0m"; break;
                        case '1': frameBuffer += "\x1b[1;36m"; break; // Bright Cyan
                        case '2': frameBuffer += "\x1b[1;35m"; break; // Bright Magenta
                        case '3': frameBuffer += "\x1b[1;33m"; break; // Bright Yellow
                        case '4': frameBuffer += "\x1b[1;32m"; break; // Bright Green Door
                        case '5': frameBuffer += "\x1b[1;93m"; break; // Gold Airlock
                        case '6': frameBuffer += "\x1b[90m";   break; // Dark Gray
                        case '7': frameBuffer += "\x1b[1;31m"; break; // Bright Red
                        case '8': frameBuffer += "\x1b[1;33m"; break; // Yellow Core
                        case '9': frameBuffer += "\x1b[1;32m"; break; // Green Pack
                        case 'a': frameBuffer += "\x1b[1;34m"; break; // Blue Shield
                        case 'b': frameBuffer += "\x1b[1;96m"; break; // Cyan Plasma
                        case 'c': frameBuffer += "\x1b[1;31m"; break; // Orange/Red Explosion
                        case 11:  frameBuffer += "\x1b[36m";   break; // Dim Cyan
                        case 12:  frameBuffer += "\x1b[35m";   break; // Dim Magenta
                        case 13:  frameBuffer += "\x1b[33m";   break; // Dim Yellow
                        default:  frameBuffer += "\x1b[0m";    break;
                    }
                }
                frameBuffer += screen[y][x];
            }
            frameBuffer += "\x1b[0m\n";
            lastColor = ' ';
        }

        // Bottom Tactical Bar
        frameBuffer += " \x1b[1;30m[W/S] Move | [A/D] Strafe | [Q/E/Arrows] Look | [SPACE] Fire | [F] Door | [M] Map | [ESC] Quit\x1b[0m\n";
        
        // Output frame to console in single atomic write
        std::cout.write(frameBuffer.data(), frameBuffer.size());
        std::cout.flush();

        // Target ~40-50 FPS
        std::this_thread::sleep_for(std::chrono::milliseconds(22));
    }

    // -------------------------------------------------------------------------
    // End Game Screens (Victory or Defeat)
    // -------------------------------------------------------------------------
    std::cout << "\x1b[?25h\x1b[H\x1b[2J";
    if (victory) {
        std::cout << "\x1b[1;32m"
                  << "  ===============================================================\n"
                  << "  MISSION COMPLETE: EXTRACTION AIRLOCK REACHED!\n"
                  << "  ===============================================================\x1b[0m\n\n"
                  << "  Operative, all " << player.totalCores << " Quantum Data Cores were successfully extracted.\n"
                  << "  The facility security grid has been neutralized.\n\n"
                  << "  \x1b[1;37mFINAL SCORE:\x1b[0m \x1b[1;33m" << player.score << " PTS\x1b[0m\n"
                  << "  \x1b[1;37mSURVIVAL STATUS:\x1b[0m \x1b[1;32m100% SUCCESS\x1b[0m\n\n";
        playArpeggio();
    } else {
        std::cout << "\x1b[1;31m"
                  << "  ===============================================================\n"
                  << "  SIGNAL LOST: OPERATIVE COMPROMISED\n"
                  << "  ===============================================================\x1b[0m\n\n"
                  << "  Security drones overwhelmed your vital systems.\n"
                  << "  Cores Secured: " << player.coresCollected << " / " << player.totalCores << "\n"
                  << "  Final Score: " << player.score << " PTS\n\n";
        playHurtSound();
    }

    std::cout << "  Press any key to exit...\n";
    _getch();

    return 0;
}
