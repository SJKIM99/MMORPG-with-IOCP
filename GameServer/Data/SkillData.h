#pragma once

#include "Data/CombatData.h"

#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 구조 V3(Phase 2-B) — 로스트아크식 칸(C · QWERASDF · V · Space · Z)과 유형 · 타격 시각표. 서버 · 클라 공용.
//
//  · SkillTable(CombatData.h)의 행은 이름 · 자원 · 사거리 · 조준 · 상태이상 · 연계 같은 "무엇"이고,
//    이 표(SkillDefTable)는 "언제 · 어디를 · 어떻게"다 — 유형, 타격 시각(ms) 배열, 타격마다 모양 · 계수 · 반응 · 무력화,
//    차지 · 콤보 · 체인 단계, 홀딩 · 토글 틱, 슈퍼아머 · 무적 구간, 연출 등급.
//  · 서버와 클라가 같은 타격 시각표를 쓴다 → 연출의 임팩트와 서버 피해가 맞는다.
//    지연 타격은 서버가 그 시각에 모양을 다시 계산한다(예고를 보고 피할 수 있다).
//  · 확장 요청(SKILL_EX_REQ)만 이 표로 판정한다. 예전 SKILL_CAST_REQ는 예전 규칙 그대로(슬롯 0~5만).
//  · 타격이 없는 행(hitCount = 0)은 예전 판정(SkillHelper Execute)을 이 표의 시각에 부른다 — 기존 18기술의 연계를 그대로 살린다.
// ─────────────────────────────────────────────────────────────────────────────

enum class SkillKey : uint8_t { C, Q, W, E, R, A, S, D, F, V, Space, Z, Count };
inline constexpr int kSkillKeys = static_cast<int>(SkillKey::Count);

enum class SkillKind : uint8_t
{
	Basic,      // 기본 공격 — 콤보(stages타, 입력 창 안에 다시 누르면 다음 타)
	Normal,     // 일반(누르면 바로)
	Charge,     // 차지: 누르고 있다 떼면 단계(stages)별 강화, 마지막 perfectMs = 완벽 구간
	Hold,       // 홀딩: 누르는 동안 tickMs마다 타격, 떼면(또는 holdMaxMs) 마무리 타격
	Cast,       // 캐스팅: castMs 뒤 지점에
	Chain,      // 체인: 입력 창(stageMs) 안에 다시 누르면 다음 단계(stages까지)
	Toggle,     // 토글: 켜 둔 동안 tickMs마다 MP 소모(최대 holdMaxMs), 다시 누르면 끔
	Move,       // 이동기(Space) — 다른 기술의 후딜을 끊는다
	Identity,   // 고유 기술(Z) — 직업 게이지가 가득일 때
	Awaken,     // 각성기(V) — 암전 · 컷인 · 무적
};

enum class HitShape : uint8_t { Target, Circle, Cone, Line, Ring };
enum class HitAnchor : uint8_t { Caster, Aim, Target };   // 타격 시각의 시전자 위치 · 시전 때 정한 지점 · 대상(따라간다)
enum class HitReact : uint8_t { None, Flinch, Knockback, Launch, Knockdown, Pull };

enum HitFlag : uint8_t
{
	HF_Status   = 1 << 0,   // 표의 상태이상(SkillRow.status)을 건다
	HF_Heavy    = 1 << 1,   // 강타(큰 히트스톱 · DAMAGE2_HEAVY)
	HF_Counter  = 1 << 2,   // 반격: 보스 강공격 예고를 끊는다
	HF_Execute  = 1 << 3,   // 단죄 규칙: 기절 · 체력 30% 이하 대상 ×2
	HF_Tick     = 1 << 4,   // 홀딩 · 토글의 반복 타격(tickMs마다)
	HF_Release  = 1 << 5,   // 홀딩 · 차지를 놓는 순간(atMs = 놓은 뒤)
	HF_Perfect  = 1 << 6,   // 차지 완벽 구간에서만
	HF_Final    = 1 << 7,   // 마지막 타(각성기 총 피해 · 크게)
};

