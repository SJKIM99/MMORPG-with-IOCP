#pragma once

#include "raylib.h"
#include "net/NetClient.h"
#include "game/Entity.h"
#include "game/Path.h"
#include "render/GameCamera.h"
#include "render/Terrain.h"
#include "render/Lighting.h"
#include "ui/Ui.h"

#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 게임 클라이언트 본체: 로그인 화면 → 게임 화면.
//   Game.cpp      : 초기화, 프레임 루프, 입력, 이동 예측/보정, 자동 공격/줍기/대화
//   GameNet.cpp   : 서버 패킷 처리(레거시 + 확장 프로토콜 전부)
//   GameDraw.cpp  : 월드 그리기(깊이 패스), 이름표/체력바/말풍선
//   GameUi.cpp    : HUD, 인벤토리/상점/텔레포트/강화/사망 창, 로그인 화면
// ─────────────────────────────────────────────────────────────────────────────
class Game
{
public:
	struct Options
	{
		std::string host = "127.0.0.1";
		uint16_t    port = PORT_NUM;
		std::string autoName;           // 있으면 바로 접속
		std::string screenshot;         // 테스트: 지정 시간 뒤 스크린샷 저장 후 종료
		float       screenshotAfter = 8.f;
		int         screenshotCount = 1;    // 여러 장(파일명_1.png ...)
		float       screenshotEvery = 1.f;
		float       hour = -1.f;        // 테스트: 시각 고정(0~24)
		std::string script;             // 테스트: 자동 조작 시나리오 이름
		bool        mute = false;
	};

	bool Init(const Options& opts);
	void Shutdown();
	void Frame();
	[[nodiscard]] bool WantsQuit() const { return m_quit; }

private:
	enum class Scene { Login, Connecting, Game, Disconnected };
	enum class GoalKind { None, Move, Attack, Pickup, Talk };
	enum class ShopMode { None, Buy, Sell, Teleport };

	struct Goal
	{
		GoalKind kind = GoalKind::None;
		ObjID    target{};
		int      x = 0, y = 0;
		uint16_t npc = 0;
		bool     force = false;       // Ctrl+클릭(플레이어 공격)
	};
	struct ChatLine { std::string text; Color color; double time; };
	struct Banner { std::string text; Color color; double start; float life; };
	struct Toast { uint16_t itemId; std::string text; Color color; double start; };
	struct CenterMsg { std::string text; Color color; double start; };
	struct QuickSlot { int kind = 0; uint16_t itemId = 0; };   // kind 0 비었음, 1 아이템, 2 십자베기
	struct EnchantFlow
	{
		int stage = 0;              // 0 없음, 1 무기 고르기, 2 확인, 3 충전(결과 대기/연출), 4 결과
		int scrollSlot = -1, targetSlot = -1;
		uint16_t scrollId = 0, itemId = 0;
		uint8_t before = 0, after = 0;
		bool resultKnown = false;
		EnchantResult result = EnchantResult::Invalid;
		double start = 0.0;
	};
	struct Discard { bool open = false; int slot = -1; int count = 1; };

	// ── 프레임 ──
	void UpdateLogin(float dt);
	void UpdateGame(float dt);
	void DrawLogin();
	void DrawGame();
	void DrawDisconnected();
	void BeginConnect();
	void ResetWorld();

	// ── 네트워크 ──
	void OnPacket(const char* data, uint16_t size);
	void HandleLoginAck(const USER_LOGIN_ACK_PACKET& p);
	void HandleSubjectAdd(const SUBJECT_ADD_NFY_PACKET& p);
	void HandleSubjectMove(const SUBJECT_MOVE_NFY_PACKET& p);
	void HandleSubjectRemove(const SUBJECT_REMOVE_NFY_PACKET& p);
	void HandleSubjectDie(const SUBJECT_DIE_NFY_PACKET& p);
	void HandleSubjectRespawn(const SUBJECT_RESPAWN_NFY_PACKET& p);
	void HandleDamage(const SUBJECT_DAMAGE_NFY_PACKET& p);
	void HandleEffect(const EFFECT_NFY_PACKET& p);
	void HandleSlot(const ITEM_SLOT_EX_DATA& s, bool announce);
	void HandleSystemMessage(const SYSTEM_MESSAGE_INF_PACKET& p);
	void HandleInfoEx(const USER_INFO_EX_INF_PACKET& p);
	void HandleSubjectInfoEx(const SUBJECT_INFO_EX_NFY_PACKET& p);
	void HandleChat(const ObjID& sender, const std::string& senderName, const std::string& text, ChatChannel channel);

	// ── 행동 ──
	void SetGoal(const Goal& g);
	void ClearGoal();
	void TickMovement();
	void TickGoal();
	bool StepToward(int gx, int gy, int stopWithin);
	void SendStep(int dir);
	void TryAttackTarget(Entity& target);
	void UseSlot(int slot);
	void UseQuickSlot(int index);
	void CastSkill();
	void PickupHere();
	void TargetNearestMonster();
	void OpenNpc(uint16_t npcId);
	void CloseNpcWindows();
	void StartEnchant(int scrollSlot);
	void SubmitChat();

	// ── 입력 ──
	void HandleWorldInput();
	void HandleHotkeys();
	void PickUnderMouse();

