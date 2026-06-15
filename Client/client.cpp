#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <Windows.h>
#include <chrono>
#include <algorithm>
#include "C:\Repository\MMORPG-with-IOCP\MMORPG-with-IOCP\GameServer\Protocol.h"

// ObjID static member definitions (required by linker)
ContentID ContentID::npos{};
ObjID ObjID::npos{};

// Orc sprite-sheet constants (Tiny RPG Character Asset Pack v1.03)
constexpr int   ORC_FRAME_W      = 100;
constexpr int   ORC_FRAME_H      = 100;
constexpr int   ORC_IDLE_FRAMES  = 6;
constexpr int   ORC_WALK_FRAMES  = 8;
constexpr int   ORC_ATK_FRAMES   = 6;
constexpr int   ORC_DEATH_FRAMES = 4;
constexpr int   ORC_HURT_FRAMES  = 4;  // Orc-Hurt.png  400/100

// Monster display sizes (pixels on screen). Tile = 65px.
constexpr int   ORC_DISPLAY_SIZE   = 256;   // Orc (aggro monster)
constexpr float ORC_FRAME_SEC      = 0.12f; // frame interval (seconds)

// Slime (passive monster) sprite-sheet constants
constexpr int   SLIME_FRAME_W      = 96;
constexpr int   SLIME_FRAME_H      = 96;
constexpr int   SLIME_IDLE_FRAMES  = 6;
constexpr int   SLIME_WALK_FRAMES  = 8;
constexpr int   SLIME_ATK_FRAMES   = 8;
constexpr int   SLIME_HURT_FRAMES  = 4;
constexpr int   SLIME_DEATH_FRAMES = 10;
constexpr int   SLIME_DISPLAY_SIZE = 128;

using namespace std;

#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH  = 16;
constexpr auto SCREEN_HEIGHT = 13;

constexpr auto TILE_WIDTH    = 65;
constexpr auto WINDOW_WIDTH  = SCREEN_WIDTH  * TILE_WIDTH;
constexpr auto WINDOW_HEIGHT = SCREEN_HEIGHT * TILE_WIDTH;

int   g_left_x;
int   g_top_y;
float g_cam_x = 0.f;   // smooth camera position (tile units, float)
float g_cam_y = 0.f;
ObjID g_myid;
string g_chatInput;
vector<string> g_chatHistory;
bool g_chatMode = false;
sf::RenderWindow* g_window;

// ─── Login screen state ───────────────────────────────────────────────────────
enum class ClientState { LOGIN, INGAME };
ClientState g_clientState = ClientState::LOGIN;
string  g_loginId;
string  g_loginPass;
int     g_loginField  = 0;   // 0 = ID focused, 1 = PW focused
float   g_loginCursor = 0.f;
// ─────────────────────────────────────────────────────────────────────────────
sf::Font* g_font = nullptr;

// ─── Soldier sprite sheet constants ──────────────────────────────────────────
constexpr int SOLDIER_FRAME_W    = 100;
constexpr int SOLDIER_FRAME_H    = 100;
constexpr int SOLDIER_IDLE_FRAMES = 6;
constexpr int SOLDIER_WALK_FRAMES = 8;
constexpr int SOLDIER_ATK_FRAMES   = 6;  // Soldier-Attack01.png  600/100
constexpr int SOLDIER_SKILL_FRAMES = 9;  // Soldier-Attack03.png  900/100
constexpr int SOLDIER_HURT_FRAMES  = 4;  // Soldier-Hurt.png      400/100
constexpr float IDLE_FPS         = 6.f;
constexpr float WALK_FPS         = 10.f;
// ─────────────────────────────────────────────────────────────────────────────

enum class AnimState { IDLE, WALK, ATTACK, HURT, DEATH };

class OBJECT {
private:
    bool m_showing = false;
    sf::Sprite m_sprite;

    sf::Text m_name;
    sf::Text m_chat;
    chrono::system_clock::time_point m_mess_end_time;

    // --- Animation ---
    sf::Texture* m_tex_idle    = nullptr;
    sf::Texture* m_tex_walk    = nullptr;
    sf::Texture* m_tex_attack  = nullptr;
    sf::Texture* m_tex_attack2 = nullptr;  // alternate attack (e.g. Attack02)
    sf::Texture* m_tex_attack3 = nullptr;  // skill attack   (e.g. Attack03)
    sf::Texture* m_tex_hurt    = nullptr;
    sf::Texture* m_tex_death   = nullptr;
    int          m_idle_frames  = 1;
    int          m_walk_frames  = 1;
    int          m_atk_frames   = 1;
    int          m_skill_frames = 1;
    int          m_hurt_frames  = 1;
    int          m_death_frames = 1;
    bool         m_atk_toggle  = false;  // alternates attack1/attack2
    bool         m_is_skill    = false;  // true while skill attack03 plays
    int          m_frame_w     = 64;
    int          m_frame_h     = 64;
    AnimState    m_anim_state  = AnimState::IDLE;
    int          m_frame       = 0;
    float        m_frame_timer = 0.f;
    bool         m_flip_x      = false;
    float        m_anim_scale  = 1.f;
    bool         m_death_done  = false;
    float        m_hurt_flash  = 0.f;  // red-flash timer, independent of anim state
    static constexpr float HURT_FLASH_DURATION = 0.35f;

public:
    ObjID id;
    int   m_x = 0, m_y = 0;
    float m_vis_x = 0.f, m_vis_y = 0.f;  // visual (interpolated) position
    int level = 0, hp = 0, maxhp = 0, exp = 0;
    char name[20] = {};
    string name_str;

    // Constructor for static (non-animated) objects: tiles, monsters, etc.
    OBJECT(sf::Texture& t, int x, int y, int w, int h) {
        m_showing = false;
        m_sprite.setTexture(t);
        m_sprite.setTextureRect(sf::IntRect(x, y, w, h));
        m_mess_end_time = chrono::system_clock::now();
        set_name("NONAME");
    }

    OBJECT() {
        m_showing = false;
        m_mess_end_time = chrono::system_clock::now();
    }

    // ── Animation setup ──────────────────────────────────────────────────────
    // Call once after construction to enable animation on this object.
    // The sprite origin is set to the frame center so flipping works correctly
    // and the character is centered on its tile.
    // targetDisplaySize: the pixel size (width==height) to render the sprite at on screen.
    //                    Pass 0 to keep native frame size (scale = 1).
    void SetAnimTextures(sf::Texture* idle, int idleFrames,
                         sf::Texture* walk, int walkFrames,
                         int frameW, int frameH,
                         int targetDisplaySize = 0)
    {
        m_tex_idle    = idle;
        m_tex_walk    = walk;
        m_idle_frames = idleFrames;
        m_walk_frames = walkFrames;
        m_frame_w     = frameW;
        m_frame_h     = frameH;
        m_frame       = 0;
        m_frame_timer = 0.f;
        m_anim_state  = AnimState::IDLE;
        m_anim_scale  = (targetDisplaySize > 0)
                        ? static_cast<float>(targetDisplaySize) / frameW
                        : 1.f;

        m_sprite.setTexture(*idle);
        m_sprite.setTextureRect(sf::IntRect(0, 0, frameW, frameH));
        // Center origin → sprite is centered on its tile position
        m_sprite.setOrigin(frameW / 2.f, frameH / 2.f);
    }

    // Orc only: 5-state animation setup (idle/walk/attack/hurt/death)
    void SetOrcAnimTextures(sf::Texture* idle,   int idleF,
                            sf::Texture* walk,   int walkF,
                            sf::Texture* attack, int atkF,
                            sf::Texture* hurt,   int hurtF,
                            sf::Texture* death,  int deathF,
                            int frameW, int frameH, int displaySize = 0)
    {
        SetAnimTextures(idle, idleF, walk, walkF, frameW, frameH, displaySize);
        m_tex_attack   = attack;
        m_tex_hurt     = hurt;
        m_tex_death    = death;
        m_atk_frames   = atkF;
        m_hurt_frames  = hurtF;
        m_death_frames = deathF;
        m_death_done   = false;
    }

    // Set attack + hurt textures after SetAnimTextures (used for Soldier).
    // atk2/atk3 may be nullptr if there is no alternate/skill attack animation.
    void SetAttackHurtTextures(sf::Texture* atk,   int atkF,
                               sf::Texture* hurt,  int hurtF,
                               sf::Texture* atk2  = nullptr,
                               sf::Texture* atk3  = nullptr, int skillF = 1)
    {
        m_tex_attack   = atk;
        m_tex_attack2  = atk2;
        m_tex_attack3  = atk3;
        m_tex_hurt     = hurt;
        m_atk_frames   = atkF;
        m_skill_frames = skillF;
        m_hurt_frames  = hurtF;
    }

    void SetAttacking()
    {
        if (m_tex_attack == nullptr) return;
        if (m_anim_state == AnimState::DEATH) return; // ignore during death anim

        // Toggle between attack1 and attack2 on each activation
        sf::Texture* tex = m_tex_attack;
        if (m_tex_attack2 != nullptr) {
            m_atk_toggle = !m_atk_toggle;
            tex = m_atk_toggle ? m_tex_attack2 : m_tex_attack;
        }

        m_anim_state  = AnimState::ATTACK;
        m_frame       = 0;
        m_frame_timer = 0.f;
        m_sprite.setTexture(*tex);
        m_sprite.setTextureRect(sf::IntRect(0, 0, m_frame_w, m_frame_h));
    }

    void SetSkillAttacking()
    {
        if (m_tex_attack3 == nullptr) return;
        if (m_anim_state == AnimState::DEATH) return;
        m_is_skill    = true;
        m_anim_state  = AnimState::ATTACK;
        m_frame       = 0;
        m_frame_timer = 0.f;
        m_sprite.setTexture(*m_tex_attack3);
        m_sprite.setTextureRect(sf::IntRect(0, 0, m_frame_w, m_frame_h));
    }