enum class SkillSpecial : uint8_t
{
	None,
	Legacy,        // 예전 판정(SkillHelper Execute)을 castMs 뒤 한 번
	ShieldBounce,  // 방패 투척: 1단 = 적 사이 3번 튕김, 2단 = 튕긴 적을 끌어당기며 되돌아옴
	Rally,         // 맹세 함성: 주변 도발 + 나에게 함성(공격 +15%)
	Roll,          // 몸 굴리기: 3칸 + 짧은 무적
	OathRelease,   // 서약 해방
	Barrier,       // 결계 고리: 둔화 + 표식
	Resonance,     // 새김 울림: 8칸 안의 표식 대상이 차례로 폭발
	Clones,        // 먹물 분신: 다음 3타를 분신 둘이 40%로 따라 한다
	Veil,          // 밤 장막: 은신 + 이속 + 다음 기술 급소
	PoisonMist,    // 맹독 안개: 지점 장판(4초) — 틱마다 중독 · 둔화
	MoonCleave,    // 달 가르기: 차지 → 직선 돌진(지나간 적은 0.3초 뒤 한꺼번에)
	DuskAwaken,    // 그믐 베기(각성): 최대 7대상 사이 순간이동 → 한꺼번에 폭발, 중독 폭발
	Dodge,         // 잔상 회피(이동기)
	Blink,         // 옛길 도약(이동기)
	Dash,          // 돌진
	ShadowStep,    // 그늘 걸음
};

struct SkillHit
{
	uint16_t  atMs = 0;            // 시전(차지 · 홀딩은 놓은 순간) 기준
	uint16_t  coeff = 100;         // 공격력 × %
	HitShape  shape = HitShape::Target;
	HitAnchor anchor = HitAnchor::Caster;
	uint8_t   a = 0;               // 반경 · 길이(칸)
	uint8_t   b = 0;               // 부채꼴: 반각(°) / 직선: 폭(칸) / 고리: 안쪽 반경
	int8_t    along = 0;           // 중심을 조준 방향으로 이만큼(칸)
	HitReact  react = HitReact::None;
	uint8_t   stagger = 0;         // 무력화 수치
	uint8_t   flags = 0;           // HitFlag
	uint8_t   stage = 0;           // 기본 공격: 콤보 몇 타(1~) · 차지: 최소 단계 · 체인: 몇 단(1~). 0 = 언제나
	uint8_t   maxTargets = 0;      // 0 = 무제한
	// ── Phase 2-D 갈래(Data/SkillGrowth.h ApplyDef가 복사본에만 쓴다 — 표에는 0) ──
	int8_t    turn = 0;            // 조준 방향에서 돌린 각(°) — 세 줄기
	uint8_t   tag = 0;             // HitTag(메아리 · 세 줄기로 덧붙은 타격)
	uint8_t   src = 0;             // 덧붙은 타격이 따라 한 원래 타격 번호(연출이 같은 모양을 다시 그린다)
};

enum HitTag : uint8_t { HT_None = 0, HT_Echo = 1, HT_Twin = 2 };

struct SkillDef
{
	uint16_t     id = 0;           // SkillTable id
	SkillKey     key = SkillKey::C;
	SkillKind    kind = SkillKind::Normal;
	uint8_t      tier = 2;         // 연출 등급 T1~T4(DESIGN_SYSTEM 10장)
	SkillSpecial special = SkillSpecial::None;
	uint32_t     cooldownMs = 0;   // 확장 경로의 재사용 대기(0 = SkillRow.cooldownMs) — 각성기 90초는 16비트를 넘는다
	uint16_t     castMs = 0;       // 첫 타 기준 예고(각성기 연출 시간 포함)
	uint16_t     lockMs = 300;     // 동작 잠금(후딜) — 이동기(Space)로 끊을 수 있다
	uint16_t     superArmorMs = 0; // 시전 시작부터 경직 면역
	uint16_t     invulnMs = 0;     // 무적(이동기 · 각성기)
	uint8_t      stages = 0;       // 콤보 타수 · 차지 단계 · 체인 단수
	uint16_t     stageMs = 0;      // 차지: 한 단계 시간 / 콤보 · 체인: 다음 입력 창
	uint16_t     perfectMs = 0;    // 차지: 마지막 완벽 구간
	uint16_t     holdMaxMs = 0;    // 홀딩 · 토글 최대
	uint16_t     tickMs = 0;       // 홀딩 · 토글 틱
	uint8_t      tickMp = 0;       // 토글: 틱마다 MP
	bool         moveWhileCasting = false;
	uint8_t      range = 0;        // 확장 경로의 사거리(0 = SkillRow.range) — 각성기처럼 예전보다 멀리 닿는 것
	uint8_t      moveMs = 0;       // 기술 이동의 칸당 시간(ms) — 돌진 · 구르기 · 회피 · 달 가르기. 0 = 순간이동(도약 · 그늘 걸음)
	uint8_t      hitCount = 0;
	SkillHit     hits[8] = {};
	const char*  kindText = "";    // 툴팁 한 줄(유형 · 판정)
};

