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
#include "Data/SkillGrowth.h"
#include "fx/SkillFx.h"
#include "game/Chat.h"

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
		int         lagMs = 0;          // 테스트: 인공 지연(한쪽 방향, ms) — --netsim의 옛 이름
		int         jitterMs = 0;
		net::NetClient::NetSim netsim;  // 테스트: --netsim latency=150,jitter=40,spike=2
		bool        debugOverlay = false;   // --f3: 처음부터 F3 정보 표시
		int         classId = -1;       // 새 캐릭터 직업(-1이면 로그인 화면에서 고른다)
		// Phase 2-A 시험용 덮어쓰기(-1 = 설정값 그대로): --quality 0~2 · --photosafe · --otherfx 0~100
		int         quality = -1;
		int         photoSafe = -1;
		int         otherFx = -1;
	};

	bool Init(const Options& opts);
	void Shutdown();
	void Frame();
	[[nodiscard]] bool WantsQuit() const { return m_quit; }

private:
	enum class Scene { Login, Connecting, Game, Disconnected };
	// Move = 목적지(클릭) · Direction = 누른 채 커서 쪽 · Follow = 다른 모험가 따라가기
	enum class GoalKind { None, Move, Attack, Pickup, Talk, Direction, Follow };
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
	// 소모품 칸: 전투 1~4 · 체력 물약(F1) · 보조 5~8 — game/KeyBinds 동작 Battle1..Aux4 순서
	static constexpr int kItemSlots = 9;
	static constexpr int kPotionSlot = 4;
	// 기술 칸: 기본 공격(C · 우클릭) · Q W E R · A S D F · 각성기 V · 이동기 Space · 고유 기술 Z
	static constexpr int kHotSlots = 12;
	static constexpr int kHotBasic = 0, kHotAwaken = 9, kHotMobility = 10, kHotIdentity = 11;
	static constexpr int kDragSkill = 4;           // ui::DragPayload.kind — 기술(기술 창 · 기술 칸에서 끌기)
	// 커서 조준(매 프레임): 월드 칸 · 방향 · 커서에 가장 가까운 적
	struct Aim
	{
		Vector2  world{};            // 커서 아래 바닥(연속 칸 좌표)
		int      tx = 0, ty = 0;     // 그 칸
		Vector2  dir{ 1.f, 0.f };    // 나 → 커서(칸 좌표 단위 벡터)
		uint8_t  dir256 = 0;         // 화면 기준 각도 0~255(0 = 오른쪽, 64 = 아래)
		ObjID    enemy{};            // 커서에 가장 가까운 적(외곽선 강조 · 대상형 기술의 자동 대상)
		bool     valid = false;
	};
	// 기술 하나를 지금 쓰면 어디로 가는가(시전과 "표시 후 시전" 미리보기가 같은 답을 쓴다)
	struct SkillAim
	{
		Entity*  target = nullptr;   // 대상형: 맞을 적
		int      tx = 0, ty = 0;     // 보낼 칸(지점형 = 떨어질 곳, 방향형 = 끝 칸)
		int      dx = 0, dy = 0;     // 방향형 8방향
		bool     ok = false;
	};
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
	// channel: SC_CHAT_EX 값 그대로(가장 높은 비트 = 접속 직후 최근 대화)
	void HandleChat(const ObjID& sender, const std::string& senderName, const std::string& text, uint8_t channel);

	// ── 행동(GameInput.cpp) ──
	void SetGoal(const Goal& g);
	void ClearGoal();
	void TickMovement();
	void TickGoal();
	bool StepToward(int gx, int gy, int stopWithin);
	bool StepAlong(Vector2 origin, Vector2 dir);        // 누른 채 이동: 커서 쪽 곧은 선을 따라 DDA 한 걸음(막히면 벽을 타고)
	[[nodiscard]] int PlanDir(int x, int y, Vector2 origin, Vector2 u) const;   // 선(origin, u)을 따르는 다음 걸음(DDA)
	bool RunLine(Vector2 target, Vector2& origin, Vector2& u);   // 걸음 계획 · 화면 안내선이 함께 쓰는 곧은 선(방향이 5° 넘게 바뀔 때만 다시 긋는다)
	[[nodiscard]] Vector2 PlanU(Vector2 u) const;                // 걸음 계획용 방향: 가려는 쪽이 돌고 있으면 한 걸음 뒤의 방향으로 조금 앞당긴다
	[[nodiscard]] bool LineClear(int x, int y, int gx, int gy, int stopWithin) const;
	bool Waypoint(int gx, int gy, int stopWithin, int& wx, int& wy);           // 곧게 갈 수 있는 가장 먼 경유점
	void UpdateGuide();                                 // 화면 추종자의 안내선(매 프레임)
	bool SendStep(int dir);
	int  PickMoveDir(int dx, int dy) const;   // 막히면 가장 가까운 열린 방향(미끄러짐)
	void SyncMover();
	void TryAttackTarget(Entity& target);
	void UseSlot(int slot);
	void UseItemSlot(int index);                        // 소모품 칸(전투 1~4 · 물약 · 보조 5~8)
	void Interact();                                    // G: 가장 가까운 NPC와 대화 · 물건 줍기 · 쓰다듬기
	[[nodiscard]] const WorldData::NpcRow* InteractNpc() const;   // G를 누르면 말 걸 NPC(없으면 nullptr)
	[[nodiscard]] Entity* InteractItem();                          // G를 누르면 주울 물건
	void InteractTarget(uint16_t& npc, ObjID& item);               // G가 향하는 대상 하나(머리 위 G 표시)
	void UpdateAim();
	[[nodiscard]] Entity* AimEnemy(const SkillRow* row);           // 대상형 기술 · 기본 공격의 자동 대상
	[[nodiscard]] SkillAim ResolveSkillAim(const SkillRow& row, const ObjID& forced);
	[[nodiscard]] bool IsEnemy(const Entity& e) const;             // 지금 공격할 수 있는 상대(Ctrl이면 모험가도)
	void BasicAttackAtAim(bool hold);
	void CastHot(int hot);                              // 기술 칸(Q~Z)을 커서 쪽으로
	void InitHotbar(bool loadSaved = true);             // 직업 기본 배치(+ 캐릭터별로 바꿔 둔 배치)
	[[nodiscard]] const SkillRow* HotSkill(int hot) const;
	[[nodiscard]] int HotOfSkill(uint16_t skillId) const;       // 그 기술이 놓인 칸(없으면 -1)
	void SetScriptAim(int tx, int ty, const ObjID& enemy);      // 자동 시나리오: 커서 대신
	[[nodiscard]] int MoveButton() const;               // 이동 = 좌클릭(설정: 좌우 반전)
	[[nodiscard]] int AttackButton() const;
	void LoadControls();                                // 키 · 소모품 칸(캐릭터별)
	void SaveControls();
	void ApplyFxOptions();   // 그래픽 품질 · 광과민 보호 · 다른 플레이어 연출 세기 · 흔들림(설정 → post · fxscale · 카메라)
	[[nodiscard]] Vector2 NpcTile(uint16_t npcId) const;  // NPC의 지금 칸(서버가 알려 준 배회 위치)
	// ── 마을 NPC(GameNpc.cpp) — 서버가 배회시키는 위치를 받아 걷게 그린다 ──
	struct NpcView
	{
		const WorldData::NpcRow* row = nullptr;
		int      tx = 0, ty = 0;             // 서버 칸
		Vector2  pos{}, logPos{};
		motion::Track track;                 // 100ms 뒤 재생 시간표 · 화면 추종자(Phase 1-C)
		motion::Body  body;                  // 방향 · 걸음 위상 · 자세
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
	// 지금 가야 할 곳(칸)과 이름(나침반 · 미니맵 · 지도). viaTeleporter: 길잡이를 거쳐 가라는 안내
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
	// 기술 id로 시전(Phase 2-B: 확장 요청 SKILL_EX_REQ — 누름). quiet: 안내 문구 없이(자동 공격·예약 시전).
	// forced: 대상을 정해서(자동 공격의 목표·사거리 밖에서 눌러 예약한 대상).
	bool TryCastSkill(uint16_t skillId, bool quiet = false, const ObjID& forced = ObjID{});
	[[nodiscard]] const SkillRow* SkillInSlot(int slot) const;   // 0 = 기본 공격(직업 표의 첫 칸)
	[[nodiscard]] static int CdIndex(uint16_t skillId);           // 재사용 대기 칸(SkillKey) — 서버와 같다
	// ── Phase 2-B 기술 구조 V3(game/GameSkill.cpp) ──
	void ReleaseHeldSkill(bool cancel);                // 차지 · 홀딩을 놓는다(키를 뗌 · 우클릭 취소)
	void TickSkills(float dt);                         // 차지 게이지 · 홀딩 틱 연출 · 타격 시각표 · 놓침 확인
	// 타격 시각표(예측 연출): ctx = 시전 연출 문맥(대상 예측 포함), 칸 좌표로 시작 · 조준 · 방향
	void StartCastTimeline(const skillfx::ExCtx& ctx, const ObjID& caster, Vector2 originTile, Vector2 aimTile, Vector2 dirTile,
		const ObjID& target, bool releaseOnly, double t0);
	// 연출 문맥(시전자 · 조준 · 방향 · 예측 대상) — 내 시전 · 남의 알림이 같이 쓴다
	skillfx::ExCtx MakeExCtx(const Entity& caster, uint16_t skillId, uint8_t phase, uint8_t stage, uint8_t flags, Vector2 originTile,
		Vector2 aimTile, Vector2 dirTile, const Entity* target, bool mine);
	[[nodiscard]] int CountInShape(const SkillHit& h, Vector2 center, Vector2 dirTile) const;   // 모양 안의 적(예측)
	[[nodiscard]] static int CastSound(uint16_t skillId);              // 기술 시전음(GameCombat의 표)
	[[nodiscard]] static bool SkillTrace();                            // HARVEN_SKILL_TRACE=1: 기술 요청 · 결과 · 알림 · 타격을 stderr로(시험용)
	void HandleSkillEx(const SKILL_EX_NFY_PACKET& p);
	void HandleForcedMove(const SUBJECT_FORCED_MOVE_NFY_PACKET& p);
	void HandleCombatGauge(const COMBAT_GAUGE_INF_PACKET& p);
	void HandleMonsterGauge(const MONSTER_GAUGE_NFY_PACKET& p);
	void ApplyHitReaction(Entity& v, uint16_t skillId, uint8_t hitIndex, uint8_t flags, uint8_t flags2);
	void StartAwakenCinematic(uint16_t skillId, Vector2 focusIso);
	void DrawCinematic();                              // 각성기 컷인 · 차지 게이지(화면)
	void DrawIdentityGauge(Rectangle r);
	// ── Phase 2-E 훈련장(마을 동쪽) — 안전지대지만 기술을 쓸 수 있고 허수아비만 맞는다 ──
	[[nodiscard]] bool CombatBlocked() const;          // 마을 안(훈련장 밖): 싸울 수 없다
	void DrawDummyMeters(const Camera2D& cam);         // 허수아비 발밑: 아직 안 때렸으면 안내 한 줄
	void DrawYardMeter();                              // 훈련 계측 판(왼쪽): 초당 피해 · 합계 · 가장 큰 한 방 · 기술별 몫
	// ── Phase 2-D 기술 성장(game/GameSkillGrow.cpp) — 레벨 · 점수 · 갈래는 서버 권위(SKILL_TREE_INF · SKILL_GROW_ACK) ──
	[[nodiscard]] SkillGrowth::State MyGrow(uint16_t skillId) const;   // 내 기술 레벨 · 갈래
	[[nodiscard]] uint8_t MyVarBits(uint16_t skillId) const;           // 켜진 갈래(SKILL_EX_NFY.flags 비트 1~4와 같은 모양)
	[[nodiscard]] const SkillDef* MyDef(uint16_t skillId) const;       // 갈래를 입힌 내 기술 표(skillview)
	void HandleSkillTree(const SKILL_TREE_INF_PACKET& p);
	void HandleSkillGrowAck(const SKILL_GROW_ACK_PACKET& p);
	void SendSkillGrow(SkillGrowOp op, uint16_t skillId, uint8_t tier = 0, uint8_t choice = 0);
	void DrawSkillGrowPanel(Rectangle r, uint16_t skillId);             // 기술 창 오른쪽: 레벨 · 갈래 · 시연
	// 기술 시연(game/GameSkillStage.cpp) — 월드 밖 무대에서 실제 연출 시스템으로 반복 재생, 렌더 텍스처에 그린다.
	// 그리는 쪽이 보여 줄 동안 매 프레임 DrawSkillStage를 부른다(0.25초 안 부르면 멈춘다). varBits: 입힐 갈래(미리 보기)
	void TickSkillStage(float dt);
	void RenderSkillStage();                                            // BeginDrawing 전에(렌더 텍스처)
	void DrawSkillStage(Rectangle r, uint16_t skillId, uint8_t varBits, const char* caption);
	// 시험(--script skillgrow · skills + HARVEN_GROW="12"): 여덟 칸을 targetLevel까지 올리고 갈래 Ⅰ=a · Ⅱ=b를 고른다. 끝나면 true
	bool ScriptGrowStep(uint8_t targetLevel, uint8_t pickA, uint8_t pickB);

	[[nodiscard]] int AttackRange() const;              // 자동 공격/예약 기술이 멈춰 설 거리
	[[nodiscard]] Vector2 PredictLanding(const SkillRow& row, int tx, int ty, const Entity* target) const;
	void AddCombo(int hits);
	void PickupHere();
	void TargetNearestMonster();                        // C를 누르고 있을 때: 가까운 적을 이어서
	void OpenNpc(uint16_t npcId);
	void CloseNpcWindows();
	void StartEnchant(int scrollSlot);
	void SubmitChat();
	// ── 상시 채팅창(Phase 3-B, game/GameChat.cpp) ──
	void AddChatMsg(chat::Ch ch, const std::string& sender, uint64_t senderKey, const std::string& text, Color color = Color{ 0, 0, 0, 0 }, bool history = false);
	void SendChatLine(const chat::Target& t, const std::string& body);   // 클라 검사 → CS_CHAT_EX
	void OpenChatInput(const std::string& prefill = std::string());
	void ChatInsertItemLink(uint16_t itemId, uint8_t enchant, uint8_t rarity);   // 소지품 Shift+클릭
	void DrawChatMenu();                                // 이름 메뉴 · 탭 설정(맨 위 작은 창)
	[[nodiscard]] bool ChatBlocked(const std::string& name) const;
	[[nodiscard]] std::string ChatDisplayText(const std::string& text) const;   // 아이템 고리 → [이름] (말풍선)

	// ── 입력 ──
	void HandleWorldInput();
	void HandleHotkeys();
	void PickUnderMouse();
	bool CloseTopWindow();                              // Esc: 맨 위 창부터 닫는다(닫은 게 있으면 true)
	void CloseWindowById(const char* id);               // 창 관리자 id → 그 창의 닫기(소리 포함)

	// ── 그리기 ──
	void DrawWorld(const Camera2D& cam);
	void DrawEntity(Entity& e, bool translucentPass);
	void DrawNpcs();
	void DrawWorldOverlay(const Camera2D& cam);
	void DrawHud();
	void DrawChat();
	// ── 창 관리(Phase 3-A, ui/Wm) ──
	void DrawWindows();                                 // 창 목록 → 겹침 순서 · 열기/닫기 애니메이션
	[[nodiscard]] float EffectiveUiScale() const;       // UI 배율(설정 · 자동)
	[[nodiscard]] bool ModalOpen() const;               // 메뉴 · 조작 설정 · 버리기 확인 · 죽음
	[[nodiscard]] std::string UiLayoutPath() const;     // config/ui_<캐릭터>.json
	void LoadUiLayout();
	void SaveUiLayout();
	void DrawQuickBar();                                // 소모품 칸(전투 1~4 · 물약 F1 · 보조 5~8)
	void DrawSkillBar();                                // 기술 칸(QWER · ASDF · V · Space · Z)
	void DrawHotSlot(Rectangle r, int hot, float keySize);
	void DrawCombatPanel();                             // 아래 가운데 판: HP · MP · 능력치 한 줄
	[[nodiscard]] float HudTop() const;                 // 전투 HUD 판의 위쪽 y(대화창 · 채팅이 피한다)
	[[nodiscard]] float HudLeft() const;                // 전투 HUD 판의 왼쪽 x
	void ApplyKeyBind(int act, int key, uint8_t mods, bool swapIfTaken);
	void SaveHotbar();
	void DrawSystemMenu();                              // Esc(열린 창이 없을 때)
	void DrawKeySettings();                             // 단축키 설정 · 조작 설정
	void DrawSkillWindow();                             // K
	void DrawBigMap();                                  // Tab: 화면 가운데 큰 반투명 지도
	void DrawMinimapAt(Rectangle m, int range, float alpha, bool bigMap);
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
	void DrawDebugOverlay();                            // F3: 프레임 시간 그래프 · 드로우콜 · 입자 · 네트워크 · 이동

	// ── 도우미 ──
	Entity* Self();
	Entity* Find(const ObjID& id);
	[[nodiscard]] bool InTown() const;
	[[nodiscard]] int GroundAt(Vector2 tile) const;     // 0 풀 · 1 돌 · 2 나무 · 3 눈 · 4 모래
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
	bool RunScriptUi(float dt);                          // Phase 3 시험 시나리오(wm · chat3:<a|b|c> · uiedit · hudres)

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
	float            m_uiW = 1280.f, m_uiH = 720.f;  // UI 논리 화면(화면 ÷ UI 배율) — HUD · 창 배치는 이 값으로
	bool             m_uiEdit = false;               // UI 편집 모드(HUD 덩어리 옮기기 · 배율)
	double           m_uiSaveAt = 0.0;               // 창 배치 저장(바뀐 뒤 2초에 한 번)

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
	// 재사용 대기 — 칸(SkillKey: C Q W E R A S D F V Space Z)마다. 서버의 m_keyReadyAt과 같은 칸
	std::array<double, kSkillKeys> m_skillReady{};  // 다시 쓸 수 있는 시각(로컬)
	std::array<double, kSkillKeys> m_skillCdTotal{};// 쿨다운 길이(스윕 표시)
	std::array<double, kSkillKeys> m_skillFlash{};  // 쿨다운이 끝난 순간(반짝임)
	uint16_t         m_castSeq = 0;
	int              m_queuedSkill = -1;            // 사거리 밖에서 누른 기술 id — 다가가서 쓴다
	double           m_queuedAt = 0.0;
	ObjID            m_queuedTarget{};
	struct DelayedHit { double at; SUBJECT_DAMAGE_NFY_PACKET p; };
	std::vector<DelayedHit> m_delayedHits;
	int              m_comboHits = 0;
	double           m_comboLastAt = -10.0;
	std::string      m_comboName;
	double           m_comboNameAt = -10.0;
	bool             m_charOpen = false;            // 캐릭터(능력치) 창
	std::array<double, kSkillKeys> m_castSentAt{};  // 칸별 마지막 시전 요청 시각(쿨다운 기준)
	double           m_actionLockUntil = 0.0;       // 기술 동작 중엔 걸음을 보내지 않는다(무게감)
	uint16_t         m_dashWaitSeq = 0;             // 돌진류: 서버 도착 칸을 받을 때까지 멈춤
	double           m_dashWaitUntil = 0.0;
	int              m_bufferSlot = -1;             // 쿨다운 직전에 누른 기술 id(선입력)
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
	std::vector<ChainHop> m_chainHops;              // 벼락 사슬: 직전에 맞은 곳(다음 줄기의 시작점)
	std::array<double, kSkillKeys> m_readyPrev{};   // 쿨다운 끝 알림(반짝임) 판정용
	// Phase 2-B 기술 상태(예측 — 서버 알림이 바로잡는다)
	uint16_t         m_heldSkill = 0;               // 차지 · 홀딩 중인 기술
	int              m_heldHot = -1;                // 그 기술의 칸(키를 떼는지 본다)
	double           m_heldAt = 0.0;                // 누른 시각
	bool             m_heldCharge = false;          // 차지(떼면 단계) / 홀딩(누르는 동안 틱)
	double           m_holdTickAt = 0.0;
	uint8_t          m_comboStage = 0;              // 기본 공격 콤보(예측)
	double           m_comboAt = -10.0;
	uint16_t         m_chainSkill = 0;              // 체인 입력 창
	uint8_t          m_chainStage = 0;
	double           m_chainUntil = 0.0;
	uint16_t         m_toggleSkill = 0;             // 켜 둔 토글(철벽 태세)
	uint16_t         m_identity = 0;                // 고유 기술 게이지 0~1000(서버 COMBAT_GAUGE_INF)
	// Phase 2-E 허수아비 계측(내가 준 피해만) — 세 허수아비를 합쳐 한 번의 "측정". 5초 동안 안 때리면 다음 타격부터 다시 센다
	struct YardMeter
	{
		double   first = 0.0, last = -100.0;
		uint64_t total = 0;
		uint32_t hits = 0, best = 0;
		uint16_t bestSkill = 0;
		std::array<uint64_t, SkillGrowth::kIdSpan> bySkill{};   // 기술별 합계(몫 막대)
	};
	YardMeter        m_yard;
	// Phase 2-D 기술 성장(서버 SKILL_TREE_INF가 기준 — 요청은 결과가 올 때 바뀐다)
	std::array<uint8_t, SkillGrowth::kIdSpan> m_skillGrow{};   // 기술 id → SkillGrowth::Pack
	uint16_t         m_skillPoints = 0;             // 남은 기술 점수
	uint16_t         m_skillPointsTotal = 0;
	bool             m_skillTreeKnown = false;
	uint16_t         m_growSeq = 0;
	double           m_growBusyUntil = 0.0;         // 요청을 보내고 결과를 기다리는 동안(연타 막기)
	uint16_t         m_growSel = 0;                 // 기술 창에서 고른 기술
	double           m_growFlashAt = -10.0;         // 레벨이 오른 순간(창 반짝임)
	uint16_t         m_growFlashSkill = 0;
	Rectangle        m_growCardRect[2][2]{};
	uint16_t         m_scriptSegSkill = 0;          // skills 시험: 마지막으로 [skillseg]를 찍은 기술
	uint64_t         m_frameNo = 0;                 // 그린 프레임 수(추적: 예측 타격 ↔ 서버 피해가 몇 프레임 차이인지)
	struct Bench { struct Part { int frames = 0; float workSum = 0.f, workMax = 0.f; int drawSum = 0, drawMax = 0, partMax = 0, over33 = 0; } part[3]; };
	Bench            m_bench;                       // --script awaken3 구간별 계측
	uint16_t         m_growAckSeq = 0;              // 마지막 SKILL_GROW_ACK(시험 growcheck가 기대값과 맞춰 본다)
	uint8_t          m_growAckResult = 0xFF;
	int              m_checkPass = 0, m_checkFail = 0;        // 갈래 칸(마지막으로 그린 자리 — 시험 스크립트가 마우스를 올린다)
	// 기술 시연 무대(Phase 2-D)
	struct SkillStage
	{
		RenderTexture2D rt{};
		bool     ready = false;
		uint16_t want = 0;            // 보여 달라는 기술(이번 프레임에 그린 쪽이 정한다)
		uint8_t  wantBits = 0;        // 그 기술에 입힐 갈래
		double   wantAt = -10.0;
		uint16_t skill = 0;           // 지금 도는 시연(0 = 멈춤)
		uint8_t  flags = 0;
		double   t0 = 0.0;
		float    endAt = 0.f;         // 한 바퀴 길이(초)
		int      step = 0;            // 다음에 알릴 단계
		struct Evt { float at = 0.f; uint8_t phase = 0, stage = 0, flags = 0; bool release = false; };
		struct Beat { float at = 0.f; uint8_t hit = 0; uint8_t evt = 0; bool tick = false; bool fired = false; };
		std::vector<Evt>  evts;
		std::vector<Beat> beats;
		float    holdFrom = -1.f, holdTo = -1.f;   // 모으기 · 돌기 구간
		Vector2  slideFrom{}, slideTo{};
		float    slideAt = -1.f, slideDur = -1.f;  // 이동 기술: 미끄러짐
		Entity   caster;
		Entity   dummies[3];
	};
	SkillStage       m_stage;
	bool             m_identityFull = false;
	double           m_identityActiveUntil = 0.0;
	double           m_identityFullAt = -10.0;
	struct CastTimeline
	{
		ObjID    caster;
		uint16_t skillId = 0;
		uint8_t  stage = 0, flags = 0;
		Vector2  origin{}, aim{}, dir{ 1.f, 0.f };   // 칸 좌표
		ObjID    target;
		double   t0 = 0.0;
		uint32_t fired = 0;                          // 쏜 타격표 비트
		bool     releaseOnly = false;
		bool     mine = false;
		bool     charging = false;                   // 남의 차지: 놓을 때까지 모으는 연출만
		bool     leapDone = false;                   // 단죄: 0.6초에 뛰어오름
		double   holdUntil = 0.0;                    // 홀딩: 이때까지 틱 연출
		double   nextTick = 0.0;
		skillfx::ExCtx ctx;                          // 연출 문맥(예측 대상 · 방향)
	};
	std::vector<CastTimeline> m_timelines;
	struct Cinematic { uint16_t skillId = 0; double start = -10.0; Vector2 focus{}; uint8_t cls = 0; };
	Cinematic        m_cine;
	struct AwakeTotal { uint16_t skillId = 0; int sum = 0; int hits = 0; double lastAt = 0.0; Vector2 iso{}; };
	AwakeTotal       m_awakeTotal;
	float            m_feelTrauma = 0.f, m_feelKick = 0.f, m_feelStop = 0.f;   // 이번 프레임 적중 체감(최댓값만)
	uint16_t         m_expectDashSkill = 0;         // 기술 이동을 기다린다(서버의 내 SUBJECT_MOVE_NFY를 순간이동 대신 미끄러짐으로)
	double           m_expectDashUntil = 0.0;
	double           m_lastBeatAt = -10.0;          // 내 기술 타격 예측(히트스톱을 이미 줬다)
	uint16_t         m_lastBeatSkill = 0;
	std::unordered_map<uint64_t, std::vector<ObjID>> m_bounceMarks;   // 방패 투척: 시전자 → 튕긴 대상(2단 연출)
	int              m_scriptHoldAct = -1;          // 스크립트: 누르고 있는 기술 키(kb::Act)
	double           m_scriptHoldUntil = 0.0;
	bool             m_scriptHolding = false;

	// 이동 예측(game/Motion.h). m_srv*/m_pred*는 mover 값을 매 프레임 비춰 둔 사본.
	motion::LocalMover m_mover;
	int              m_srvX = 0, m_srvY = 0;
	int              m_predX = 0, m_predY = 0;
	std::vector<path::Step> m_path;
	double           m_repathAt = 0.0;
	int              m_pathGoalX = -1, m_pathGoalY = -1;   // m_path를 찾은 목적지
	int              m_wayX = 0, m_wayY = 0;               // 지금 향하는 경유점(곧은 선의 끝)
	bool             m_wayValid = false;
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

	std::unordered_map<ObjID, Entity> m_ents;
	ClientInventory  m_inv;

	// UI 상태
	std::deque<ChatLine> m_chat;                // (예전 채팅 기록 — 호환용, 그리지 않는다)
	// 상시 채팅창(Phase 3-B)
	std::vector<chat::Tab> m_chatTabs;
	int              m_chatTab = 0;
	chat::Settings   m_chatSet;
	chat::Target     m_chatTarget;                  // 입력칸 머리(보낼 곳)
	std::string      m_lastWhisperFrom;             // /r
	std::string      m_lastWhisperTo;               // 귓속말 실패 알림
	std::vector<std::string> m_chatHistory;         // ↑↓(보낸 줄)
	int              m_chatHistIdx = -1;
	uint32_t         m_chatSerial = 0;
	struct ChatLinkRef { std::string shown, token; };
	std::vector<ChatLinkRef> m_chatLinks;           // 입력칸의 [아이템 이름] ↔ 표식
	int              m_chatMenu = 0;                // 0 없음 · 1 이름 메뉴 · 2 탭 설정
	std::string      m_chatMenuName;
	Vector2          m_chatMenuPos{};
	int              m_chatMenuTab = -1;
	double           m_chatNoteAt = -10.0;          // 클라 안내(너무 김 등) 1초에 한 번
	float            m_chatBgA = 0.35f;             // 배경 투명도(부드럽게 따라간다)
	uint32_t         m_chatSeen = 0;                // 보고 있는 탭이 마지막으로 반영한 줄 번호(올려 보기 고정)
	ui::TextField    m_chatField;
	bool             m_chatOpen = false;
	std::deque<Banner>   m_banners;
	std::deque<Toast>    m_toasts;
	std::deque<CenterMsg> m_center;
	std::array<QuickSlot, kItemSlots> m_quick{};
	// ── 조작(1-A) ──
	std::array<uint16_t, kHotSlots> m_hot{};       // 칸마다 기술 id(0 = 비었음)
	ClassId          m_hotClass = ClassId::Count;   // m_hot을 만든 직업
	Aim              m_aim;
	bool             m_aimScripted = false;         // 자동 시나리오가 조준을 정했다(이번 프레임)
	int              m_aimingHot = -1;              // 표시 후 시전: 누르고 있는 칸
	bool             m_moveHeld = false;            // 이동 버튼을 누르고 있다
	bool             m_moveDirMode = false;         // 누른 채 커서 쪽으로 계속
	double           m_movePressAt = 0.0;
	ObjID            m_movePressEnt{};              // 누른 순간 커서 밑 모험가 · 물건(따라가기 · 줍기)
	bool             m_lineValid = false;           // 곧은 선(RunLine): 원점 · 방향(칸 좌표)
	Vector2          m_lineO{}, m_lineU{};
	Vector2          m_wantPrev{};                  // 가려는 방향(화면 위치 → 목표)과 그 회전 속도(라디안/초, 거른 값)
	double           m_wantAt = 0.0;
	float            m_wantTurn = 0.f;
	int              m_movePressNpc = 0;            // 누른 순간 커서 밑 NPC(대화)
	Vector2          m_dirTarget{};                 // Direction 목표(커서 아래 바닥)
	double           m_whiffAt = -10.0;             // 빈 곳에 휘두른 시각(기본 공격)
	bool             m_menuOpen = false;            // 시스템 메뉴
	bool             m_keysOpen = false;            // 단축키 설정
	int              m_keysCapture = -1;            // 새 키를 기다리는 동작(kb::Act), -1 없음
	int              m_keysConflict = -1;           // 겹친 동작(맞바꿀지 묻는 중), -1 없음
	int              m_keysConflictFor = -1;        // 그 키를 원한 동작
	int              m_keysConflictKey = 0;         // 그때 누른 키(kb::Chord.key)
	uint8_t          m_keysConflictMods = 0;
	int              m_keysPendingMod = 0;          // 보조키만 눌렀다 뗄 때(강제 공격 = Ctrl 같은)
	std::string      m_keysNote;                    // 방금 바꾼 결과 한 줄
	double           m_keysNoteAt = -10.0;
	bool             m_skillWinOpen = false;        // K
	bool             m_bigMapOpen = false;          // Tab
	bool             m_invOpen = false;
	bool             m_helpOpen = false;
	bool             m_mapOpen = false;
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
	bool             m_cloneDraw = false;           // 먹물 분신(반투명 먹빛 복제)
	int              m_pressKind = 0;      // 1 인벤토리, 2 퀵슬롯
	int              m_pressIndex = -1;
	Vector2          m_pressPos{};
	int32_t          m_lastExpLost = 0;
	RenderTexture2D  m_minimapRt{};
	int              m_minimapX = -9999, m_minimapY = -9999;
	RenderTexture2D  m_bigMapRt{};                  // Tab 확장 미니맵(몇 칸 움직일 때마다 다시 그린다)
	int              m_bigMapX = -9999, m_bigMapY = -9999;
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

	// F3 디버그 정보
	bool             m_debugOverlay = false;
	float            m_moveRttMs = 0.f;             // 이동 요청 → 응답 왕복(지수 평균)
	float            m_moveRttLastMs = 0.f;
	uint64_t         m_netBytesInPrev = 0, m_netBytesOutPrev = 0;
	uint32_t         m_netPacketsPrev = 0;
	double           m_netRateAt = 0.0;
	float            m_netInKBs = 0.f, m_netOutKBs = 0.f, m_netPps = 0.f;

	// 테스트 자동화
	float            m_runTime = 0.f;
	bool             m_shotTaken = false;
	int              m_shotIndex = 0;
	int              m_scriptStep = 0;
	float            m_scriptT = 0.f;
	Vector2          m_scriptFrom{}, m_scriptTo{};
	float            m_scriptMark = 0.f;                    // 녹화 시나리오: 달리기 시작 시각(시작 칸에 선 뒤) · Phase 3 시험: 단계 시작 시각
	int              m_scriptKeyX = 0, m_scriptKeyY = 0;   // 스크립트가 누른 WASD(화면 방향)
	double           m_scriptNextCast = 0.0;                // aimcombat: 다음 기술 키
	struct ScriptCast { int hot; float hold; float wait; };   // skills: 칸 · 누르고 있을 시간 · 다음까지
	std::vector<ScriptCast> m_scriptSeq;
	int              m_scriptSeqIdx = 0;
	int              m_scriptSeqLoops = 0;
	double           m_scriptApproachAt = -10.0;
	bool             m_scriptGrowDone = false;      // skills + HARVEN_GROW: 갈래를 다 골랐다
	int              m_scriptCastIdx = 0;
	uint32_t         m_lastPacketsIn = 0;                   // 통신 멈춤 판단(원격 외삽 — motion::SetNetQuiet)
	double           m_lastRecvAt = 0.0;
	std::FILE*       m_traceFile = nullptr;
};