    void SetDying()
    {
        if (m_tex_death == nullptr) return;
        m_anim_state  = AnimState::DEATH;
        m_frame       = 0;
        m_frame_timer = 0.f;
        m_death_done  = false;
        m_sprite.setTexture(*m_tex_death);
        m_sprite.setTextureRect(sf::IntRect(0, 0, m_frame_w, m_frame_h));
    }

    void SetHurt()
    {
        if (m_anim_state == AnimState::DEATH) return;

        // Always trigger the red flash, regardless of current anim state
        m_hurt_flash = HURT_FLASH_DURATION;

        // Don't interrupt an in-progress attack; the flash alone signals the hit
        if (m_anim_state == AnimState::ATTACK) return;

        if (m_tex_hurt == nullptr) return;
        m_anim_state  = AnimState::HURT;
        m_frame       = 0;
        m_frame_timer = 0.f;
        m_sprite.setTexture(*m_tex_hurt);
        m_sprite.setTextureRect(sf::IntRect(0, 0, m_frame_w, m_frame_h));
    }

    bool IsDeathDone() const { return m_death_done; }

    // Switch to walk animation and update facing direction.
    // direction: 0=up 1=down 2=left 3=right 4=up-left 5=up-right 6=down-left 7=down-right
    void SetWalking(int direction)
    {
        if (m_tex_walk == nullptr) return;
        // Don't interrupt one-shot animations
        if (m_anim_state == AnimState::ATTACK || m_anim_state == AnimState::HURT) return;

        if (direction == 2 || direction == 4 || direction == 6) m_flip_x = true;
        if (direction == 3 || direction == 5 || direction == 7) m_flip_x = false;

        if (m_anim_state != AnimState::WALK) {
            m_anim_state  = AnimState::WALK;
            m_frame       = 0;
            m_frame_timer = 0.f;
            m_sprite.setTexture(*m_tex_walk);
            m_sprite.setTextureRect(sf::IntRect(0, 0, m_frame_w, m_frame_h));
        }
    }

    bool IsOneShot() const
    {
        return m_anim_state == AnimState::ATTACK || m_anim_state == AnimState::HURT;
    }

    // Update horizontal facing without changing animation state.
    // faceLeft=true → sprite flipped (facing left), false → normal (facing right).
    void SetFacing(bool faceLeft) { m_flip_x = faceLeft; }
    bool IsFacingLeft() const { return m_flip_x; }

    // Switch to idle animation.
    void SetIdle()
    {
        if (m_tex_idle == nullptr) return;
        m_is_skill = false;
        if (m_anim_state != AnimState::IDLE) {
            m_anim_state  = AnimState::IDLE;
            m_frame       = 0;
            m_frame_timer = 0.f;
            m_sprite.setTexture(*m_tex_idle);
            m_sprite.setTextureRect(sf::IntRect(0, 0, m_frame_w, m_frame_h));
        }
    }

    // Advance the animation frame. Call once per frame with dt in seconds.
    void UpdateAnim(float dt)
    {
        if (m_tex_idle == nullptr) return;
        if (m_death_done) return; // freeze after death anim completes

        float fps   = IDLE_FPS;
        int   total = m_idle_frames;
        switch (m_anim_state)
        {
        case AnimState::IDLE:   fps = IDLE_FPS;     total = m_idle_frames;  break;
        case AnimState::WALK:   fps = WALK_FPS;     total = m_walk_frames;  break;
        case AnimState::ATTACK: fps = 1.f / ORC_FRAME_SEC; total = m_is_skill ? m_skill_frames : m_atk_frames; break;
        case AnimState::HURT:   fps = 1.f / ORC_FRAME_SEC; total = m_hurt_frames;  break;
        case AnimState::DEATH:  fps = 1.f / ORC_FRAME_SEC; total = m_death_frames; break;
        }

        m_frame_timer += dt;
        if (m_frame_timer >= 1.f / fps)
        {
            m_frame_timer -= 1.f / fps;

            if (m_anim_state == AnimState::ATTACK || m_anim_state == AnimState::HURT)
            {
                // play once then return to Idle
                if (m_frame + 1 >= total) { SetIdle(); }
                else { ++m_frame; }
            }
            else if (m_anim_state == AnimState::DEATH)
            {
                // play death anim once then freeze on last frame
                if (m_frame + 1 >= total) { m_death_done = true; }
                else { ++m_frame; }
            }
            else
            {
                m_frame = (m_frame + 1) % total;
            }

            m_sprite.setTextureRect(sf::IntRect(m_frame * m_frame_w, 0, m_frame_w, m_frame_h));
        }

        float scaleX = m_flip_x ? -m_anim_scale : m_anim_scale;
        m_sprite.setScale(scaleX, m_anim_scale);

        // Red flash overlay for hurt (independent of animation state)
        if (m_hurt_flash > 0.f)
        {
            m_hurt_flash -= dt;
            m_sprite.setColor(sf::Color(255, 100, 100, 255));
        }
        else
        {
            m_sprite.setColor(sf::Color::White);
        }

        // Smooth position interpolation toward logical tile
        constexpr float MOVE_LERP = 10.f;
        m_vis_x += ((float)m_x - m_vis_x) * min(1.f, MOVE_LERP * dt);
        m_vis_y += ((float)m_y - m_vis_y) * min(1.f, MOVE_LERP * dt);
    }
    // ─────────────────────────────────────────────────────────────────────────

    void show() { m_showing = true; m_vis_x = (float)m_x; m_vis_y = (float)m_y; }
    void hide() { m_showing = false; }

    // Scale a static (non-animated) sprite. Has no effect on animated sprites
    // (those use displaySize / frameW ratio set in SetAnimTextures).
    void SetScale(float s) { if (m_tex_idle == nullptr) m_sprite.setScale(s, s); }

    void a_move(float x, float y) { m_sprite.setPosition(x, y); }
    void a_draw()              { g_window->draw(m_sprite); }

    void move(int x, int y) { m_x = x; m_y = y; }

    void draw()
    {
        if (!m_showing) return;

        float rx = (m_vis_x - g_cam_x) * TILE_WIDTH + 1.f;
        float ry = (m_vis_y - g_cam_y) * TILE_WIDTH + 1.f;

        if (m_tex_idle != nullptr) {
            // Animated: center on tile
            float cx = rx + TILE_WIDTH / 2.f;
            float cy = ry + TILE_WIDTH / 2.f;
            m_sprite.setPosition(cx, cy);
            g_window->draw(m_sprite);

            float labelY = ry - 14.f;
            if (m_mess_end_time < chrono::system_clock::now()) {
                auto size = m_name.getGlobalBounds();
                m_name.setPosition(cx - size.width / 2.f, labelY);
                g_window->draw(m_name);
            } else {
                auto cb = m_chat.getLocalBounds();
                float bw = cb.width + 10.f;
                float bh = cb.height + 8.f;
                float bx = cx - bw / 2.f;
                float by = labelY - bh - 2.f;
                sf::RectangleShape bubble(sf::Vector2f(bw, bh));
                bubble.setFillColor(sf::Color(20, 20, 20, 200));
                bubble.setOutlineColor(sf::Color(220, 220, 220, 160));
                bubble.setOutlineThickness(1.f);
                bubble.setPosition(bx, by);
                g_window->draw(bubble);
                m_chat.setPosition(bx + 5.f, by + 2.f);
                g_window->draw(m_chat);
            }
        } else {
            // Static sprite (original behavior)
            m_sprite.setPosition(rx, ry);
            g_window->draw(m_sprite);

            auto size = m_name.getGlobalBounds();
            if (m_mess_end_time < chrono::system_clock::now()) {
                m_name.setPosition(rx + 32 - size.width / 2.f, ry - 10.f);
                g_window->draw(m_name);
            } else {
                auto cb = m_chat.getLocalBounds();
                float bw = cb.width + 10.f;
                float bh = cb.height + 8.f;
                float bx = rx + 32 - bw / 2.f;
                float by = ry - 10.f - bh - 2.f;
                sf::RectangleShape bubble(sf::Vector2f(bw, bh));
                bubble.setFillColor(sf::Color(20, 20, 20, 200));
                bubble.setOutlineColor(sf::Color(220, 220, 220, 160));
                bubble.setOutlineThickness(1.f);
                bubble.setPosition(bx, by);
                g_window->draw(bubble);
                m_chat.setPosition(bx + 5.f, by + 2.f);
                g_window->draw(m_chat);
            }
        }
    }

    void set_name(const char str[])
    {
        name_str = str;
        m_name.setFont(*g_font);
        m_name.setString(str);
        if (static_cast<EnumCategory>(id.GetCategory()) == EnumCategory::eUser)
            m_name.setFillColor(sf::Color(255, 255, 255));
        else
            m_name.setFillColor(sf::Color(255, 255, 0));
        m_name.setStyle(sf::Text::Bold);
        m_name.setCharacterSize(12);
    }

    void set_chat(const char str[])
    {
        m_chat.setFont(*g_font);
        m_chat.setCharacterSize(13);
        m_chat.setString(str);
        m_chat.setFillColor(sf::Color(255, 255, 255));
        m_chat.setStyle(sf::Text::Bold);
        m_mess_end_time = chrono::system_clock::now() + chrono::seconds(4);
    }
};

OBJECT avatar;
OBJECT obstacle;

unordered_map<ObjID, OBJECT> players;
unordered_map<ObjID, OBJECT> g_pendingObjects;  // ADD_NFY buffer during FADE_OUT

OBJECT tile1;
OBJECT tile2;