namespace SkillDefDetail
{
	using K = SkillKey;
	using Kd = SkillKind;
	using Sh = HitShape;
	using An = HitAnchor;
	using Re = HitReact;
	using Sp = SkillSpecial;

	constexpr SkillHit H(uint16_t at, uint16_t coeff, Sh shape, An anchor, uint8_t a, uint8_t b, int8_t along, Re react, uint8_t stagger, uint8_t flags, uint8_t stage = 0, uint8_t maxT = 0)
	{
		SkillHit h{};
		h.atMs = at; h.coeff = coeff; h.shape = shape; h.anchor = anchor; h.a = a; h.b = b; h.along = along;
		h.react = react; h.stagger = stagger; h.flags = flags; h.stage = stage; h.maxTargets = maxT;
		return h;
	}

	struct B
	{
		SkillDef d{};
		constexpr B(uint16_t id, K key, Kd kind, uint8_t tier) { d.id = id; d.key = key; d.kind = kind; d.tier = tier; }
		constexpr B& special(Sp s) { d.special = s; return *this; }
		constexpr B& cd(uint32_t ms) { d.cooldownMs = ms; return *this; }
		constexpr B& cast(uint16_t ms) { d.castMs = ms; return *this; }
		constexpr B& lock(uint16_t ms) { d.lockMs = ms; return *this; }
		constexpr B& armor(uint16_t ms) { d.superArmorMs = ms; return *this; }
		constexpr B& invuln(uint16_t ms) { d.invulnMs = ms; return *this; }
		constexpr B& stages(uint8_t n, uint16_t ms) { d.stages = n; d.stageMs = ms; return *this; }
		constexpr B& perfect(uint16_t ms) { d.perfectMs = ms; return *this; }
		constexpr B& hold(uint16_t maxMs, uint16_t tick) { d.holdMaxMs = maxMs; d.tickMs = tick; return *this; }
		constexpr B& drain(uint8_t mp) { d.tickMp = mp; return *this; }
		constexpr B& moving() { d.moveWhileCasting = true; return *this; }
		constexpr B& reach(uint8_t r) { d.range = r; return *this; }
		constexpr B& move(uint8_t msPerTile) { d.moveMs = msPerTile; return *this; }
		constexpr B& hit(const SkillHit& h) { if (d.hitCount < 8) d.hits[d.hitCount++] = h; return *this; }
		constexpr B& text(const char* t) { d.kindText = t; return *this; }
	};

	constexpr uint8_t kStatus = HF_Status, kHeavy = HF_Heavy, kCounter = HF_Counter, kExec = HF_Execute,
		kTick = HF_Tick, kRel = HF_Release, kPerf = HF_Perfect, kFinal = HF_Final;
}

namespace SkillDefTable
{
	using namespace SkillDefDetail;