	// ── 그리기 ──
	void DrawWorld(const Camera2D& cam);
	void DrawEntity(Entity& e, bool translucentPass);
	void DrawNpcs();
	void DrawWorldOverlay(const Camera2D& cam);
	void DrawHud();
	void DrawChat();
	void DrawQuickBar();
	void DrawTargetFrame();
	void DrawMinimap();
	void DrawInventory();
	void DrawNpcDialog();
	void DrawShop();
	void DrawSell();
	void DrawTeleport();
	void DrawEnchant();
	void DrawDiscard();
	void DrawDeath();
	void DrawBanners();
	void DrawToasts();
	void DrawHelp();
	void DrawWorldMap();
	void DrawItemTooltip(uint16_t itemId, uint8_t enchant, int count, bool equipped, bool forSell);
	void DrawCursor();

	// ── 도우미 ──
	Entity* Self();
	Entity* Find(const ObjID& id);
	[[nodiscard]] bool InTown() const;
	[[nodiscard]] int Region() const;
	void AddChat(const std::string& text, Color color);
	void AddCenter(const std::string& text, Color color);
	void AddBanner(const std::string& text, Color color, float life = 4.5f);
	void AddToast(uint16_t itemId, const std::string& text, Color color);
	void PlayAt(int sfxId, Vector2 tile, float volume = 1.f, float pitchVar = 0.05f);
	[[nodiscard]] Vector2 TileToIso(Vector2 tile, float z = 0.f) const;
	[[nodiscard]] Color NameColor(const Entity& e) const;
	void LoadQuickSlots();
	void SaveQuickSlots();
	void RunScript(float dt);

	// ── 상태 ──
	Options          m_opts;
	net::NetClient   m_net;
	Terrain          m_terrain;
	Lighting         m_light;
	GameCamera       m_cam;
	Camera2D         m_cam2d{};
	Scene            m_scene = Scene::Login;
	bool             m_quit = false;
	double           m_now = 0.0;
	float            m_time = 0.f;      // 애니메이션 시간(히트스톱 반영)
	int              m_screenW = 1280, m_screenH = 720;

	// 로그인
	ui::TextField    m_fHost, m_fPort, m_fName, m_fPass;
	std::string      m_loginError;
	bool             m_loginSent = false;
	double           m_connectStart = 0.0;
	std::string      m_playerName;

	// 나
	ObjID            m_selfId{};
	bool             m_hasSelf = false;
	int              m_level = 1;
	uint32_t         m_exp = 0, m_expToNext = 30;
	int              m_hp = 100, m_maxhp = 100;
	float            m_hpShown = 1.f, m_hpLag = 1.f, m_expShown = 0.f;
	int              m_attack = 10;
	int16_t          m_alignment = 0;
	uint16_t         m_pkCount = 0;
	uint8_t          m_flags = 0;
	double           m_hasteUntil = 0.0;
	bool             m_dead = false;
	double           m_deadAt = 0.0;
	bool             m_restartSent = false;

	// 이동 예측
	int              m_srvX = 0, m_srvY = 0;
	bool             m_pending = false;
	double           m_pendingAt = 0.0;
	int              m_predX = 0, m_predY = 0;
	double           m_nextMoveAt = 0.0;
	std::vector<path::Step> m_path;
	double           m_repathAt = 0.0;
	int              m_lastDir = 1;

	// 목표/대상
	Goal             m_goal;
	ObjID            m_target{};
	ObjID            m_hover{};
	uint16_t         m_hoverNpc = 0;
	int              m_hoverX = -1, m_hoverY = -1;
	double           m_nextAttackAt = 0.0;
	double           m_lastSkillAt = -100.0;
	double           m_lastPotionAt = -100.0;
	double           m_lastScrollAt = -100.0;
	double           m_localSkillFxAt = -100.0;
	double           m_pickupSentAt = -100.0;
	bool             m_mouseHeld = false;
	double           m_holdRepathAt = 0.0;

	std::unordered_map<ObjID, Entity> m_ents;
	ClientInventory  m_inv;

	// UI 상태
	std::deque<ChatLine> m_chat;
	ui::TextField    m_chatField;
	bool             m_chatOpen = false;
	std::deque<Banner>   m_banners;
	std::deque<Toast>    m_toasts;
	std::deque<CenterMsg> m_center;
	std::array<QuickSlot, 8> m_quick{};
	bool             m_invOpen = false;
	bool             m_helpOpen = false;
	bool             m_mapOpen = false;
	bool             m_showGrid = true;
	uint16_t         m_dialogNpc = 0;
	ShopMode         m_shopMode = ShopMode::None;
	uint16_t         m_buyItem = 0;
	int              m_buyCount = 1;
	int              m_sellSlot = -1;
	int              m_sellCount = 1;
	int              m_teleportSel = -1;
	EnchantFlow      m_enchant;
	Discard          m_discard;
	int              m_invHover = -1;
	Rectangle        m_invRect{};
	double           m_ambNext = 0.0;
	bool             m_ghostDraw = false;
	bool             m_silhouetteDraw = false;
	int              m_pressKind = 0;      // 1 인벤토리, 2 퀵슬롯
	int              m_pressIndex = -1;
	Vector2          m_pressPos{};
	int32_t          m_lastExpLost = 0;
	RenderTexture2D  m_minimapRt{};
	int              m_minimapX = -9999, m_minimapY = -9999;
	double           m_chatOpenedAt = -10.0;
	double           m_bossWarnAt = -100.0;
	uint32_t         m_lastShopSeq = 0;

	// 테스트 자동화
	float            m_runTime = 0.f;
	bool             m_shotTaken = false;
	int              m_shotIndex = 0;
	int              m_scriptStep = 0;
	float            m_scriptT = 0.f;
};
