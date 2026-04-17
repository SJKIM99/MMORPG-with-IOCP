#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <Windows.h>
#include <chrono>
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
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH    = 65;
constexpr auto WINDOW_WIDTH  = SCREEN_WIDTH  * TILE_WIDTH;
constexpr auto WINDOW_HEIGHT = SCREEN_HEIGHT * TILE_WIDTH;

int g_left_x;
int g_top_y;
ObjID g_myid;
int chat = -1;
sf::RenderWindow* g_window;
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
    int m_x = 0, m_y = 0;
    int level = 0, hp = 0, maxhp = 0, exp = 0;
    char name[20] = {};

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
    }
    // ─────────────────────────────────────────────────────────────────────────

    void show() { m_showing = true; }
    void hide() { m_showing = false; }

    // Scale a static (non-animated) sprite. Has no effect on animated sprites
    // (those use displaySize / frameW ratio set in SetAnimTextures).
    void SetScale(float s) { if (m_tex_idle == nullptr) m_sprite.setScale(s, s); }

    void a_move(int x, int y) { m_sprite.setPosition((float)x, (float)y); }
    void a_draw()              { g_window->draw(m_sprite); }

    void move(int x, int y) { m_x = x; m_y = y; }

    void draw()
    {
        if (!m_showing) return;

        float rx = (m_x - g_left_x) * 65.0f + 1;
        float ry = (m_y - g_top_y)  * 65.0f + 1;

        if (m_tex_idle != nullptr) {
            // Animated: center on tile
            float cx = rx + TILE_WIDTH / 2.f;
            float cy = ry + TILE_WIDTH / 2.f;
            m_sprite.setPosition(cx, cy);
            g_window->draw(m_sprite);

            auto size = m_name.getGlobalBounds();
            float labelY = ry - 14.f;
            if (m_mess_end_time < chrono::system_clock::now()) {
                m_name.setPosition(cx - size.width / 2.f, labelY);
                g_window->draw(m_name);
            } else {
                m_chat.setPosition(cx - size.width / 2.f, labelY);
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
                m_chat.setPosition(rx + 32 - size.width / 2.f, ry - 10.f);
                g_window->draw(m_chat);
            }
        }
    }

    void set_name(const char str[])
    {
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
        m_chat.setString(str);
        m_chat.setFillColor(sf::Color(255, 255, 255));
        m_chat.setStyle(sf::Text::Bold);
        m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
    }
};

OBJECT avatar;
OBJECT obstacle;

unordered_map<ObjID, OBJECT> players;

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

    // Convert world tile to screen pixel position
    dn.x = static_cast<float>((worldX - g_left_x) * TILE_WIDTH + TILE_WIDTH / 2);
    dn.y = static_cast<float>((worldY - g_top_y)  * TILE_WIDTH - 10);
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