	// 행 순서 = SkillTable id(0 = 빈 행). 새 기술은 SkillTable(CombatData.h)에도 같은 id로 있다.
	inline constexpr SkillDef kRows[] =
	{
		B(0, K::C, Kd::Normal, 1).d,

		// ── 서약기사 ────────────────────────────────────────────────────────────
		B(1, K::C, Kd::Basic, 1).stages(3, 900).lock(260)
			.hit(H(90, 100, Sh::Cone, An::Caster, 1, 60, 0, Re::Flinch, 4, 0, 1))
			.hit(H(90, 110, Sh::Cone, An::Caster, 1, 60, 0, Re::Flinch, 4, 0, 2))
			.hit(H(120, 150, Sh::Cone, An::Caster, 1, 70, 0, Re::Knockback, 10, kHeavy, 3))
			.text("3타 이어 치기 · 부채꼴 — 베기 → 올려 베기 → 방패 밀치기").d,
		B(2, K::Q, Kd::Normal, 2).special(Sp::Legacy).lock(320).armor(320)
			.hit(H(110, 0, Sh::Target, An::Target, 0, 0, 0, Re::Knockback, 25, kCounter))
			.text("일반 · 대상 · 기절 · 반격(보스 강공격 끊기)").d,
		B(3, K::W, Kd::Hold, 2).hold(1500, 250).lock(200).armor(1700).moving()
			.hit(H(0, 45, Sh::Circle, An::Caster, 1, 0, 0, Re::Flinch, 3, kTick))
			.hit(H(60, 150, Sh::Circle, An::Caster, 2, 0, 0, Re::Knockback, 14, kRel | kHeavy))
			.text("누르고 있기 1.5초 · 원 · 경직 면역 — 떼는 순간 마지막 한 바퀴").d,
		B(4, K::A, Kd::Normal, 2).special(Sp::Dash).move(55).lock(300).armor(350)
			.text("일반 · 직선 4칸 돌진 · 벽 충돌").d,
		B(5, K::S, Kd::Toggle, 2).hold(4000, 500).drain(3).lock(150)
			.text("켜고 끄기 — 켜 둔 동안 MP 소모(최대 4초) · 받는 피해 절반 + 반격").d,
		B(6, K::V, Kd::Awaken, 4).cd(90000).cast(1400).lock(1500).invuln(1500).reach(5)
			.hit(H(1000, 35, Sh::Circle, An::Aim, 3, 0, 0, Re::Flinch, 10, 0))
			.hit(H(1133, 35, Sh::Circle, An::Aim, 3, 0, 0, Re::Flinch, 10, 0))
			.hit(H(1266, 35, Sh::Circle, An::Aim, 3, 0, 0, Re::Flinch, 10, 0))
			.hit(H(1400, 420, Sh::Circle, An::Aim, 3, 0, 0, Re::Knockdown, 60, kExec | kHeavy | kFinal))
			.text("각성기 · 지점 원 3칸 · 빈사 · 기절 대상 2배").d,
		B(19, K::E, Kd::Charge, 3).stages(3, 380).perfect(140).lock(420).armor(1300)
			.hit(H(60, 90, Sh::Circle, An::Caster, 0, 0, 1, Re::Launch, 8, 0, 0))
			.hit(H(105, 90, Sh::Circle, An::Caster, 0, 0, 2, Re::Launch, 8, 0, 0))
			.hit(H(150, 90, Sh::Circle, An::Caster, 0, 0, 3, Re::Launch, 8, 0, 0))
			.hit(H(195, 100, Sh::Circle, An::Caster, 0, 0, 4, Re::Launch, 10, 0, 2))
			.hit(H(240, 110, Sh::Circle, An::Caster, 0, 0, 5, Re::Launch, 12, kHeavy, 3))
			.hit(H(330, 220, Sh::Circle, An::Caster, 1, 0, 5, Re::Knockdown, 30, kHeavy | kPerf | kFinal, 3))
			.text("3단 모으기 · 직선 5칸 — 균열이 땅을 달린다 · 완벽 구간에서 떼면 한 번 더 폭발").d,
		B(20, K::R, Kd::Chain, 2).special(Sp::ShieldBounce).stages(2, 3000).lock(300)
			.hit(H(150, 110, Sh::Target, An::Target, 0, 0, 0, Re::Flinch, 10, 0, 1))
			.hit(H(250, 130, Sh::Target, An::Target, 0, 0, 0, Re::Pull, 16, kHeavy, 2))
			.text("이어 쓰기 2회 · 적 사이 3번 튕김 → 다시 누르면 되돌아오며 끌어당김").d,
		B(21, K::D, Kd::Normal, 2).special(Sp::Rally).lock(350)
			.hit(H(200, 60, Sh::Circle, An::Caster, 2, 0, 0, Re::Flinch, 6, 0))
			.text("일반 · 원 2칸 · 도발 + 함성(공격 +15%)").d,
		B(22, K::F, Kd::Cast, 3).cast(600).lock(700)
			.hit(H(600, 90, Sh::Circle, An::Aim, 2, 0, 0, Re::Flinch, 12, 0))
			.hit(H(1000, 90, Sh::Circle, An::Aim, 2, 0, 0, Re::Flinch, 12, 0))
			.hit(H(1400, 140, Sh::Circle, An::Aim, 2, 0, 0, Re::Knockdown, 24, kStatus | kHeavy | kFinal))
			.text("준비 0.6초 → 지점 원 2칸 · 종이 세 번 울림 · 셋째에 기절(움직이면 끊김)").d,
		B(23, K::Space, Kd::Move, 2).special(Sp::Roll).move(70).cd(9000).lock(280).invuln(260)
			.text("이동기 3칸 · 짧은 무적").d,
		B(24, K::Z, Kd::Identity, 3).special(Sp::OathRelease).lock(400).invuln(400)
			.hit(H(150, 80, Sh::Circle, An::Caster, 2, 0, 0, Re::Knockback, 20, kHeavy))
			.text("고유 기술 — 서약이 가득일 때 12초 경직 면역 · 모든 타격에 잔향 30% · 단죄 재사용 절반").d,

		// ── 각인술사 ────────────────────────────────────────────────────────────
		B(7, K::C, Kd::Basic, 1).stages(3, 1000).lock(240)
			.hit(H(140, 90, Sh::Line, An::Caster, 7, 1, 0, Re::Flinch, 3, 0, 1, 1))
			.hit(H(140, 90, Sh::Line, An::Caster, 7, 1, 0, Re::Flinch, 3, 0, 2, 1))
			.hit(H(170, 125, Sh::Line, An::Caster, 7, 1, 0, Re::Flinch, 8, 0, 3))
			.text("3발 이어 쏘기 · 투사체 — 3발째 관통").d,
		B(8, K::Q, Kd::Normal, 2).special(Sp::Legacy).cast(250).lock(380)
			.hit(H(250, 0, Sh::Circle, An::Aim, 1, 0, 0, Re::None, 20, kCounter))
			.text("지점 원 2칸 · 빙결 — 서리 글자가 솟구친다").d,
		B(9, K::W, Kd::Normal, 2).special(Sp::Legacy).lock(360)
			.hit(H(120, 0, Sh::Circle, An::Caster, 2, 0, 0, Re::Knockback, 14, 0))
			.text("자신 주변 고리 2칸 · 빙결 대상 증발 폭발").d,
		B(10, K::Space, Kd::Move, 2).special(Sp::Blink).lock(200).invuln(200)
			.text("이동기 · 순간이동 4칸 · 떠난 자리 각인 폭발").d,
		B(11, K::E, Kd::Normal, 2).special(Sp::Legacy).lock(360)
			.hit(H(80, 0, Sh::Target, An::Target, 0, 0, 0, Re::Flinch, 10, 0))
			.text("대상 · 최대 5회 전도 — 표식 · 화상 · 빙결 대상으로 더 멀리").d,
		B(12, K::V, Kd::Awaken, 4).cd(90000).cast(1600).lock(1600).invuln(1700).reach(7)
			.hit(H(1600, 460, Sh::Circle, An::Aim, 3, 0, 0, Re::Launch, 60, kStatus | kHeavy | kFinal))
			.text("각성기 · 지점 원 3칸 · 빙결 대상 산산조각").d,
		B(25, K::R, Kd::Charge, 2).stages(3, 350).perfect(120).lock(380)
			.hit(H(90, 140, Sh::Line, An::Caster, 6, 1, 0, Re::Knockback, 12, kStatus, 0))
			.hit(H(120, 90, Sh::Cone, An::Caster, 6, 25, 0, Re::Flinch, 8, 0, 3))
			.hit(H(150, 120, Sh::Circle, An::Aim, 1, 0, 0, Re::Knockdown, 20, kPerf | kHeavy, 3))
			.text("3단 모으기 · 직선 관통 — 3단이면 세 갈래").d,
		B(26, K::A, Kd::Cast, 3).cast(800).lock(900)
			.hit(H(800, 90, Sh::Circle, An::Aim, 3, 0, 0, Re::Launch, 16, kStatus))
			.hit(H(1100, 40, Sh::Circle, An::Aim, 3, 0, 0, Re::None, 4, 0))
			.hit(H(1600, 40, Sh::Circle, An::Aim, 3, 0, 0, Re::None, 4, 0))
			.hit(H(2100, 40, Sh::Circle, An::Aim, 3, 0, 0, Re::None, 4, 0))
			.hit(H(2600, 40, Sh::Circle, An::Aim, 3, 0, 0, Re::None, 4, 0))
			.hit(H(3100, 40, Sh::Circle, An::Aim, 3, 0, 0, Re::None, 4, kFinal))
			.text("준비 0.8초 → 지점 원 3칸 · 둘레 불기둥 · 남은 불길 3초(움직이면 끊김)").d,
		B(27, K::S, Kd::Normal, 2).special(Sp::Barrier).lock(380)
			.hit(H(300, 50, Sh::Ring, An::Aim, 2, 1, 0, Re::None, 10, kStatus))
			.text("지점 고리 2칸 · 5초 둔화 + 표식").d,
		// 낙뢰 각인: 커서 아래 적이 있으면 그 적의 머리 위에 새긴다(따라간다), 없으면 지점. 몬스터가 쫓아오는 1.2초 동안
		// 지점이 비어 헛치던 것(Phase 2-D 시험: 서버 판정 6번 중 5번 대상 0) — 대상 기준으로 바꿨다
		B(28, K::D, Kd::Normal, 3).lock(360)
			.hit(H(1200, 260, Sh::Circle, An::Target, 1, 0, 0, Re::Knockdown, 30, kStatus | kHeavy | kFinal))
			.text("대상(없으면 지점) 원 1칸 · 1.2초 뒤 낙뢰 — 머리 위 벼락 글자 예고").d,
		B(29, K::F, Kd::Hold, 2).hold(2000, 250).lock(250).armor(2000)
			.hit(H(0, 38, Sh::Cone, An::Caster, 4, 30, 0, Re::Flinch, 4, kTick | kStatus))
			.hit(H(60, 90, Sh::Cone, An::Caster, 4, 30, 0, Re::Knockback, 10, kRel))
			.text("누르고 있기 2초 · 앞 부채꼴 60° 4칸 · 빙결 누적").d,
		B(30, K::Z, Kd::Identity, 3).special(Sp::Resonance).lock(500)
			.text("고유 기술 — 새김이 가득일 때 8칸 안 표식 대상이 차례로 폭발").d,

		// ── 밤사냥꾼 ────────────────────────────────────────────────────────────
		B(13, K::C, Kd::Basic, 1).stages(4, 800).lock(200)
			.hit(H(60, 55, Sh::Cone, An::Caster, 1, 45, 0, Re::Flinch, 3, 0, 1))
			.hit(H(60, 55, Sh::Cone, An::Caster, 1, 45, 0, Re::Flinch, 3, 0, 2))
			.hit(H(60, 60, Sh::Cone, An::Caster, 1, 45, 0, Re::Flinch, 3, 0, 3))
			.hit(H(90, 70, Sh::Cone, An::Caster, 1, 60, 0, Re::Knockback, 8, 0, 4))
			.hit(H(170, 70, Sh::Cone, An::Caster, 1, 60, 0, Re::Flinch, 6, 0, 4))
			.text("4타 이어 치기 · 부채꼴 — 4타 X자 베기").d,
		B(15, K::Q, Kd::Normal, 2).lock(300)
			.hit(H(100, 110, Sh::Cone, An::Caster, 1, 45, 0, Re::Flinch, 8, kStatus))
			.text("일반 · 부채꼴 90° 1.5칸 · 중독 1~5중첩").d,
		B(14, K::W, Kd::Normal, 2).special(Sp::ShadowStep).lock(260)
			.text("커서 근처 적의 등 뒤로 순간이동 · 기습").d,
		B(17, K::E, Kd::Normal, 2).special(Sp::Legacy).lock(360)
			.hit(H(140, 0, Sh::Target, An::Target, 0, 0, 0, Re::Knockdown, 18, 0))
			.text("일반 · 등 뒤면 급소").d,
		B(16, K::Space, Kd::Move, 2).special(Sp::Dodge).move(50).lock(200).invuln(300)
			.text("이동기 · 3칸 회피 · 무적 · 완벽 회피").d,
		B(18, K::V, Kd::Awaken, 4).special(Sp::DuskAwaken).cd(90000).cast(1400).lock(1600).invuln(1600)
			.hit(H(1400, 110, Sh::Circle, An::Caster, 4, 0, 0, Re::Launch, 30, kFinal, 0, 7))
			.hit(H(1460, 70, Sh::Circle, An::Caster, 4, 0, 0, Re::Flinch, 10, 0, 0, 7))
			.hit(H(1520, 70, Sh::Circle, An::Caster, 4, 0, 0, Re::Flinch, 10, 0, 0, 7))
			.hit(H(1580, 70, Sh::Circle, An::Caster, 4, 0, 0, Re::Flinch, 10, 0, 0, 7))
			.hit(H(1640, 90, Sh::Circle, An::Caster, 4, 0, 0, Re::Knockdown, 20, kHeavy | kFinal, 0, 7))
			.text("각성기 · 원 4칸 · 최대 7대상 · 중독 대상 독 폭발").d,
		B(31, K::R, Kd::Hold, 2).hold(1600, 160).lock(200).armor(1700).moving()
			.hit(H(0, 22, Sh::Circle, An::Caster, 1, 0, 0, Re::Flinch, 2, kTick))
			.hit(H(40, 80, Sh::Circle, An::Caster, 1, 0, 0, Re::Knockback, 8, kRel))
			.text("누르고 있기 1.6초 · 원 1.5칸 · 10연타").d,
		B(32, K::A, Kd::Normal, 2).special(Sp::Clones).lock(300)
			.text("일반 · 분신 둘이 다음 3타를 40%로 따라 한다").d,
		B(33, K::S, Kd::Chain, 2).stages(3, 2200).lock(260)
			.hit(H(120, 60, Sh::Cone, An::Caster, 5, 30, 0, Re::Flinch, 4, 0, 1, 5))
			.hit(H(120, 60, Sh::Cone, An::Caster, 5, 30, 0, Re::Flinch, 4, 0, 2, 5))
			.hit(H(150, 90, Sh::Line, An::Caster, 6, 1, 0, Re::Knockback, 8, kHeavy, 3))
			.text("이어 쓰기 3회 · 부채꼴 비수 다섯 · 세 번째는 관통").d,
		B(34, K::D, Kd::Normal, 2).special(Sp::PoisonMist).lock(300)
			.hit(H(200, 25, Sh::Circle, An::Aim, 2, 0, 0, Re::None, 2, kStatus))
			.text("지점 원 2칸 · 4초 · 중독 누적 + 둔화").d,
		B(35, K::F, Kd::Charge, 3).special(Sp::MoonCleave).move(40).stages(2, 450).perfect(150).lock(380).armor(1000)
			.hit(H(300, 180, Sh::Line, An::Caster, 4, 1, 0, Re::Launch, 20, kHeavy | kFinal, 0))
			.hit(H(300, 90, Sh::Line, An::Caster, 4, 1, 0, Re::Knockdown, 14, kPerf, 2))
			.text("2단 모으기 · 직선 돌진 베기 4칸 — 지나간 선이 0.3초 늦게 한꺼번에 터진다").d,
		B(36, K::Z, Kd::Identity, 3).special(Sp::Veil).lock(250)
			.text("고유 기술 — 그늘이 가득일 때 3초 동안 몬스터가 놓친다 · 이동 +30% · 다음 기술 급소").d,
	};