sf::Texture* board1;
sf::Texture* board2;
sf::Texture* wall;

// Slime (passive monster) textures
sf::Texture* slime_idle_tex;
sf::Texture* slime_walk_tex;
sf::Texture* slime_atk_tex;
sf::Texture* slime_hurt_tex;
sf::Texture* slime_death_tex;

// Soldier textures
sf::Texture* soldier_idle_tex;
sf::Texture* soldier_walk_tex;
sf::Texture* soldier_atk_tex;
sf::Texture* soldier_atk2_tex;
sf::Texture* soldier_atk3_tex;
sf::Texture* soldier_hurt_tex;

// Orc textures (Tiny RPG Character Asset Pack v1.03)
sf::Texture* orc_idle_tex;
sf::Texture* orc_walk_tex;
sf::Texture* orc_atk_tex;
sf::Texture* orc_hurt_tex;
sf::Texture* orc_death_tex;

// Respawn countdown state
bool g_isDead = false;
chrono::steady_clock::time_point g_deathTime;
constexpr float RESPAWN_DELAY_SEC = 30.f;

// Delta-time clock
sf::Clock g_clock;
// Movement rate clock (send move packet at most every 250ms)
sf::Clock g_moveClock;
// Attack cooldown clock (1 second between attacks, matches server cooldown)
sf::Clock g_attackClock;
// Skill cooldown clock (5 seconds, matches server cooldown)
sf::Clock g_skillClock;

// ─── Floating damage numbers ──────────────────────────────────────────────────
struct DamageNumber
{
    sf::Text text;
    float    x, y;     // screen-space position (pixels)
    float    life;     // remaining lifetime in seconds
    static constexpr float MAX_LIFE  = 1.2f;
    static constexpr float RISE_SPEED = 40.f; // pixels per second upward
};
std::vector<DamageNumber> g_damageNumbers;

// ─── Zone transition ──────────────────────────────────────────────────────────
constexpr int   ZONE_TILE_SIZE    = 500;  // SectorsPerZoneX(50) * SECTOR_RANGE(10)
constexpr float ZONE_FADE_OUT_DUR = 0.5f;
constexpr float ZONE_LOADING_DUR  = 0.8f;
constexpr float ZONE_FADE_IN_DUR  = 0.5f;
constexpr int   ZONE_COUNT_X      = 4;   // ZoneCountX from ZoneLayout

enum class ZoneTransState { NONE, FADE_OUT, LOADING, FADE_IN };
ZoneTransState g_zoneTransState  = ZoneTransState::NONE;
sf::Clock      g_zoneTransClock;
int            g_currentZoneCol  = -1;   // -1 = not yet initialized
int            g_currentZoneRow  = -1;
int            g_pendingPlayerX  = 0;
int            g_pendingPlayerY  = 0;
// ─────────────────────────────────────────────────────────────────────────────

void SpawnDamageNumber(int worldX, int worldY, int32_t damage)
{
    DamageNumber dn;
    dn.text.setFont(*g_font);
    dn.text.setCharacterSize(20);
    dn.text.setStyle(sf::Text::Bold);
    dn.text.setFillColor(sf::Color(255, 80, 80));
    dn.text.setOutlineColor(sf::Color::Black);
    dn.text.setOutlineThickness(1.f);

    char buf[16];
    sprintf_s(buf, "-%d", damage);
    dn.text.setString(buf);

    // Convert world tile to screen pixel position (use float camera for accuracy)
    dn.x = (worldX - g_cam_x) * TILE_WIDTH + TILE_WIDTH / 2.f;
    dn.y = (worldY - g_cam_y) * TILE_WIDTH - 10.f;
    dn.life = DamageNumber::MAX_LIFE;
    g_damageNumbers.push_back(std::move(dn));
}
// ─────────────────────────────────────────────────────────────────────────────

// Returns true if any monster currently occupies tile (x, y).
bool IsTileOccupiedByMonster(int x, int y)
{
    for (const auto& [id, obj] : players)
    {
        if (static_cast<EnumCategory>(id.GetCategory()) != EnumCategory::eMonster) continue;
        if (obj.m_x == x && obj.m_y == y) return true;
    }
    return false;
}

inline int GetZoneCol(int wx) noexcept { return wx / ZONE_TILE_SIZE; }
inline int GetZoneRow(int wy) noexcept { return wy / ZONE_TILE_SIZE; }

void TriggerZoneTransition(int newX, int newY)
{
    g_pendingPlayerX = newX;
    g_pendingPlayerY = newY;
    g_currentZoneCol = GetZoneCol(newX);
    g_currentZoneRow = GetZoneRow(newY);
    players.clear();          // immediately remove old-zone objects
    g_pendingObjects.clear(); // discard any leftover buffer
    g_zoneTransState = ZoneTransState::FADE_OUT;
    g_zoneTransClock.restart();
}

// Forward declaration for send_packet used before its definition
void send_packet(void* packet);

// ─── Login helpers ────────────────────────────────────────────────────────────
static void DoLogin()
{
    if (g_loginId.empty()) return;
    USER_LOGIN_REQ_PACKET p{};
    p.size = sizeof(p);
    p.type = static_cast<char>(PacketType::USER_LOGIN_REQ);
    ::strncpy_s(p.name, g_loginId.c_str(), NAME_SIZE - 1);
    send_packet(&p);
    avatar.set_name(p.name);
    g_clock.restart();   // reset dt so first game frame isn't huge
    g_clientState = ClientState::INGAME;
}