// Forward declaration for send_packet used before its definition
void send_packet(void* packet);

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
        g_left_x = packet->x - SCREEN_WIDTH / 2;
        g_top_y  = packet->y - SCREEN_HEIGHT / 2;
        avatar.maxhp = packet->maxhp;
        avatar.hp    = packet->hp;
        avatar.level = packet->level;
        avatar.exp   = packet->exp;
        avatar.show();
    }
    break;

    case static_cast<char>(PacketType::SUBJECT_ADD_NFY):
    {
        SUBJECT_ADD_NFY_PACKET* my_packet = reinterpret_cast<SUBJECT_ADD_NFY_PACKET*>(ptr);
        ObjID id = my_packet->id;

        if (id == g_myid) {
            avatar.move(my_packet->x, my_packet->y);
            g_left_x = my_packet->x - SCREEN_WIDTH / 2;
            g_top_y  = my_packet->y - SCREEN_HEIGHT / 2;
            avatar.show();
        }
        else if (static_cast<EnumCategory>(id.GetCategory()) == EnumCategory::eUser) {
            players[id] = OBJECT{};
            players[id].SetAnimTextures(
                soldier_idle_tex, SOLDIER_IDLE_FRAMES,
                soldier_walk_tex, SOLDIER_WALK_FRAMES,
                SOLDIER_FRAME_W,  SOLDIER_FRAME_H,
                256);
            players[id].SetAttackHurtTextures(
                soldier_atk_tex,  SOLDIER_ATK_FRAMES,
                soldier_hurt_tex, SOLDIER_HURT_FRAMES,
                soldier_atk2_tex,
                soldier_atk3_tex, SOLDIER_SKILL_FRAMES);
            players[id].id = id;
            players[id].move(my_packet->x, my_packet->y);
            players[id].set_name(my_packet->name);
            players[id].show();
        }
        else {
            if (my_packet->monster_type == MONSTER_TYPE::PASSIVE) {
                players[id] = OBJECT{};
                players[id].SetOrcAnimTextures(
                    slime_idle_tex,  SLIME_IDLE_FRAMES,
                    slime_walk_tex,  SLIME_WALK_FRAMES,
                    slime_atk_tex,   SLIME_ATK_FRAMES,
                    slime_hurt_tex,  SLIME_HURT_FRAMES,
                    slime_death_tex, SLIME_DEATH_FRAMES,
                    SLIME_FRAME_W, SLIME_FRAME_H, SLIME_DISPLAY_SIZE);
            } else {
                // Aggro -> Orc animated sprite
                players[id] = OBJECT{};
                players[id].SetOrcAnimTextures(
                    orc_idle_tex,  ORC_IDLE_FRAMES,
                    orc_walk_tex,  ORC_WALK_FRAMES,
                    orc_atk_tex,   ORC_ATK_FRAMES,
                    orc_hurt_tex,  ORC_HURT_FRAMES,
                    orc_death_tex, ORC_DEATH_FRAMES,
                    ORC_FRAME_W, ORC_FRAME_H, ORC_DISPLAY_SIZE);
            }
            players[id].id = id;
            players[id].move(my_packet->x, my_packet->y);
            players[id].set_name(my_packet->name);
            players[id].show();
        }
        break;
    }

    case static_cast<char>(PacketType::SUBJECT_MOVE_NFY):
    {
        SUBJECT_MOVE_NFY_PACKET* my_packet = reinterpret_cast<SUBJECT_MOVE_NFY_PACKET*>(ptr);
        ObjID other_id = my_packet->id;
        if (other_id == g_myid) {
            avatar.move(my_packet->x, my_packet->y);
            g_left_x = my_packet->x - SCREEN_WIDTH / 2;
            g_top_y  = my_packet->y - SCREEN_HEIGHT / 2;
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
            g_left_x  = packet->x - SCREEN_WIDTH / 2;
            g_top_y   = packet->y - SCREEN_HEIGHT / 2;
            avatar.move(packet->x, packet->y);
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
        avatar.hp = packet->hp;
        avatar.SetHurt();
        if (players.count(packet->attacker_id))
        {
            auto& npc = players[packet->attacker_id];
            // Face the player before playing the attack animation
            const int dx = avatar.m_x - npc.m_x;
            if (dx != 0) npc.SetFacing(dx < 0);
            npc.SetAttacking();
        }
        break;
    }

    case static_cast<char>(PacketType::USER_HEAL_INF):
    {
        USER_HEAL_INF_PACKET* packet = reinterpret_cast<USER_HEAL_INF_PACKET*>(ptr);
        avatar.hp = packet->hp;
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
            in_packet_size = ptr[0];

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

    if (moving && g_moveClock.getElapsedTime().asMilliseconds() >= 1000)
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
    // ──────────────────────────────────────────────────────────────────────

    // Network receive
    char net_buf[BUF_SIZE];
    size_t received;
    auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
    if (recv_result == sf::Socket::Error)       { cout << "Recv error!\n"; exit(-1); }
    if (recv_result == sf::Socket::Disconnected){ cout << "Disconnected\n"; exit(-1); }
    if (recv_result != sf::Socket::NotReady)
        if (received > 0) process_data(net_buf, received);

    // Draw tiles
    for (int i = 0; i < SCREEN_WIDTH; ++i)
        for (int j = 0; j < SCREEN_HEIGHT; ++j)
        {
            int tile_x = i + g_left_x;
            int tile_y = j + g_top_y;
            if ((tile_x < 0) || (tile_y < 0)) continue;

            if (0 == (tile_x / 3 + tile_y / 3) % 3) {
                tile1.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                tile1.a_draw();
            } else if (1 == (tile_x / 3 + tile_y / 3) % 3) {
                tile2.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                tile2.a_draw();
            } else {
                if (0 == (tile_x / 2 + tile_y / 2) % 3) {
                    obstacle.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                    obstacle.a_draw();
                } else if (1 == (tile_x / 2 + tile_y / 2) % 3) {
                    tile2.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                    tile2.a_draw();
                } else {
                    tile1.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
                    tile1.a_draw();
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
}

void send_packet(void* packet)
{
    unsigned char* p = reinterpret_cast<unsigned char*>(packet);
    size_t sent = 0;
    s_socket.send(packet, p[0], sent);
}

int main()
{
    (void)0; // locale setup removed (no Korean wide strings)
    sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
    s_socket.setBlocking(false);

    if (status != sf::Socket::Done) {
        cout << "Cannot connect to server.\n";
        exit(-1);
    }

    client_initialize();

    char id[20];
    cout << "ID :";
    cin >> id;

    USER_LOGIN_REQ_PACKET p;
    p.size = sizeof(p);
    p.type = static_cast<char>(PacketType::USER_LOGIN_REQ);
    strcpy_s(p.name, id);
    send_packet(&p);
    avatar.set_name(p.name);

    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
    g_window = &window;

    while (window.isOpen())
    {
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (chat != 1) {
                if (event.type == sf::Event::Closed)
                    window.close();

                if (event.type == sf::Event::KeyPressed) {
                    int attack = -1;

                    switch (event.key.code) {
                    case sf::Keyboard::LControl: attack = 1; break;
                    case sf::Keyboard::Enter:    chat = 1;   break;
                    case sf::Keyboard::Escape:   window.close(); break;
                    default: break;
                    }

                    if (attack == 1 && g_attackClock.getElapsedTime().asMilliseconds() >= 1000)
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

                    if (event.key.code == sf::Keyboard::A
                        && g_skillClock.getElapsedTime().asSeconds() >= 5.f)
                    {
                        g_skillClock.restart();
                        avatar.SetSkillAttacking();
                        USER_SKILL_REQ_PACKET sp;
                        sp.size = sizeof(sp);
                        sp.type = static_cast<char>(PacketType::USER_SKILL_REQ);
                        send_packet(&sp);
                    }
                }
            }
        }

        window.clear();
        client_main();
        window.display();
    }

    client_finish();
    return 0;
}