	inline constexpr int kCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

	[[nodiscard]] constexpr const SkillDef* Find(uint16_t id) noexcept
	{
		for (int i = 0; i < kCount; ++i)
			if (kRows[i].id == id && id != 0) return &kRows[i];
		return nullptr;
	}

	// 직업 · 칸 → 기술 id(기본 배치 — 칸은 클라 설정으로 바꿀 수 있고, 서버는 id로 받는다)
	[[nodiscard]] constexpr uint16_t Default(ClassId cls, SkillKey key) noexcept
	{
		constexpr uint16_t kMap[3][kSkillKeys] = {
			//  C   Q   W   E   R   A   S   D   F   V  Spc  Z
			{   1,  2,  3, 19, 20,  4,  5, 21, 22,  6, 23, 24 },   // 서약기사
			{   7,  8,  9, 11, 25, 26, 27, 28, 29, 12, 10, 30 },   // 각인술사
			{  13, 15, 14, 17, 31, 32, 33, 34, 35, 18, 16, 36 },   // 밤사냥꾼
		};
		const int c = static_cast<int>(cls);
		const int k = static_cast<int>(key);
		return (c >= 0 && c < 3 && k >= 0 && k < kSkillKeys) ? kMap[c][k] : 0;
	}

	// 기술 → 칸(재사용 대기 칸). 이 직업의 기술이 아니면 Count
	[[nodiscard]] constexpr SkillKey KeyOf(uint16_t id) noexcept
	{
		const SkillDef* d = Find(id);
		return d ? d->key : SkillKey::Count;
	}

