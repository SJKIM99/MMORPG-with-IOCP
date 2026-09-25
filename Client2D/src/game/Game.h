#pragma once

#include "raylib.h"
#include "net/NetClient.h"
#include "game/Entity.h"
#include "game/Path.h"
#include "game/Motion.h"
#include "render/GameCamera.h"
#include "render/Terrain.h"
#include "render/Lighting.h"
#include "render/Actors.h"
#include "ui/Ui.h"
#include "Data/QuestData.h"

#include <array>
#include <cstdio>
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
//   GameItems.cpp : 장비창 · 툴팁 · 희귀도 루팅 연출(아이템 성장)
//   GameNpc.cpp   : 서버가 배회시키는 마을 NPC
//   GameQuest.cpp : 퀘스트 · 메인 이야기(추적 · 일지 · 대화 · 나침반 · 장 제목 · 엔딩)
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
		std::string traceMove;          // 테스트: 내 이동(논리/표시 위치)을 프레임마다 CSV로 기록
		int         lagMs = 0;          // 테스트: 인공 지연(한쪽 방향, ms)
		int         jitterMs = 0;
		int         classId = -1;       // 새 캐릭터 직업(-1이면 로그인 화면에서 고른다)
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
	struct QuickSlot { int kind = 0; uint16_t itemId = 0; };   // kind 0 비었음, 1 아이템
	static constexpr int kQuickSlots = 4;                      // Q E R F(= F5~F8)
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
	// 아이템 툴팁 모드(무엇을 덧붙일지: 비교 · 착용 안내 · 판매가)
	enum class TipMode { Inventory, Equipped, Shop, Sell };


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
	void HandleMoveAck(const USER_MOVE_EX_ACK_PACKET& p);
	void HandleServerConfig(const SERVER_CONFIG_INF_PACKET& p);
	// ── 아이템 성장(GameItems.cpp) ──
	void HandleItemDetail(const ITEM_DETAIL_INF_PACKET& p);
	void HandlePlayerLook(const PLAYER_LOOK_NFY_PACKET& p);
	void AnnounceLoot(int slot, int gained);
	void TickLoot();
	[[nodiscard]] actors::PlayerLook LookOf(const Entity& e) const;
	[[nodiscard]] Rarity SlotRarity(const InvSlot& s) const;
	// 지금 장비에서 invSlot 장비를 입었을 때(같은 부위는 벗는다)의 전투 능력치. invSlot < 0이면 지금 그대로.
	// unequip이 true면 그 슬롯 장비를 벗었을 때.
	[[nodiscard]] DerivedStats StatsIf(int invSlot, bool unequip = false) const;
	[[nodiscard]] int UpgradeScore(int invSlot) const;   // > 0 이면 갈아입을 만하다(▲)
	[[nodiscard]] GearExtras MyGearExtras() const;       // 지금 입은 장비의 특수 효과·기술 강화·세트(서버와 같은 계산)
	[[nodiscard]] bool CanWear(const ItemTableRow& row, std::string* why = nullptr) const;
	void BuildItemTip(std::vector<ui::TooltipLine>& out, const InvSlot& s, TipMode mode, int invSlot) const;
	void DrawItemTooltip(const InvSlot& s, TipMode mode, int invSlot = -1);
	void DrawEquipPanel(Rectangle area);
	void TryEquipFromUi(int invSlot, EquipSlot target);
	// ── 전투 V2(GameCombat.cpp) ──
	void HandleStats(const USER_STATS_INF_PACKET& p);
	void HandleResource(const USER_RESOURCE_INF_PACKET& p);
	void HandleSkillResult(const SKILL_RESULT_ACK_PACKET& p);
	void HandleSkillCast(const SKILL_CAST_NFY_PACKET& p);
	void HandleStatus(const STATUS_NFY_PACKET& p);
	void HandleTelegraph(const MONSTER_TELEGRAPH_NFY_PACKET& p);
	void QueueDamage(const SUBJECT_DAMAGE_NFY_PACKET& p);   // 투사체/연타 지연 표시 → HandleDamage
	void TickCombat(float dt);
	[[nodiscard]] bool HasDelayedHitFor(const ObjID& victim) const;
	void OnTelegraphImpact(Entity& m);                  // 몬스터 강공격 예고가 끝난 순간(연출)
	void HandleChat(const ObjID& sender, const std::string& senderName, const std::string& text, ChatChannel channel);

	// ── 행동 ──
	void SetGoal(const Goal& g);
	void ClearGoal();
	void TickMovement();
	void TickGoal();
	bool StepToward(int gx, int gy, int stopWithin);
	bool SendStep(int dir);
	int  PickMoveDir(int dx, int dy) const;   // 막히면 가장 가까운 열린 방향(미끄러짐)
	void SyncMover();
	void TryAttackTarget(Entity& target);
	void UseSlot(int slot);
	void UseQuickSlot(int index);
	void CycleTarget();
	void Interact();                                    // F: 가까운 NPC와 대화, 없으면 줍기
	[[nodiscard]] Vector2 NpcTile(uint16_t npcId) const;  // NPC의 지금 칸(서버가 알려 준 배회 위치)
	// ── 마을 NPC(GameNpc.cpp) — 서버가 배회시키는 위치를 받아 걷게 그린다 ──
	struct NpcView
	{
		const WorldData::NpcRow* row = nullptr;
		int      tx = 0, ty = 0;             // 서버 칸
		Vector2  pos{}, logPos{}, from{}, to{};
		float    moveT = 1.f, moveDur = 0.5f;
		float    walkPhase = 0.f, moving = 0.f, speed = 0.f;
		bool     faceRight = true, faceBack = false;
		uint8_t  state = 0;                  // NpcActState
		ObjID    talkTo{};
		std::string bark;
		float    barkT = 0.f, barkLife = 1.f;
		float    heartT = -1.f;              // 쓰다듬었을 때 하트
		float    hopT = -1.f;                // 반가워서 폴짝
		float    noteAt = 0.f;               // 음유시인 음표
		bool     known = false;              // 서버 위치를 받았나
	};
	void InitNpcs();
	void UpdateNpcs(float dt);
	[[nodiscard]] NpcView* NpcViewOf(uint16_t npcId);
	void HandleNpcList(const NPC_LIST_NFY_PACKET& p);
	void HandleNpcState(const NPC_STATE_NFY_PACKET& p);
	void HandleNpcBark(const NPC_BARK_NFY_PACKET& p);
	void HandleNpcTalkAck(const NPC_TALK_ACK_PACKET& p);
	void HandleNpcServiceAck(const NPC_SERVICE_ACK_PACKET& p);
	void RequestNpcService(uint16_t npcId, WorldData::NpcService service);
	// ── 퀘스트 · 메인 이야기(GameQuest.cpp) — 판정은 서버, 표시는 Data/QuestData.h ──
	struct QuestView
	{
		uint16_t id = 0;
		std::array<uint16_t, 3> progress{};
		std::array<double, 3> bumpAt{ -10.0, -10.0, -10.0 };   // 진행도가 오른 순간(튀어 오름)
		double acceptedAt = -10.0;
		double readyAt = -10.0;                                // 목표를 다 이룬 순간
	};
	enum class QuestAvail { No, Yes, LowLevel, Cooldown };
	enum class QuestMark { None, Low, Active, Talk, Available, Ready };   // 우선순위 순
	struct QuestFly { Vector2 from; double at; uint16_t quest; int obj; };
	struct QuestDoneFx { uint16_t quest; double at; uint32_t exp, adena; uint16_t item, count; uint8_t rarity; };
	struct Discovery { uint16_t poi; double at; bool heart; };
	void HandleQuestList(const QUEST_LIST_INF_PACKET& p);
	void HandleQuestUpdate(const QUEST_UPDATE_INF_PACKET& p);
	void HandleQuestResult(const QUEST_RESULT_ACK_PACKET& p);
	[[nodiscard]] const QuestView* ActiveQuest(uint16_t id) const;
	[[nodiscard]] bool QuestDone(uint16_t id) const;
	[[nodiscard]] bool QuestReady(const QuestView& q) const;
	[[nodiscard]] QuestAvail CanTakeQuest(const QuestData::QuestRow& q) const;
	[[nodiscard]] QuestMark NpcQuestMark(uint16_t npcId) const;
	[[nodiscard]] std::string ObjectiveLabel(const QuestData::Objective& o) const;
	[[nodiscard]] const QuestView* FocusQuest() const;             // 나침반 · 추적 표시 대상
	// 지금 가야 할 곳(칸)과 이름(나침반 · 미니맵 · 지도). viaTeleporter: 텔레포터를 거쳐 가라는 안내
	struct QuestDest { Vector2 tile{}; std::string label; bool viaTeleporter = false; bool npc = false; bool poi = false; uint16_t quest = 0; };
	bool QuestDestination(QuestDest& out) const;
	[[nodiscard]] QuestAvail CanTakeQuestId(uint16_t id) const;
	uint16_t AutoQuestForNpc(uint16_t npcId) const;                 // 말을 걸면 바로 펼칠 퀘스트(보고 · 메인 이야기)
	void DrawNpcPortrait(Rectangle pr, const WorldData::NpcRow& npc);
	[[nodiscard]] bool IsQuestTarget(const Entity& e) const;       // 몬스터 머리 위 퀘스트 표식
	[[nodiscard]] bool QuestWantsRegion(int zoneId) const;         // 진행 중인 목표가 그 지역에 있나(텔레포트 목록 표시)
	void SendQuest(QuestAction action, uint16_t questId);
	void OpenQuestInDialog(uint16_t questId);
	void UpdateQuests(float dt);
	void DrawQuestMark(Vector2 head, QuestMark mark, float scale);
	void DrawQuestTracker();
	void DrawQuestLog();
	void DrawQuestDialog(const WorldData::NpcRow& npc);
	float DrawQuestRewards(float x, float y, float w, const QuestData::QuestRow& q, bool stacked = false);   // 차지한 높이
	float DrawQuestObjectives(float x, float y, float w, const QuestData::QuestRow& q, const QuestView* v, float size);
	void DrawQuestWorld(bool groundPass);                          // 월드 패스: 목적지 바닥 고리(ground) · 빛기둥
	void DrawQuestCompass(const Camera2D& cam);                    // 화면 밖 목적지 화살표
	void DrawQuestOverlay(const Camera2D& cam);                    // 날아오는 퀘스트 물건
	void DrawQuestScreenFx();                                      // 장 제목 · 엔딩(화면 전체)
	float DrawQuestStackCards(float y);                            // 알림 띠 맨 위: 새 장소 · 퀘스트 완료 카드(차지한 높이)
	// 0 = 기본 공격, 1~5 스킬. quiet: 안내 문구 없이(자동 공격·예약 시전).
	// forced: 대상을 정해서(자동 공격의 목표·사거리 밖에서 눌러 예약한 대상).
	bool TryCastSkill(int slot, bool quiet = false, const ObjID& forced = ObjID{});
	[[nodiscard]] const SkillRow* SkillInSlot(int slot) const;
	[[nodiscard]] Entity* PickSkillTarget(const SkillRow& row);
	[[nodiscard]] int AttackRange() const;              // 자동 공격/예약 기술이 멈춰 설 거리
	[[nodiscard]] Vector2 PredictLanding(const SkillRow& row, int tx, int ty, const Entity* target) const;
	void AddCombo(int hits);
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
	void DrawSkillBar();
	void DrawSkillTooltip(const SkillRow& row);
	void DrawStatusIcons(float x, float y);
	void DrawComboCounter();
	void DrawCharacter();
	void DrawClassSelect(Rectangle area);
	void DrawSkillPreview();                            // 스킬 칸에 마우스를 올리면 사거리/범위
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
	void DrawItemTooltip(uint16_t itemId, uint8_t enchant, int count, bool equipped, bool forSell);   // 상점 등(인스턴스 없음)
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

	// ── 전투 V2 ──
	ClassId          m_classId = ClassId::Knight;
	uint8_t          m_newClassId = 0;              // 로그인 화면에서 고른 직업(새 캐릭터)
	DerivedStats     m_stats{};
	BaseStats        m_baseStats{}, m_allocStats{}, m_gearStats{};
	uint16_t         m_freePoints = 0;
	int              m_mp = 0, m_maxmp = 0;
	float            m_mpShown = 1.f;
	std::array<double, 6> m_skillReady{};           // 슬롯별 다시 쓸 수 있는 시각(로컬)
	std::array<double, 6> m_skillCdTotal{};         // 쿨다운 길이(스윕 표시)
	std::array<double, 6> m_skillFlash{};           // 쿨다운이 끝난 순간(반짝임)
	uint16_t         m_castSeq = 0;
	int              m_queuedSkill = -1;            // 사거리 밖에서 누른 스킬 — 다가가서 쓴다
	double           m_queuedAt = 0.0;
	ObjID            m_queuedTarget{};
	struct DelayedHit { double at; SUBJECT_DAMAGE_NFY_PACKET p; };
	std::vector<DelayedHit> m_delayedHits;
	int              m_comboHits = 0;
	double           m_comboLastAt = -10.0;
	std::string      m_comboName;
	double           m_comboNameAt = -10.0;
	bool             m_charOpen = false;            // 캐릭터(능력치) 창
	std::array<double, 6> m_castSentAt{};           // 슬롯별 마지막 시전 요청 시각(쿨다운 기준)
	double           m_actionLockUntil = 0.0;       // 기술 동작 중엔 걸음을 보내지 않는다(무게감)
	uint16_t         m_dashWaitSeq = 0;             // 돌진류: 서버 도착 칸을 받을 때까지 멈춤
	double           m_dashWaitUntil = 0.0;
	int              m_bufferSlot = -1;             // 쿨다운 직전에 누른 기술(선입력)
	double           m_bufferAt = 0.0;
	double           m_mpFlashAt = -10.0;
	double           m_lastHitSfxAt = 0.0;
	std::vector<SUBJECT_DIE_NFY_PACKET> m_deferredDies;   // 투사체가 닿은 뒤에 쓰러뜨린다
	std::array<double, 16> m_comboShownAt{};
	int              m_comboBest = 0;
	int              m_previewSkill = -1;
	uint16_t         m_prevFreePoints = 0;
	bool             m_statsKnown = false;
	double           m_dangerAt = -10.0;            // 내 발밑에 예고가 깔린 시각(화면 가장자리 경고)
	struct ChainHop { ObjID caster; Vector2 iso; float z; double at; };
	std::vector<ChainHop> m_chainHops;              // 번개 폭발: 직전에 맞은 곳(다음 줄기의 시작점)
	std::array<double, 6> m_readyPrev{};            // 쿨다운 끝 알림(반짝임) 판정용
	float            m_feelTrauma = 0.f, m_feelKick = 0.f, m_feelStop = 0.f;   // 이번 프레임 적중 체감(최댓값만)

	// 이동 예측(game/Motion.h). m_srv*/m_pred*는 mover 값을 매 프레임 비춰 둔 사본.
	motion::LocalMover m_mover;
	int              m_srvX = 0, m_srvY = 0;
	int              m_predX = 0, m_predY = 0;
	std::vector<path::Step> m_path;
	double           m_repathAt = 0.0;
	int              m_viewRange = 9;          // 서버 설정(SERVER_CONFIG_INF)
	uint32_t         m_baseSpeedPercent = 100; // 서버가 알려 준 내 이동속도(가속 제외)

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
	std::array<QuickSlot, kQuickSlots> m_quick{};
	bool             m_invOpen = false;
	bool             m_helpOpen = false;
	bool             m_mapOpen = false;
	bool             m_showGrid = true;
	uint16_t         m_dialogNpc = 0;
	int              m_dialogLine = -1;             // 대화창: -1 인사, 0.. 이야기
	double           m_dialogTextAt = 0.0;          // 글자가 한 자씩 나타나기 시작한 시각
	std::unordered_map<uint16_t, NpcView> m_npcView;
	double           m_npcServiceReadyAt[3]{};      // NpcService별(표시용)
	double           m_lastVoiceAt = 0.0;
	ShopMode         m_shopMode = ShopMode::None;
	uint16_t         m_buyItem = 0;
	int              m_buyCount = 1;
	int              m_sellSlot = -1;
	int              m_sellCount = 1;
	int              m_teleportSel = -1;
	EnchantFlow      m_enchant;
	Discard          m_discard;
	int              m_invHover = -1;
	// ── 아이템 성장 UI ──
	ui::TextField    m_invSearch;
	// 획득 판정: 슬롯 알림이 몰려 오는 동안(교환·정리는 여러 칸이 한꺼번에 바뀐다) 처음 상태를
	// 기억해 두었다가, 잠깐 뒤 "처음에 없던 물건"만 획득으로 알린다.
	std::array<InvSlot, MAX_INVENTORY_SLOTS> m_invBaseline{};
	double           m_invBaselineAt = -1.0;
	double           m_sortSentAt = -10.0;
	int              m_equipHover = -1;           // 장비칸(EquipSlot) 위 마우스
	Vector2          m_invGridPos{};              // 가방 첫 칸 위치(테스트 스크립트·안내용)
	std::array<Rectangle, 9> m_equipRects{};      // EquipSlot별 장비칸 위치
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

	// ── 퀘스트 ──
	std::vector<QuestView> m_quests;                   // 진행 중(받은 순서)
	std::array<uint8_t, 32> m_questDone{};             // 끝낸 퀘스트 id 비트(서버와 같은 모양)
	std::unordered_map<uint16_t, double> m_questCooldownUntil;   // 반복 퀘스트: m_now 기준 다시 받을 수 있는 시각
	bool             m_questsKnown = false;
	bool             m_questLogOpen = false;
	uint16_t         m_questLogSel = 0;
	uint16_t         m_trackedQuest = 0;              // 0 = 자동(메인 이야기 → 먼저 받은 것)
	uint16_t         m_dialogQuest = 0;               // NPC 대화창에서 보고 있는 퀘스트
	double           m_questSentAt = -10.0;
	int              m_abandonArm = 0;                // 포기 확인(두 번 누르기)
	double           m_abandonArmAt = -10.0;
	std::vector<QuestFly> m_questFlies;
	std::deque<QuestDoneFx> m_questDoneFx;
	std::deque<Discovery> m_discoveries;
	int              m_chapterCard = 0;
	double           m_chapterCardAt = -100.0;
	double           m_endingAt = -100.0;
	float            m_trackerBottom = 256.f;         // 추적 표시 아래 끝(알림 토스트가 그 아래로)
	QuestDest        m_questDest;                     // 이번 프레임 목적지(나침반 · 미니맵 · 지도 공용)
	bool             m_questDestValid = false;
	bool             m_chapterBellPlayed = false;
	float            m_bannersBottom = 84.f;          // 알림 띠 아래 끝(가운데 알림이 그 아래로)
	uint16_t         m_dialogAutoQuest = 0;           // 보고 직후 이어서 펼칠 다음 이야기(완료 연출이 지나간 뒤)
	double           m_dialogAutoAt = 0.0;
	int              m_endingStage = 0;
	float            m_questLogScroll = 0.f;

	// 테스트 자동화
	float            m_runTime = 0.f;
	bool             m_shotTaken = false;
	int              m_shotIndex = 0;
	int              m_scriptStep = 0;
	float            m_scriptT = 0.f;
	int              m_scriptKeyX = 0, m_scriptKeyY = 0;   // 스크립트가 누른 WASD(화면 방향)
	std::FILE*       m_traceFile = nullptr;
};
