#pragma once

#include <array>
#include <cstdint>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 조작 — "동작 → 키" 표. 게임은 키 번호가 아니라 동작 이름으로만 입력을 읽는다.
//
//  · 기본값은 로스트아크식(docs/upgrade/PHASE_1.md 5장): 이동 = 좌클릭, 기본 공격 = 우클릭 · C,
//    기술 8칸 Q W E R · A S D F, 각성기 V, 이동기 Space, 고유 기술 Z, 전투 칸 1~4 · 물약 F1 · 보조 5~8.
//  · 캐릭터별 저장(controls_<이름>.txt, 한 줄 = "동작 키 보조키" · 설정 = "이름=값").
//  · 채팅 입력 중(한글 조합 포함)에는 어떤 동작도 눌리지 않는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace kb
{
	enum class Act : uint8_t
	{
		// 전투
		BasicAttack,
		Skill1, Skill2, Skill3, Skill4, Skill5, Skill6, Skill7, Skill8,
		Awaken, Mobility, Identity, Identity2,
		ForceAttack,            // 누르고 있는 동안 강제 공격(PK)
		// 소모품
		Battle1, Battle2, Battle3, Battle4, Potion, Aux1, Aux2, Aux3, Aux4,
		// 상호작용 · 창
		Interact, Inventory, Character, SkillWindow, QuestLog, WorldMap, BigMap, Guild, Auction,
		Chat, Command, ShowNames, Help, DebugInfo, ZoomIn, ZoomOut,
		Count
	};
	inline constexpr int kActCount = static_cast<int>(Act::Count);

	enum Mod : uint8_t { ModNone = 0, ModCtrl = 1, ModShift = 2, ModAlt = 4 };

	struct Chord
	{
		int     key = 0;      // raylib KeyboardKey(0 = 없음)
		uint8_t mods = 0;     // Mod 조합(이 동작을 누를 때 함께 눌러야 하는 것)
		[[nodiscard]] bool Empty() const { return key == 0; }
		bool operator==(const Chord& o) const { return key == o.key && mods == o.mods; }
	};

	enum class CastMode : uint8_t { Quick, ShowThenCast };

	struct Options
	{
		bool     swapMouse = false;               // 좌클릭 = 공격, 우클릭 = 이동
		CastMode castMode = CastMode::Quick;      // 빠른 시전 · 표시 후 시전
		bool     showGrid = true;                 // 바닥 격자
		// Phase 2-A 전투 연출
		uint8_t  gfxQuality = 2;                  // 0 낮음 · 1 보통 · 2 높음(블룸 · 왜곡 · 입자 수)
		bool     photoSafe = false;               // 광과민 보호(번쩍임 · 흔들림 · 색수차 · 암전 절반)
		uint8_t  otherFx = 50;                    // 다른 플레이어 기술 연출 세기(%) — 25 · 50 · 100
		bool     shake = true;                    // 화면 흔들림
		// Phase 3-A UI
		uint8_t  uiScale = 0;                     // UI 배율(%) — 0 자동(화면 높이로) · 100 · 125 · 150 · 175 · 200
	};

	// ── 표 ──
	[[nodiscard]] const char* Name(Act a);         // 설정 창에 보이는 이름(예: "기술 1")
	[[nodiscard]] const char* Group(Act a);        // 묶음 제목(전투 · 소모품 · 창 · 기타)
	[[nodiscard]] Chord Default(Act a);
	[[nodiscard]] Chord Get(Act a);
	void Set(Act a, Chord c);
	void ResetDefaults();
	[[nodiscard]] Options& Opt();

	// 같은 키 조합을 쓰는 다른 동작(없으면 Count)
	[[nodiscard]] Act Conflict(Act a, Chord c);
	// 키 글자(예: "Q", "Spc", "F1", "Alt+U") — 작은 칸에 들어가게 짧게
	[[nodiscard]] std::string Label(Chord c);
	[[nodiscard]] std::string Label(Act a);
	[[nodiscard]] std::string KeyName(int key);

	// ── 입력(채팅 중이면 모두 false) ──
	void BeginFrame();                             // 막힘 판정 갱신(프레임 맨 앞)
	[[nodiscard]] bool Pressed(Act a);
	[[nodiscard]] bool Down(Act a);
	[[nodiscard]] bool Released(Act a);
	[[nodiscard]] bool Blocked();                  // 채팅 입력 · 한글 조합 · 키 지정 중
	void SetCapturing(bool on);                    // 키 설정 창이 새 키를 기다리는 중

	// 자동 시나리오: 이 동작을 이번 프레임에 누른 것으로(한 프레임)
	void ScriptPress(Act a);
	void ScriptHold(Act a, bool down);

	// HUD 칸 ↔ 동작. 기술 칸: 0 기본 공격 · 1~8 기술(Q W E R · A S D F) · 9 각성기 · 10 이동기 · 11 고유 기술.
	// 소모품 칸: 0~3 전투 1~4 · 4 체력 물약 · 5~8 보조 5~8.
	[[nodiscard]] Act HotAct(int hot);
	[[nodiscard]] Act ItemAct(int index);
	[[nodiscard]] bool IsModifierKey(int key);

	// ── 저장(캐릭터별) ──
	void Load(const std::string& character);       // 없으면 기본값
	void Save(const std::string& character);
}