	// 확장 경로의 재사용 대기(ms) — 표에 따로 있으면 그것(각성기 90초), 없으면 SkillRow. 쿨다운 감소 %는 SkillTable과 같은 규칙
	[[nodiscard]] constexpr uint32_t CooldownMs(const SkillDef& d, const SkillRow& row, uint16_t cdr) noexcept
	{
		const uint32_t r = cdr > 50 ? 50 : cdr;
		const uint32_t base = d.cooldownMs > 0 ? d.cooldownMs : row.cooldownMs;
		return base * (100u - r) / 100u;
	}

	// 차지 단계(0 = 모자람, 1~stages) · 완벽 구간
	[[nodiscard]] constexpr uint8_t ChargeStage(const SkillDef& d, uint32_t heldMs) noexcept
	{
		if (d.stageMs == 0) return 1;
		const uint32_t s = heldMs / d.stageMs;
		return static_cast<uint8_t>(s >= d.stages ? d.stages : (s < 1 ? 1 : s));
	}
	[[nodiscard]] constexpr uint32_t ChargeMaxMs(const SkillDef& d) noexcept { return static_cast<uint32_t>(d.stageMs) * d.stages + d.perfectMs; }
	// 누르고 있기의 첫 틱(ms) — 그 전 0.12초가 예고(힘이 모이는 빛). 서버 판정 · 클라 예측 · 기술 창 시연이 같이 쓴다
	inline constexpr uint16_t kHoldFirstTickMs = 120;
	[[nodiscard]] constexpr bool ChargePerfect(const SkillDef& d, uint32_t heldMs, uint32_t slackMs) noexcept
	{
		if (d.perfectMs == 0) return false;
		const uint32_t full = static_cast<uint32_t>(d.stageMs) * d.stages;
		return heldMs + slackMs >= full && heldMs <= full + d.perfectMs + slackMs;
	}