static void DrawLoginScreen(float dt)
{
    if (!g_font || !g_window) return;
    const float CX = WINDOW_WIDTH  / 2.f;
    const float CY = WINDOW_HEIGHT / 2.f;

    sf::RectangleShape bg(sf::Vector2f((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
    bg.setFillColor(sf::Color(12, 12, 28));
    g_window->draw(bg);

    // Title
    sf::Text title;
    title.setFont(*g_font);
    title.setCharacterSize(36);
    title.setStyle(sf::Text::Bold);
    title.setFillColor(sf::Color(170, 205, 255));
    title.setString("2D  MMORPG");
    auto tb = title.getLocalBounds();
    title.setOrigin(tb.left + tb.width/2.f, tb.top + tb.height/2.f);
    title.setPosition(CX, CY - 130.f);
    g_window->draw(title);

    // Cursor blink
    g_loginCursor += dt;
    const bool blink = (int)(g_loginCursor * 2.f) % 2 == 0;

    const float FW = 260.f, FH = 34.f;
    const float FX = CX - FW / 2.f;
    const float LX = FX - 42.f;
    const float ID_Y = CY - 48.f;
    const float PW_Y = CY + 14.f;

    auto drawField = [&](const char* lbl, const string& val, float fy, bool active, bool mask)
    {
        sf::Text lt;
        lt.setFont(*g_font); lt.setCharacterSize(15);
        lt.setFillColor(sf::Color(175, 175, 200));
        lt.setString(lbl);
        lt.setPosition(LX, fy + 9.f);
        g_window->draw(lt);

        sf::RectangleShape box(sf::Vector2f(FW, FH));
        box.setFillColor(active ? sf::Color(26,30,62,245) : sf::Color(18,20,44,200));
        box.setOutlineColor(active ? sf::Color(95,165,255) : sf::Color(50,78,138));
        box.setOutlineThickness(1.5f);
        box.setPosition(FX, fy);
        g_window->draw(box);

        sf::Text it;
        it.setFont(*g_font); it.setCharacterSize(15);
        it.setFillColor(sf::Color::White);
        string disp = mask ? string(val.size(), '*') : val;
        if (active && blink) disp += '|';
        it.setString(disp);
        it.setPosition(FX + 8.f, fy + 9.f);
        g_window->draw(it);
    };

    drawField("ID", g_loginId,   ID_Y, g_loginField == 0, false);
    drawField("PW", g_loginPass, PW_Y, g_loginField == 1, true);

    // ENTER GAME button
    const float BW = 180.f, BH = 40.f;
    const float BX = CX - BW / 2.f;
    const float BY = CY + 78.f;
    sf::RectangleShape btn(sf::Vector2f(BW, BH));
    btn.setFillColor(sf::Color(32, 72, 152));
    btn.setOutlineColor(sf::Color(85, 155, 255));
    btn.setOutlineThickness(1.5f);
    btn.setPosition(BX, BY);
    g_window->draw(btn);

    sf::Text btnTxt;
    btnTxt.setFont(*g_font); btnTxt.setCharacterSize(18);
    btnTxt.setStyle(sf::Text::Bold);
    btnTxt.setFillColor(sf::Color::White);
    btnTxt.setString("ENTER GAME");
    auto bb = btnTxt.getLocalBounds();
    btnTxt.setOrigin(bb.left + bb.width/2.f, bb.top + bb.height/2.f);
    btnTxt.setPosition(CX, BY + BH/2.f);
    g_window->draw(btnTxt);
}
// ─────────────────────────────────────────────────────────────────────────────

void client_initialize()
{
    board1 = new sf::Texture;
    board2 = new sf::Texture;
    wall   = new sf::Texture;

    board1->loadFromFile("floor1.png");
    board2->loadFromFile("floor2.png");
    wall->loadFromFile("obstacle.png");

    // Slime (passive monster) sprite sheets
    slime_idle_tex  = new sf::Texture;
    slime_walk_tex  = new sf::Texture;
    slime_atk_tex   = new sf::Texture;
    slime_hurt_tex  = new sf::Texture;
    slime_death_tex = new sf::Texture;
    if (!slime_idle_tex->loadFromFile("Monster_Slime_Idle-Sheet.png"))
        cout << "Failed to load Monster_Slime_Idle-Sheet.png\n";
    if (!slime_walk_tex->loadFromFile("Monster_Slime_Walk-Sheet.png"))
        cout << "Failed to load Monster_Slime_Walk-Sheet.png\n";
    if (!slime_atk_tex->loadFromFile("Monster_Slime_Attack1-Sheet.png"))
        cout << "Failed to load Monster_Slime_Attack1-Sheet.png\n";
    if (!slime_hurt_tex->loadFromFile("Monster_Slime_Hurt-Sheet.png"))
        cout << "Failed to load Monster_Slime_Hurt-Sheet.png\n";
    if (!slime_death_tex->loadFromFile("Monster_Slime_Death-Sheet.png"))
        cout << "Failed to load Monster_Slime_Death-Sheet.png\n";

    // Soldier sprite sheets
    soldier_idle_tex = new sf::Texture;
    soldier_walk_tex = new sf::Texture;
    soldier_atk_tex  = new sf::Texture;
    soldier_atk2_tex = new sf::Texture;
    soldier_atk3_tex = new sf::Texture;
    soldier_hurt_tex = new sf::Texture;
    if (!soldier_idle_tex->loadFromFile("Soldier-Idle.png"))
        cout << "Failed to load Soldier-Idle.png\n";
    if (!soldier_walk_tex->loadFromFile("Soldier-Walk.png"))
        cout << "Failed to load Soldier-Walk.png\n";
    if (!soldier_atk_tex->loadFromFile("Soldier-Attack01.png"))
        cout << "Failed to load Soldier-Attack01.png\n";
    if (!soldier_atk2_tex->loadFromFile("Soldier-Attack02.png"))
        cout << "Failed to load Soldier-Attack02.png\n";
    if (!soldier_atk3_tex->loadFromFile("Soldier-Attack03.png"))
        cout << "Failed to load Soldier-Attack03.png\n";
    if (!soldier_hurt_tex->loadFromFile("Soldier-Hurt.png"))
        cout << "Failed to load Soldier-Hurt.png\n";

    // Orc sprite sheets (Tiny RPG Character Asset Pack v1.03)
    orc_idle_tex  = new sf::Texture;
    orc_walk_tex  = new sf::Texture;
    orc_atk_tex   = new sf::Texture;
    orc_hurt_tex  = new sf::Texture;
    orc_death_tex = new sf::Texture;
    if (!orc_idle_tex->loadFromFile("Orc-Idle.png"))
        cout << "Failed to load Orc-Idle.png\n";
    if (!orc_walk_tex->loadFromFile("Orc-Walk.png"))
        cout << "Failed to load Orc-Walk.png\n";
    if (!orc_atk_tex->loadFromFile("Orc-Attack01.png"))
        cout << "Failed to load Orc-Attack01.png\n";
    if (!orc_hurt_tex->loadFromFile("Orc-Hurt.png"))
        cout << "Failed to load Orc-Hurt.png\n";
    if (!orc_death_tex->loadFromFile("Orc-Death.png"))
        cout << "Failed to load Orc-Death.png\n";

    g_font = new sf::Font();
    if (!g_font->loadFromFile("cour.ttf")) {
        cout << "Font Loading Error!\n";
        exit(-1);
    }

    tile1    = OBJECT{ *board1, 0, 0, TILE_WIDTH, TILE_WIDTH };
    tile2    = OBJECT{ *board2, 0, 0, TILE_WIDTH, TILE_WIDTH };
    obstacle = OBJECT{ *wall,   0, 0, TILE_WIDTH, TILE_WIDTH };

    // Avatar: animated Soldier with shadow
    avatar = OBJECT{};
    avatar.SetAnimTextures(
        soldier_idle_tex, SOLDIER_IDLE_FRAMES,
        soldier_walk_tex, SOLDIER_WALK_FRAMES,
        SOLDIER_FRAME_W,  SOLDIER_FRAME_H,
        256);
    avatar.SetAttackHurtTextures(
        soldier_atk_tex,  SOLDIER_ATK_FRAMES,
        soldier_hurt_tex, SOLDIER_HURT_FRAMES,
        soldier_atk2_tex,
        soldier_atk3_tex, SOLDIER_SKILL_FRAMES);

    avatar.move(4, 4);
    avatar.hp    = 0;
    avatar.level = 0;

    g_clock.restart();
}

void client_finish()
{
    players.clear();
    delete board1;
    delete board2;
    delete wall;
    delete slime_idle_tex;
    delete slime_walk_tex;
    delete slime_atk_tex;
    delete slime_hurt_tex;
    delete slime_death_tex;
    delete soldier_idle_tex;
    delete soldier_walk_tex;
    delete soldier_atk_tex;
    delete soldier_atk2_tex;
    delete soldier_atk3_tex;
    delete soldier_hurt_tex;
    delete orc_idle_tex;
    delete orc_walk_tex;
    delete orc_atk_tex;
    delete orc_hurt_tex;
    delete orc_death_tex;
    delete g_font;
    exit(0);
    g_font = nullptr;
}

static void PushHistory(const string& msg);

void ProcessPacket(char* ptr)
{
    static bool first_time = true;
    switch (ptr[2]) {
    case static_cast<char>(PacketType::USER_LOGIN_ACK):
    {
        USER_LOGIN_ACK_PACKET* packet = reinterpret_cast<USER_LOGIN_ACK_PACKET*>(ptr);
        g_myid = packet->id;
        avatar.id = g_myid;
        avatar.move(packet->x, packet->y);
        g_cam_x  = (float)(packet->x - SCREEN_WIDTH  / 2);
        g_cam_y  = (float)(packet->y - SCREEN_HEIGHT / 2);
        g_left_x = (int)g_cam_x;
        g_top_y  = (int)g_cam_y;
        avatar.maxhp = packet->maxhp;
        avatar.hp    = packet->hp;
        avatar.level = packet->level;
        avatar.exp   = packet->exp;
        avatar.show();
        g_currentZoneCol = GetZoneCol(packet->x);
        g_currentZoneRow = GetZoneRow(packet->y);
    }
    break;

    case static_cast<char>(PacketType::USER_LOGIN_FAIL_ACK):
        PushHistory("> Login failed. Check account name.");
        printf("Login failed (USER_LOGIN_FAIL_ACK received)\n");
        break;

    case static_cast<char>(PacketType::SUBJECT_ADD_NFY):
    {
        SUBJECT_ADD_NFY_PACKET* my_packet = reinterpret_cast<SUBJECT_ADD_NFY_PACKET*>(ptr);
        ObjID id = my_packet->id;

        if (id == g_myid) {
            avatar.move(my_packet->x, my_packet->y);
            g_cam_x  = (float)(my_packet->x - SCREEN_WIDTH  / 2);
            g_cam_y  = (float)(my_packet->y - SCREEN_HEIGHT / 2);
            g_left_x = (int)g_cam_x;
            g_top_y  = (int)g_cam_y;
            avatar.show();
            break;
        }

        // During FADE_OUT the screen isn't fully black yet — buffer objects instead
        // of adding to players. They are flushed to players when LOADING starts.
        auto& dest = (g_zoneTransState == ZoneTransState::FADE_OUT)
            ? g_pendingObjects : players;

        if (static_cast<EnumCategory>(id.GetCategory()) == EnumCategory::eUser) {
            dest[id] = OBJECT{};
            dest[id].SetAnimTextures(
                soldier_idle_tex, SOLDIER_IDLE_FRAMES,
                soldier_walk_tex, SOLDIER_WALK_FRAMES,
                SOLDIER_FRAME_W,  SOLDIER_FRAME_H,
                256);
            dest[id].SetAttackHurtTextures(
                soldier_atk_tex,  SOLDIER_ATK_FRAMES,
                soldier_hurt_tex, SOLDIER_HURT_FRAMES,
                soldier_atk2_tex,
                soldier_atk3_tex, SOLDIER_SKILL_FRAMES);
            dest[id].id = id;
            dest[id].move(my_packet->x, my_packet->y);
            dest[id].set_name(my_packet->name);
            dest[id].show();
        }
        else {
            if (my_packet->monster_type == MONSTER_TYPE::PASSIVE) {
                dest[id] = OBJECT{};
                dest[id].SetOrcAnimTextures(
                    slime_idle_tex,  SLIME_IDLE_FRAMES,
                    slime_walk_tex,  SLIME_WALK_FRAMES,
                    slime_atk_tex,   SLIME_ATK_FRAMES,
                    slime_hurt_tex,  SLIME_HURT_FRAMES,
                    slime_death_tex, SLIME_DEATH_FRAMES,
                    SLIME_FRAME_W, SLIME_FRAME_H, SLIME_DISPLAY_SIZE);
            } else {
                dest[id] = OBJECT{};
                dest[id].SetOrcAnimTextures(
                    orc_idle_tex,  ORC_IDLE_FRAMES,
                    orc_walk_tex,  ORC_WALK_FRAMES,
                    orc_atk_tex,   ORC_ATK_FRAMES,
                    orc_hurt_tex,  ORC_HURT_FRAMES,
                    orc_death_tex, ORC_DEATH_FRAMES,
                    ORC_FRAME_W, ORC_FRAME_H, ORC_DISPLAY_SIZE);
            }
            dest[id].id = id;
            dest[id].move(my_packet->x, my_packet->y);
            dest[id].set_name(my_packet->name);
            dest[id].show();
        }
        break;
    }

    case static_cast<char>(PacketType::SUBJECT_MOVE_NFY):
    {
        SUBJECT_MOVE_NFY_PACKET* my_packet = reinterpret_cast<SUBJECT_MOVE_NFY_PACKET*>(ptr);
        ObjID other_id = my_packet->id;
        if (other_id == g_myid) {
            const int newCol = GetZoneCol(my_packet->x);
            const int newRow = GetZoneRow(my_packet->y);
            const bool zoneChanged = (g_currentZoneCol >= 0)
                && (newCol != g_currentZoneCol || newRow != g_currentZoneRow);

            if (zoneChanged && g_zoneTransState == ZoneTransState::NONE) {
                TriggerZoneTransition(my_packet->x, my_packet->y);
            } else if (g_zoneTransState == ZoneTransState::NONE) {
                avatar.move(my_packet->x, my_packet->y);
                // g_cam_x/y lerp toward new position each frame — no instant snap
            }
            // skip position update while zone transition is in progress
        } else {
            if (players.count(other_id)) {
                auto& obj = players[other_id];
                if (static_cast<EnumCategory>(other_id.GetCategory()) == EnumCategory::eMonster)
                {
                    const int dx = my_packet->x - obj.m_x;
                    const int dy = my_packet->y - obj.m_y;
                    // Pick a direction: horizontal takes priority for flip;
                    // pure vertical movement uses up/down (no flip change).
                    int direction;
                    if      (dx < 0) direction = 2; // left
                    else if (dx > 0) direction = 3; // right
                    else if (dy < 0) direction = 0; // up   (flip unchanged)
                    else             direction = 1; // down (flip unchanged)
                    obj.move(my_packet->x, my_packet->y);
                    obj.SetWalking(direction);
                }
                else
                {
                    obj.move(my_packet->x, my_packet->y);
                }
            }
        }
        break;
    }

    case static_cast<char>(PacketType::SUBJECT_REMOVE_NFY):
    {
        SUBJECT_REMOVE_NFY_PACKET* my_packet = reinterpret_cast<SUBJECT_REMOVE_NFY_PACKET*>(ptr);
        ObjID other_id = my_packet->id;
        if (other_id == g_myid)
            avatar.hide();
        else
            players.erase(other_id);
        break;
    }

    case static_cast<char>(PacketType::USER_ATTACK_ACK):
    {
        USER_ATTACK_ACK_PACKET* packet = reinterpret_cast<USER_ATTACK_ACK_PACKET*>(ptr);
        if (players.count(packet->id))
        {
            players[packet->id].SetHurt();
            SpawnDamageNumber(players[packet->id].m_x, players[packet->id].m_y, packet->damage);
        }
        break;
    }

    case static_cast<char>(PacketType::USER_STAT_CHANGE_INF):
    {
        USER_STAT_CHANGE_INF_PACKET* packet = reinterpret_cast<USER_STAT_CHANGE_INF_PACKET*>(ptr);
        avatar.level = packet->level;
        avatar.hp    = packet->hp;
        avatar.maxhp = packet->maxhp;
        avatar.exp   = packet->exp;
        break;
    }

    case static_cast<char>(PacketType::SUBJECT_DIE_NFY):
    {
        SUBJECT_DIE_NFY_PACKET* packet = reinterpret_cast<SUBJECT_DIE_NFY_PACKET*>(ptr);
        if (static_cast<EnumCategory>(packet->id.GetCategory()) == EnumCategory::eMonster)
        {
            if (players.count(packet->id))
                players[packet->id].SetDying(); // play death anim; render loop removes it
            else
                players.erase(packet->id);
        }
        else
        {
            if (packet->id == g_myid) {
                avatar.hp = packet->hp;
                avatar.hide();
                g_isDead   = true;
                g_deathTime = chrono::steady_clock::now();
            } else if (players.count(packet->id)) {
                players[packet->id].hp = packet->hp;
                players[packet->id].hide();
            }
        }
        break;
    }

    case static_cast<char>(PacketType::SUBJECT_RESPAWN_NFY):
    {
        SUBJECT_RESPAWN_NFY_PACKET* packet = reinterpret_cast<SUBJECT_RESPAWN_NFY_PACKET*>(ptr);
        if (packet->id == g_myid) {
            avatar.move(packet->x, packet->y);
            g_cam_x  = (float)(packet->x - SCREEN_WIDTH  / 2);
            g_cam_y  = (float)(packet->y - SCREEN_HEIGHT / 2);
            g_left_x = (int)g_cam_x;
            g_top_y  = (int)g_cam_y;
            avatar.hp = packet->hp;
            avatar.SetIdle();
            avatar.show();
            g_isDead  = false;
        } else if (players.count(packet->id)) {
            players[packet->id].move(packet->x, packet->y);
            players[packet->id].hp = packet->hp;
            players[packet->id].SetIdle();
            players[packet->id].show();
        } else {
            if (static_cast<EnumCategory>(packet->id.GetCategory()) == EnumCategory::eUser) {
                players[packet->id] = OBJECT{};
                players[packet->id].SetAnimTextures(
                    soldier_idle_tex, SOLDIER_IDLE_FRAMES,
                    soldier_walk_tex, SOLDIER_WALK_FRAMES,
                    SOLDIER_FRAME_W,  SOLDIER_FRAME_H,
                    256);
                players[packet->id].SetAttackHurtTextures(
                    soldier_atk_tex,  SOLDIER_ATK_FRAMES,
                    soldier_hurt_tex, SOLDIER_HURT_FRAMES,
                    soldier_atk2_tex,
                    soldier_atk3_tex, SOLDIER_SKILL_FRAMES);
            } else if (packet->monster_type == MONSTER_TYPE::PASSIVE) {
                players[packet->id] = OBJECT{};
                players[packet->id].SetOrcAnimTextures(
                    slime_idle_tex,  SLIME_IDLE_FRAMES,
                    slime_walk_tex,  SLIME_WALK_FRAMES,
                    slime_atk_tex,   SLIME_ATK_FRAMES,
                    slime_hurt_tex,  SLIME_HURT_FRAMES,
                    slime_death_tex, SLIME_DEATH_FRAMES,
                    SLIME_FRAME_W, SLIME_FRAME_H, SLIME_DISPLAY_SIZE);
            } else {
                players[packet->id] = OBJECT{};
                players[packet->id].SetOrcAnimTextures(
                    orc_idle_tex,  ORC_IDLE_FRAMES,
                    orc_walk_tex,  ORC_WALK_FRAMES,
                    orc_atk_tex,   ORC_ATK_FRAMES,
                    orc_hurt_tex,  ORC_HURT_FRAMES,
                    orc_death_tex, ORC_DEATH_FRAMES,
                    ORC_FRAME_W, ORC_FRAME_H, ORC_DISPLAY_SIZE);
            }

            players[packet->id].id = packet->id;
            players[packet->id].move(packet->x, packet->y);
            players[packet->id].hp = packet->hp;
            players[packet->id].set_name(packet->name);
            players[packet->id].SetIdle();
            players[packet->id].show();
        }
        break;
    }

    case static_cast<char>(PacketType::SUBJECT_ATTACK_NFY):
    {
        SUBJECT_ATTACK_NFY_PACKET* packet = reinterpret_cast<SUBJECT_ATTACK_NFY_PACKET*>(ptr);
        if (packet->victim_id == g_myid)
        {
            // Monster attacked me
            avatar.hp = packet->hp;
            avatar.SetHurt();
            if (players.count(packet->attacker_id))
            {
                auto& npc = players[packet->attacker_id];
                const int dx = avatar.m_x - npc.m_x;
                if (dx != 0) npc.SetFacing(dx < 0);
                npc.SetAttacking();
            }
        }
        else
        {
            // Monster attacked another player (bot) — show hurt on that player
            if (players.count(packet->victim_id))
            {
                players[packet->victim_id].hp = packet->hp;
                players[packet->victim_id].SetHurt();
            }
            if (players.count(packet->attacker_id))
                players[packet->attacker_id].SetAttacking();
        }
        break;
    }

    case static_cast<char>(PacketType::USER_HEAL_INF):
    {
        USER_HEAL_INF_PACKET* packet = reinterpret_cast<USER_HEAL_INF_PACKET*>(ptr);
        avatar.hp = packet->hp;
        break;
    }

    case static_cast<char>(PacketType::PLAYER_ATTACK_NFY):
    {
        PLAYER_ATTACK_NFY_PACKET* packet = reinterpret_cast<PLAYER_ATTACK_NFY_PACKET*>(ptr);
        if (players.count(packet->attacker_id))
        {
            auto& npc = players[packet->attacker_id];
            npc.SetFacing(packet->facing == 1);
            npc.SetAttacking();
        }
        break;
    }

    case static_cast<char>(PacketType::SC_CHAT):
    {
        SC_CHAT_PACKET* packet = reinterpret_cast<SC_CHAT_PACKET*>(ptr);
        char safe[CHAT_SIZE + 1]{};
        ::strncpy_s(safe, packet->mess, CHAT_SIZE);

        if (packet->sender_id == g_myid)
        {
            avatar.set_chat(safe);
            PushHistory(string("Me: ") + safe);
        }
        else if (players.count(packet->sender_id))
        {
            auto& npc = players[packet->sender_id];
            npc.set_chat(safe);
            string label = npc.name_str.empty() ? "??" : npc.name_str;
            PushHistory(label + ": " + safe);
        }
        break;
    }

    default:
        printf("Unknown PACKET type [%d]\n", ptr[2]);
    }
}

void process_data(char* net_buf, size_t io_byte)
{
    char* ptr = net_buf;
    static size_t in_packet_size    = 0;
    static size_t saved_packet_size = 0;
    static char   packet_buffer[BUF_SIZE];

    while (0 != io_byte) {
        if (0 == in_packet_size)
            in_packet_size = static_cast<unsigned char>(ptr[0]);

        if (io_byte + saved_packet_size >= in_packet_size) {
            memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
            ProcessPacket(packet_buffer);
            ptr      += in_packet_size - saved_packet_size;
            io_byte  -= in_packet_size - saved_packet_size;
            in_packet_size    = 0;
            saved_packet_size = 0;
        } else {
            memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
            saved_packet_size += io_byte;
            io_byte = 0;
        }
    }
}

void client_main()
{
    // Delta time
    float dt = g_clock.restart().asSeconds();

    // timer-based movement (send packet at most every 1000ms)
    const bool up    = sf::Keyboard::isKeyPressed(sf::Keyboard::Up);
    const bool down  = sf::Keyboard::isKeyPressed(sf::Keyboard::Down);
    const bool left  = sf::Keyboard::isKeyPressed(sf::Keyboard::Left);
    const bool right = sf::Keyboard::isKeyPressed(sf::Keyboard::Right);
    const bool moving = up || down || left || right;

    if (moving && g_zoneTransState == ZoneTransState::NONE && g_moveClock.getElapsedTime().asMilliseconds() >= 500)
    {
        g_moveClock.restart();

        // diagonal takes priority
        int direction = -1;
        if      (up   && left)  direction = 4;  // UP-LEFT
        else if (up   && right) direction = 5;  // UP-RIGHT
        else if (down && left)  direction = 6;  // DOWN-LEFT
        else if (down && right) direction = 7;  // DOWN-RIGHT
        else if (up)            direction = 0;
        else if (down)          direction = 1;
        else if (left)          direction = 2;
        else if (right)         direction = 3;

        if (direction != -1)
        {
            // Compute the target tile for this direction
            int tx = avatar.m_x, ty = avatar.m_y;
            switch (direction) {
            case 0: ty--;       break; // up
            case 1: ty++;       break; // down
            case 2: tx--;       break; // left
            case 3: tx++;       break; // right
            case 4: tx--; ty--; break; // up-left
            case 5: tx++; ty--; break; // up-right
            case 6: tx--; ty++; break; // down-left
            case 7: tx++; ty++; break; // down-right
            }

            // Block movement onto a tile occupied by a monster
            if (!IsTileOccupiedByMonster(tx, ty))
            {
                if (!avatar.IsOneShot())
                    avatar.SetWalking(direction);

                USER_MOVE_REQ_PACKET mp;
                mp.size      = sizeof(mp);
                mp.type      = static_cast<char>(PacketType::USER_MOVE_REQ);
                mp.direction = direction;
                mp.move_time = static_cast<unsigned>(
                    chrono::duration_cast<chrono::milliseconds>(
                        chrono::steady_clock::now().time_since_epoch()).count());
                send_packet(&mp);
            }
            else if (direction == 2 || direction == 3)
            {
                // Blocked but trying to move horizontally: update facing only
                if (!avatar.IsOneShot())
                    avatar.SetFacing(direction == 2);
            }
        }
    }
    else if (!moving)
    {
        if (!avatar.IsOneShot())
            avatar.SetIdle();
    }

    // animation update
    avatar.UpdateAnim(dt);

    // ── Smooth camera: lerp toward avatar's visual (interpolated) position ─
    {
        const float tcx = avatar.m_vis_x - SCREEN_WIDTH  / 2.f;
        const float tcy = avatar.m_vis_y - SCREEN_HEIGHT / 2.f;
        g_cam_x += (tcx - g_cam_x) * min(1.f, 10.f * dt);
        g_cam_y += (tcy - g_cam_y) * min(1.f, 10.f * dt);
        g_left_x = static_cast<int>(g_cam_x);
        g_top_y  = static_cast<int>(g_cam_y);
    }
    // ──────────────────────────────────────────────────────────────────────

    // Network receive
    char net_buf[BUF_SIZE];
    size_t received;
    auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
    if (recv_result == sf::Socket::Error)       { cout << "Recv error!\n"; exit(-1); }
    if (recv_result == sf::Socket::Disconnected){ cout << "Disconnected\n"; exit(-1); }
    if (recv_result != sf::Socket::NotReady)
        if (received > 0) process_data(net_buf, received);

    // Draw tiles (subpixel offset prevents tile-edge popping during camera lerp)
    {
        const float frac_px = (g_cam_x - (float)g_left_x) * TILE_WIDTH;
        const float frac_py = (g_cam_y - (float)g_top_y)  * TILE_WIDTH;
        for (int i = 0; i < SCREEN_WIDTH + 1; ++i)
            for (int j = 0; j < SCREEN_HEIGHT + 1; ++j)
            {
                int tile_x = i + g_left_x;
                int tile_y = j + g_top_y;
                if ((tile_x < 0) || (tile_y < 0) || tile_x >= W_WIDTH || tile_y >= W_HEIGHT) continue;

                const float sx = TILE_WIDTH * i - frac_px;
                const float sy = TILE_WIDTH * j - frac_py;

                if (0 == (tile_x / 3 + tile_y / 3) % 3) {
                    tile1.a_move(sx, sy);
                    tile1.a_draw();
                } else if (1 == (tile_x / 3 + tile_y / 3) % 3) {
                    tile2.a_move(sx, sy);
                    tile2.a_draw();
                } else {
                    if (0 == (tile_x / 2 + tile_y / 2) % 3) {
                        obstacle.a_move(sx, sy);
                        obstacle.a_draw();
                    } else if (1 == (tile_x / 2 + tile_y / 2) % 3) {
                        tile2.a_move(sx, sy);
                        tile2.a_draw();
                    } else {
                        tile1.a_move(sx, sy);
                        tile1.a_draw();
                    }
                }
            }
    }

    // update monster anims + remove death-completed objects
    {
        vector<ObjID> toRemove;
        for (auto& [id, obj] : players)
        {
            obj.UpdateAnim(dt);
            if (obj.IsDeathDone())
                toRemove.push_back(id);
        }
        for (const auto& id : toRemove)
            players.erase(id);
    }

    // Draw objects
    for (auto& pl : players) pl.second.draw();
    avatar.draw();

    // Respawn countdown (shown while dead)
    if (g_isDead)
    {
        const float elapsed = chrono::duration<float>(
            chrono::steady_clock::now() - g_deathTime).count();
        const float remaining = RESPAWN_DELAY_SEC - elapsed;
        const float displaySec = (remaining > 0.f) ? remaining : 0.f;

        char cdBuf[64];
        sprintf_s(cdBuf, "DEAD  Respawn in %.0f s", displaySec);

        sf::Text cdText;
        cdText.setFont(*g_font);
        cdText.setCharacterSize(32);
        cdText.setStyle(sf::Text::Bold);
        cdText.setFillColor(sf::Color(220, 40, 40));
        cdText.setOutlineColor(sf::Color::Black);
        cdText.setOutlineThickness(2.f);
        cdText.setString(cdBuf);

        const sf::FloatRect bounds = cdText.getLocalBounds();
        cdText.setOrigin(bounds.left + bounds.width / 2.f,
                         bounds.top  + bounds.height / 2.f);
        cdText.setPosition(WINDOW_WIDTH / 2.f, WINDOW_HEIGHT / 2.f);
        g_window->draw(cdText);
    }

    // Update + draw floating damage numbers
    {
        auto it = g_damageNumbers.begin();
        while (it != g_damageNumbers.end())
        {
            it->life -= dt;
            if (it->life <= 0.f)
            {
                it = g_damageNumbers.erase(it);
                continue;
            }
            it->y -= DamageNumber::RISE_SPEED * dt;
            const float alpha = (it->life / DamageNumber::MAX_LIFE);
            sf::Color col = it->text.getFillColor();
            col.a = static_cast<uint8_t>(alpha * 255);
            it->text.setFillColor(col);
            sf::Color outline = it->text.getOutlineColor();
            outline.a = col.a;
            it->text.setOutlineColor(outline);
            it->text.setPosition(it->x, it->y);
            g_window->draw(it->text);
            ++it;
        }
    }

    // Zone boundary visualization — gradient glow lines at zone edges
    {
        const float W    = (float)WINDOW_WIDTH;
        const float H    = (float)WINDOW_HEIGHT;
        const float GLOW = 14.f;
        const sf::Color C(80, 140, 255, 110);
        const sf::Color T(80, 140, 255, 0);

        for (int bx = ZONE_TILE_SIZE; bx < W_WIDTH; bx += ZONE_TILE_SIZE) {
            const float sx = (bx - g_cam_x) * TILE_WIDTH;
            if (sx < -GLOW || sx > W + GLOW) continue;
            sf::VertexArray lq(sf::Quads, 4), rq(sf::Quads, 4);
            lq[0] = sf::Vertex(sf::Vector2f(sx - GLOW, 0.f), T);
            lq[1] = sf::Vertex(sf::Vector2f(sx,        0.f), C);
            lq[2] = sf::Vertex(sf::Vector2f(sx,        H  ), C);
            lq[3] = sf::Vertex(sf::Vector2f(sx - GLOW, H  ), T);
            rq[0] = sf::Vertex(sf::Vector2f(sx,        0.f), C);
            rq[1] = sf::Vertex(sf::Vector2f(sx + GLOW, 0.f), T);
            rq[2] = sf::Vertex(sf::Vector2f(sx + GLOW, H  ), T);
            rq[3] = sf::Vertex(sf::Vector2f(sx,        H  ), C);
            g_window->draw(lq);
            g_window->draw(rq);
        }

        for (int by = ZONE_TILE_SIZE; by < W_HEIGHT; by += ZONE_TILE_SIZE) {
            const float sy = (by - g_cam_y) * TILE_WIDTH;
            if (sy < -GLOW || sy > H + GLOW) continue;
            sf::VertexArray tq(sf::Quads, 4), bq(sf::Quads, 4);
            tq[0] = sf::Vertex(sf::Vector2f(0.f, sy - GLOW), T);
            tq[1] = sf::Vertex(sf::Vector2f(W,   sy - GLOW), T);
            tq[2] = sf::Vertex(sf::Vector2f(W,   sy        ), C);
            tq[3] = sf::Vertex(sf::Vector2f(0.f, sy        ), C);
            bq[0] = sf::Vertex(sf::Vector2f(0.f, sy        ), C);
            bq[1] = sf::Vertex(sf::Vector2f(W,   sy        ), C);
            bq[2] = sf::Vertex(sf::Vector2f(W,   sy + GLOW ), T);
            bq[3] = sf::Vertex(sf::Vector2f(0.f, sy + GLOW ), T);
            g_window->draw(tq);
            g_window->draw(bq);
        }
    }

    // HUD
    sf::Text text;
    text.setFont(*g_font);
    text.setCharacterSize(18);
    text.setStyle(sf::Text::Bold);
    char buf[512];
    sprintf_s(buf, "(%d, %d)  Lv.%d  HP:%d/%d  EXP:%d",
              avatar.m_x, avatar.m_y, avatar.level, avatar.hp, avatar.maxhp, avatar.exp);
    text.setString(buf);
    text.setPosition(4.f, 4.f);
    g_window->draw(text);

    // Skill cooldown indicator
    sf::Text skillText;
    skillText.setFont(*g_font);
    skillText.setCharacterSize(18);
    skillText.setStyle(sf::Text::Bold);
    const float elapsed  = g_skillClock.getElapsedTime().asSeconds();
    const float cooldown = 5.f;
    if (elapsed >= cooldown)
    {
        skillText.setString("[A] Skill: READY");
        skillText.setFillColor(sf::Color(80, 220, 80));
    }
    else
    {
        char cdBuf[64];
        sprintf_s(cdBuf, "[A] Skill: %.1fs", cooldown - elapsed);
        skillText.setString(cdBuf);
        skillText.setFillColor(sf::Color(200, 200, 200));
    }
    skillText.setPosition(4.f, 24.f);
    g_window->draw(skillText);

    {
        const int zoneId = GetZoneRow(avatar.m_y) * ZONE_COUNT_X + GetZoneCol(avatar.m_x);
        char zoneBuf[24];
        sprintf_s(zoneBuf, "Zone %d", zoneId + 1);
        sf::Text zoneLabel;
        zoneLabel.setFont(*g_font);
        zoneLabel.setCharacterSize(14);
        zoneLabel.setFillColor(sf::Color(130, 190, 255, 200));
        zoneLabel.setString(zoneBuf);
        zoneLabel.setPosition(4.f, 44.f);
        g_window->draw(zoneLabel);
    }

    // Zone transition overlay — drawn last, covers all game elements
    if (g_zoneTransState != ZoneTransState::NONE)
    {
        const float t = g_zoneTransClock.getElapsedTime().asSeconds();

        if (g_zoneTransState == ZoneTransState::FADE_OUT)
        {
            float alpha = t / ZONE_FADE_OUT_DUR;
            if (alpha >= 1.f) {
                alpha = 1.f;
                avatar.move(g_pendingPlayerX, g_pendingPlayerY);
                g_left_x = g_pendingPlayerX - SCREEN_WIDTH  / 2;
                g_top_y  = g_pendingPlayerY - SCREEN_HEIGHT / 2;
                g_cam_x  = (float)g_left_x;
                g_cam_y  = (float)g_top_y;
                avatar.show();
                // Screen is now fully black — flush buffered new-zone objects
                for (auto& kv : g_pendingObjects)
                    players[kv.first] = std::move(kv.second);
                g_pendingObjects.clear();
                g_zoneTransState = ZoneTransState::LOADING;
                g_zoneTransClock.restart();
            }
            sf::RectangleShape fadeOutOverlay(sf::Vector2f((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
            fadeOutOverlay.setFillColor(sf::Color(0, 0, 0, (uint8_t)(alpha * 255.f)));
            g_window->draw(fadeOutOverlay);
        }

        if (g_zoneTransState == ZoneTransState::LOADING)
        {
            const float lt = g_zoneTransClock.getElapsedTime().asSeconds();
            float progress = lt / ZONE_LOADING_DUR;
            if (progress >= 1.f) {
                progress         = 1.f;
                g_zoneTransState = ZoneTransState::FADE_IN;
                g_zoneTransClock.restart();
            }

            sf::RectangleShape loadingBg(sf::Vector2f((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
            loadingBg.setFillColor(sf::Color::Black);
            g_window->draw(loadingBg);

            char loadBuf[64] = {};
            const int zoneIdx = g_currentZoneRow * ZONE_COUNT_X + g_currentZoneCol + 1;
            sprintf_s(loadBuf, "Zone %d  Loading...", zoneIdx);

            sf::Text loadText;
            loadText.setFont(*g_font);
            loadText.setCharacterSize(28);
            loadText.setStyle(sf::Text::Bold);
            loadText.setFillColor(sf::Color(220, 220, 220));
            loadText.setOutlineColor(sf::Color::Black);
            loadText.setOutlineThickness(2.f);
            loadText.setString(loadBuf);
            const sf::FloatRect lb = loadText.getLocalBounds();
            loadText.setOrigin(lb.left + lb.width / 2.f, lb.top + lb.height / 2.f);
            loadText.setPosition(WINDOW_WIDTH / 2.f, WINDOW_HEIGHT / 2.f - 20.f);
            g_window->draw(loadText);

            const float BAR_W = 300.f, BAR_H = 8.f;
            sf::RectangleShape barBg(sf::Vector2f(BAR_W, BAR_H));
            barBg.setFillColor(sf::Color(60, 60, 60));
            barBg.setOrigin(BAR_W / 2.f, BAR_H / 2.f);
            barBg.setPosition(WINDOW_WIDTH / 2.f, WINDOW_HEIGHT / 2.f + 20.f);
            g_window->draw(barBg);

            sf::RectangleShape barFill(sf::Vector2f(BAR_W * progress, BAR_H));
            barFill.setFillColor(sf::Color(100, 180, 255));
            barFill.setOrigin(0.f, BAR_H / 2.f);
            barFill.setPosition(WINDOW_WIDTH / 2.f - BAR_W / 2.f, WINDOW_HEIGHT / 2.f + 20.f);
            g_window->draw(barFill);
        }

        if (g_zoneTransState == ZoneTransState::FADE_IN)
        {
            const float it    = g_zoneTransClock.getElapsedTime().asSeconds();
            const float alpha = 1.f - it / ZONE_FADE_IN_DUR;
            if (alpha <= 0.f) {
                g_zoneTransState = ZoneTransState::NONE;
            } else {
                sf::RectangleShape fadeInOverlay(sf::Vector2f((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT));
                fadeInOverlay.setFillColor(sf::Color(0, 0, 0, (uint8_t)(alpha * 255.f)));
                g_window->draw(fadeInOverlay);
            }
        }
    }
    // ─────────────────────────────────────────────────────────────────────────

    // ── EXIT button (top-right corner) ──────────────────────────────────────
    {
        const float EBW = 58.f, EBH = 22.f;
        const float EBX = (float)WINDOW_WIDTH - EBW - 5.f;
        const float EBY = 5.f;
        sf::RectangleShape eb(sf::Vector2f(EBW, EBH));
        eb.setFillColor(sf::Color(90, 20, 20, 215));
        eb.setOutlineColor(sf::Color(200, 60, 60, 185));
        eb.setOutlineThickness(1.f);
        eb.setPosition(EBX, EBY);
        g_window->draw(eb);
        sf::Text et;
        et.setFont(*g_font); et.setCharacterSize(13);
        et.setStyle(sf::Text::Bold);
        et.setFillColor(sf::Color(230, 165, 165));
        et.setString("EXIT");
        auto ebl = et.getLocalBounds();
        et.setOrigin(ebl.left + ebl.width/2.f, ebl.top + ebl.height/2.f);
        et.setPosition(EBX + EBW/2.f, EBY + EBH/2.f);
        g_window->draw(et);
    }

    // Chat panel — always visible, bottom-right
    {
        const float PANEL_W   = 380.f;
        const float PANEL_X   = (float)WINDOW_WIDTH  - 400.f;
        const float PANEL_BTM = (float)WINDOW_HEIGHT - 10.f;
        const float LINE_H    = 19.f;
        const float INPUT_H   = 28.f;
        const int   MAX_SHOW  = 5;

        const int   showLines = (int)min((int)g_chatHistory.size(), MAX_SHOW);
        const float histH     = showLines > 0 ? showLines * LINE_H + 4.f : 0.f;
        const float inputTop  = PANEL_BTM - INPUT_H;
        const float histTop   = inputTop  - histH;

        // History background + lines
        if (showLines > 0)
        {
            sf::RectangleShape histBg(sf::Vector2f(PANEL_W, histH));
            histBg.setFillColor(sf::Color(0, 0, 0, 150));
            histBg.setPosition(PANEL_X, histTop);
            g_window->draw(histBg);

            sf::Text line;
            line.setFont(*g_font);
            line.setCharacterSize(13);

            const int start = (int)g_chatHistory.size() - showLines;
            for (int i = 0; i < showLines; ++i)
            {
                const string& entry = g_chatHistory[start + i];
                // "Me: " = yellow, ">" = teleport/system = blue, others = white
                if (entry.size() >= 4 && entry.substr(0, 4) == "Me: ")
                    line.setFillColor(sf::Color(255, 230, 100));
                else if (!entry.empty() && entry[0] == '>')
                    line.setFillColor(sf::Color(130, 200, 255));
                else
                    line.setFillColor(sf::Color(220, 220, 220));
                line.setString(entry);
                line.setPosition(PANEL_X + 6.f, histTop + 2.f + i * LINE_H);
                g_window->draw(line);
            }
        }

        // Input box — always shown
        sf::RectangleShape inputBg(sf::Vector2f(PANEL_W, INPUT_H));
        inputBg.setFillColor(g_chatMode
            ? sf::Color(10, 10, 40, 230)
            : sf::Color(10, 10, 20, 140));
        inputBg.setPosition(PANEL_X, inputTop);
        g_window->draw(inputBg);

        sf::RectangleShape inputBorder(sf::Vector2f(PANEL_W, INPUT_H));
        inputBorder.setFillColor(sf::Color::Transparent);
        inputBorder.setOutlineColor(g_chatMode
            ? sf::Color(100, 160, 255, 240)
            : sf::Color(60, 90, 150, 120));
        inputBorder.setOutlineThickness(1.f);
        inputBorder.setPosition(PANEL_X, inputTop);
        g_window->draw(inputBorder);

        sf::Text inputTxt;
        inputTxt.setFont(*g_font);
        inputTxt.setCharacterSize(13);

        if (g_chatMode)
        {
            static float cursorTimer = 0.f;
            cursorTimer += dt;
            const bool showCursor = (int)(cursorTimer * 2.f) % 2 == 0;
            inputTxt.setFillColor(sf::Color::White);
            inputTxt.setString(g_chatInput + (showCursor ? "|" : " "));
        }
        else
        {
            inputTxt.setFillColor(sf::Color(140, 140, 140, 180));
            inputTxt.setString("Enter 키로 채팅 입력...");
        }
        inputTxt.setPosition(PANEL_X + 6.f, inputTop + 6.f);
        g_window->draw(inputTxt);
    }
}

void send_packet(void* packet)
{
    unsigned char* p = reinterpret_cast<unsigned char*>(packet);
    size_t sent = 0;
    s_socket.send(packet, p[0], sent);
}

static void PushHistory(const string& msg)
{
    g_chatHistory.push_back(msg);
    if ((int)g_chatHistory.size() > 20)
        g_chatHistory.erase(g_chatHistory.begin());
}

void ProcessChatCommand(const string& input)
{
    if (input.empty()) return;
    PushHistory("> " + input);

    int tx = 0, ty = 0;
    const bool parsed =
        sscanf_s(input.c_str(), "/teleport x=%d, y=%d", &tx, &ty) == 2 ||
        sscanf_s(input.c_str(), "/teleport x=%d y=%d",  &tx, &ty) == 2 ||
        sscanf_s(input.c_str(), "/teleport %d %d",       &tx, &ty) == 2 ||
        sscanf_s(input.c_str(), "/tp %d %d",             &tx, &ty) == 2;

    if (!parsed) { PushHistory("  Unknown command"); return; }

    char fb[48];
    sprintf_s(fb, "  Teleporting to (%d, %d)", tx, ty);
    PushHistory(fb);

    USER_TELEPORT_REQ_PACKET tp;
    tp.size = sizeof(tp);
    tp.type = static_cast<char>(PacketType::USER_TELEPORT_REQ);
    tp.x    = static_cast<short>(tx);
    tp.y    = static_cast<short>(ty);
    send_packet(&tp);
}

int main()
{
    (void)0;
    sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
    s_socket.setBlocking(false);

    if (status != sf::Socket::Done) {
        cout << "Cannot connect to server.\n";
        exit(-1);
    }

    client_initialize();  // loads font + textures

    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
    g_window = &window;

    // Layout constants shared between draw and event handler
    const float LCX  = WINDOW_WIDTH  / 2.f;
    const float LCY  = WINDOW_HEIGHT / 2.f;
    const float LFW  = 260.f, LFH = 34.f;
    const float LFX  = LCX - LFW / 2.f;
    const float LID_Y = LCY - 48.f;
    const float LPW_Y = LCY + 14.f;
    const float LBW  = 180.f, LBH = 40.f;
    const float LBX  = LCX - LBW / 2.f;
    const float LBY  = LCY + 78.f;

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();

            if (g_clientState == ClientState::LOGIN)
            {
                // ── Login screen events ──────────────────────────────────────
                if (event.type == sf::Event::MouseButtonPressed &&
                    event.mouseButton.button == sf::Mouse::Left)
                {
                    const float mx = (float)event.mouseButton.x;
                    const float my = (float)event.mouseButton.y;
                    if (mx >= LFX && mx <= LFX+LFW && my >= LID_Y && my <= LID_Y+LFH)
                        g_loginField = 0;
                    else if (mx >= LFX && mx <= LFX+LFW && my >= LPW_Y && my <= LPW_Y+LFH)
                        g_loginField = 1;
                    else if (mx >= LBX && mx <= LBX+LBW && my >= LBY && my <= LBY+LBH)
                        DoLogin();
                }
                if (event.type == sf::Event::TextEntered)
                {
                    const uint32_t ch = event.text.unicode;
                    string& cur = (g_loginField == 0) ? g_loginId : g_loginPass;
                    const int lim = (g_loginField == 0) ? NAME_SIZE - 1 : PASSWORD_SIZE - 1;
                    if (ch == 8) { if (!cur.empty()) cur.pop_back(); }
                    else if (ch >= 32 && ch < 127 && (int)cur.size() < lim)
                        cur += static_cast<char>(ch);
                }
                if (event.type == sf::Event::KeyPressed)
                {
                    switch (event.key.code)
                    {
                    case sf::Keyboard::Tab:
                        g_loginField = 1 - g_loginField;
                        break;
                    case sf::Keyboard::Return:
                        if (g_loginField == 0) g_loginField = 1;
                        else DoLogin();
                        break;
                    case sf::Keyboard::Escape:
                        window.close();
                        break;
                    default: break;
                    }
                }
            }
            else  // ClientState::INGAME
            {
                if (g_chatMode)
                {
                    // ── 채팅 입력 모드 ───────────────────────────────────────
                    if (event.type == sf::Event::TextEntered)
                    {
                        const uint32_t ch = event.text.unicode;
                        if (ch == 8) {
                            if (!g_chatInput.empty()) g_chatInput.pop_back();
                        } else if (ch >= 32 && ch < 127) {
                            if ((int)g_chatInput.size() < CHAT_SIZE - 1)
                                g_chatInput += static_cast<char>(ch);
                        }
                    }
                    if (event.type == sf::Event::KeyPressed)
                    {
                        switch (event.key.code)
                        {
                        case sf::Keyboard::Return:
                        {
                            if (!g_chatInput.empty())
                            {
                                const bool isCmd =
                                    g_chatInput.rfind("/teleport", 0) == 0 ||
                                    g_chatInput.rfind("/tp", 0) == 0;
                                if (isCmd)
                                    ProcessChatCommand(g_chatInput);
                                else
                                {
                                    CS_CHAT_PACKET cp{};
                                    cp.size = sizeof(cp);
                                    cp.type = static_cast<char>(PacketType::CS_CHAT);
                                    ::strncpy_s(cp.mess, g_chatInput.c_str(), CHAT_SIZE - 1);
                                    send_packet(&cp);
                                }
                                g_chatInput.clear();
                            }
                            g_chatMode = false;
                            break;
                        }
                        case sf::Keyboard::Escape:
                            g_chatInput.clear();
                            g_chatMode = false;
                            break;
                        case sf::Keyboard::Up:
                        case sf::Keyboard::Down:
                        case sf::Keyboard::Left:
                        case sf::Keyboard::Right:
                            g_chatInput.clear();
                            g_chatMode = false;
                            break;
                        default: break;
                        }
                    }
                }
                else
                {
                    // ── 일반 게임 입력 모드 ─────────────────────────────────
                    if (event.type == sf::Event::MouseButtonPressed &&
                        event.mouseButton.button == sf::Mouse::Left)
                    {
                        // EXIT button hit test
                        const float EBW = 58.f, EBH = 22.f;
                        const float EBX = (float)WINDOW_WIDTH - EBW - 5.f, EBY = 5.f;
                        const float mx = (float)event.mouseButton.x;
                        const float my = (float)event.mouseButton.y;
                        if (mx >= EBX && mx <= EBX+EBW && my >= EBY && my <= EBY+EBH)
                            window.close();
                    }
                    if (event.type == sf::Event::KeyPressed)
                    {
                        switch (event.key.code)
                        {
                        case sf::Keyboard::Return:
                            g_chatMode = true;
                            break;
                        case sf::Keyboard::Escape:
                            window.close();
                            break;
                        case sf::Keyboard::LControl:
                        {
                            if (g_attackClock.getElapsedTime().asMilliseconds() >= 500)
                            {
                                g_attackClock.restart();
                                avatar.SetAttacking();
                                USER_ATTACK_REQ_PACKET ap;
                                ap.size        = sizeof(ap);
                                ap.type        = static_cast<char>(PacketType::USER_ATTACK_REQ);
                                ap.attack_time = static_cast<unsigned>(
                                    chrono::duration_cast<chrono::milliseconds>(
                                        chrono::steady_clock::now().time_since_epoch()).count());
                                ap.facing      = avatar.IsFacingLeft() ? 1u : 0u;
                                send_packet(&ap);
                            }
                            break;
                        }
                        case sf::Keyboard::A:
                        {
                            if (g_skillClock.getElapsedTime().asSeconds() >= 5.f)
                            {
                                g_skillClock.restart();
                                avatar.SetSkillAttacking();
                                USER_SKILL_REQ_PACKET sp;
                                sp.size = sizeof(sp);
                                sp.type = static_cast<char>(PacketType::USER_SKILL_REQ);
                                send_packet(&sp);
                            }
                            break;
                        }
                        default: break;
                        }
                    }
                }
            }
        }

        window.clear();
        if (g_clientState == ClientState::LOGIN)
        {
            const float dt = g_clock.restart().asSeconds();
            DrawLoginScreen(dt);
        }
        else
        {
            client_main();
        }
        window.display();
    }

    client_finish();
    return 0;
}