	// 타격의 방향(칸 공간 단위 벡터) — 세 줄기(갈래)로 덧붙은 타격은 turn°만큼 돌린다. 서버 · 클라가 판정 전에 부른다
	inline void TurnDir(const SkillHit& h, float& dx, float& dy) noexcept
	{
		if (h.turn == 0) return;
		const float a = static_cast<float>(h.turn) * 3.14159265f / 180.f;
		const float c = std::cos(a), s = std::sin(a);
		const float nx = dx * c - dy * s, ny = dx * s + dy * c;
		dx = nx; dy = ny;
	}

	// 판정 모양(칸 공간) — 서버 판정과 클라 예측 연출이 같은 함수를 쓴다.
	//   원 = 체비셰프 반경 · 부채꼴 = 유클리드 반경 + 0.5 안, 조준 방향과의 각 ≤ b° · 직선 = 투영 0 < t ≤ a + 0.5, 옆 ≤ b/2 + 0.35 · 고리 = b ≤ 체비셰프 ≤ a
	[[nodiscard]] inline bool InsideHit(const SkillHit& h, float cx, float cy, float dx, float dy, float x, float y) noexcept
	{
		const float rx = x - cx, ry = y - cy;
		const float ax = rx < 0.f ? -rx : rx, ay = ry < 0.f ? -ry : ry;
		const float cheb = ax > ay ? ax : ay;
		switch (h.shape)
		{
		case HitShape::Target: return cheb < 0.5f;
		case HitShape::Circle: return cheb <= static_cast<float>(h.a) + 0.001f;
		case HitShape::Ring: return cheb <= static_cast<float>(h.a) + 0.001f && cheb >= static_cast<float>(h.b) - 0.001f;
		case HitShape::Cone:
		{
			const float dist = std::sqrt(rx * rx + ry * ry);
			if (dist > h.a + 0.5f) return false;
			if (dist < 0.5f) return true;
			return (rx * dx + ry * dy) / dist >= std::cos(static_cast<float>(h.b) * 3.14159265f / 180.f);
		}
		case HitShape::Line:
		{
			const float t = rx * dx + ry * dy;
			if (t <= 0.f || t > h.a + 0.5f) return false;
			const float side = rx * dy - ry * dx;
			return (side < 0.f ? -side : side) <= static_cast<float>(h.b) * 0.5f + 0.35f;
		}
		}
		return false;
	}

	namespace Detail
	{
		constexpr bool Validate() noexcept
		{
			for (int i = 1; i < kCount; ++i)
			{
				const SkillDef& d = kRows[i];
				const SkillRow* r = SkillTable::Find(d.id);
				if (r == nullptr) return false;
				// 기본 배치의 칸과 표의 칸이 같아야 한다
				if (Default(r->cls, d.key) != d.id) return false;
				for (int j = 0; j < d.hitCount; ++j)
					if (d.hits[j].shape == HitShape::Cone && d.hits[j].b == 0) return false;
			}
			// 세 직업 × 12칸이 모두 표에 있다
			for (int c = 0; c < 3; ++c)
				for (int k = 0; k < kSkillKeys; ++k)
					if (Find(Default(static_cast<ClassId>(c), static_cast<SkillKey>(k))) == nullptr) return false;
			return true;
		}
	}
	static_assert(Detail::Validate(), "SkillDefTable: 기술 id · 칸 · 직업이 SkillTable과 맞지 않습니다.");
}
