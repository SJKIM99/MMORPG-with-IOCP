#pragma once

#include <cstdint>
#include <cstddef>

#include "Data/WorldData.h"
#include "Data/MonsterTable.h"
#include "Data/CombatData.h"
#include "Data/ItemRules.h"
#include "Item/ItemTableData.h"

// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 · 메인 이야기 — 서버·클라 공용.
//
// 이야기의 뼈대: "새벽마다 뛰던 심장나무 하르벤이 멈췄다." (세계관 · 이름 규칙: docs/WORLD.md)
//   제1장 늦어지는 새벽(데니차) → 제2장 속삭이는 숲(올벤 숲) → 제3장 세 번의 종소리(캄파나 폐허)
//   → 제4장 깨어진 서약(서약의 언덕 · 올하르 사원) → 제5장 심장의 불씨(칼둔 균열 → 하르벤)
//
// 서버는 받기/완료 조건 · 진행(처치 · 수집 · 대화 · 탐험 · 보스 · 장비)을 판정하고 보상을
// 준다(서버 권위). 클라는 같은 표로 대사 · 목표 · 보상 · 표식(!/?) · 나침반을 그린다.
// 진행 상태는 User의 진행 데이터(PlayerProgress v3)에 저장된다.
//
// 규칙: 퀘스트 id는 한 번 배정하면 바꾸거나 재사용하지 않는다(완료 기록이 id 비트로
// 저장된다). 1~255.
//
// 수집(Collect) 물건은 가방에 들어가지 않는다 — 진행도로만 센다(가방 칸을 뺏지 않고,
// 버리거나 팔아서 퀘스트가 꼬이는 일이 없다). 운이 나빠도 끝없이 헤매지 않도록
// 실패할 때마다 확률이 조금씩 오른다(서버).
// ─────────────────────────────────────────────────────────────────────────────
namespace QuestData
{
	inline constexpr int     kMaxActive   = 8;       // 동시에 진행할 수 있는 퀘스트
	inline constexpr int     kMaxObjs     = 3;
	inline constexpr uint8_t kAnyZone     = 0xFF;
	inline constexpr int     kBossShareRange = 16;   // 보스가 쓰러질 때 이 거리(칸) 안에 있으면 함께 인정
	inline constexpr uint16_t kFinalQuest = 16;      // 끝나면 엔딩 · 전 서버 공지

	enum class QuestKind : uint8_t { Main, Side, Daily, Class };

	enum class ObjKind : uint8_t
	{
		None,
		Kill,      // target=종족, count          (zone이 있으면 그 지역에서만)
		KillIn,    // zone의 아무 몬스터 count마리
		Collect,   // target=떨어뜨리는 종족, count개, chance%(한 마리당) — 가방 밖 퀘스트 물건
		Talk,      // target=NPC — 말을 건다(동물은 쓰다듬기)
		Explore,   // target=장소(Poi id) — 반경 안에 들어간다
		Boss,      // target=보스 종족 — 쓰러질 때 가까이(kBossShareRange) 있으면 함께 인정
		Equip,     // count=장비를 입은 부위 수(지금 상태)
	};

	// 수집 물건 아이콘(클라가 그린다)
	enum class QuestIcon : uint8_t { None, Moss, Straw, Meat, SpiritLight, Sap, Crest, Ember, CarvedStone, Count };

	struct Objective
	{
		ObjKind     kind   = ObjKind::None;
		uint16_t    target = 0;
		uint16_t    count  = 0;
		uint8_t     zone   = kAnyZone;
		uint8_t     chance = 100;
		const char* label  = nullptr;     // 표시 이름(없으면 종류로 만든다) — 수집물 이름
		QuestIcon   icon   = QuestIcon::None;
	};

	struct Reward
	{
		uint32_t exp     = 0;
		uint8_t  expPct  = 0;         // > 0이면 "지금 레벨 필요 경험치의 N%"(반복 퀘스트)
		uint32_t adena   = 0;         // 그로시(내부 이름 Adena 유지)
		uint16_t item[3] = {};        // 직업별(서약기사 · 각인술사 · 밤사냥꾼). 0 = 없음
		uint16_t count   = 1;
		Rarity   rarity  = Rarity::Common;   // 장비: 적어도 이 희귀도(가끔 한두 단계 위로 — 서버 굴림)
	};

	struct QuestRow
	{
		uint16_t    id;
		QuestKind   kind;
		uint8_t     chapter;      // 메인 이야기 장(1~5), 그 밖은 0
		const char* name;
		uint16_t    giver;        // 퀘스트를 주는 NPC
		uint16_t    turnIn;       // 보고할 NPC(0 = 목표를 이루는 순간 완료)
		uint8_t     minLevel;
		uint16_t    prereq;       // 먼저 끝내야 하는 퀘스트(0 = 없음)
		uint8_t     classMask;    // 0 = 모든 직업(ItemTableData::kMask*)
		const char* summary;      // 일지 한 줄
		const char* offer;        // 받기 전 대사(주는 NPC)
		const char* during;       // 진행 중 대사(보고할 NPC · 주는 NPC)
		const char* done;         // 완료 대사(보고할 NPC)
		Objective   objs[kMaxObjs];
		Reward      reward;
		uint16_t    cooldownMin = 0;   // 반복 퀘스트: 다시 받기까지(분). 0 = 한 번뿐
	};

	// ── 장소(탐험 목표 · 지도 표식) ───────────────────────────────────────────
	struct Poi
	{
		uint16_t    id;
		const char* name;
		int16_t     x, y;
		uint8_t     radius;
		const char* flavor;       // 처음 들어섰을 때 이야기(이야기책 한 줄)
	};

	inline constexpr Poi kPois[] =
	{
		{ 1, "올벤 숲 어귀",        752,  272, 6, "나뭇잎이 바람도 없이 흔들린다. 귀를 기울이면… 누군가 '돌려줘'라고 속삭인다." },
		{ 2, "하르벤",              716,  226, 6, "하늘을 가릴 만큼 큰 나무가 숨을 멈춘 채 서 있다. 가슴께의 옹이가 텅 비어 있다." },
		{ 3, "캄파나 대성당",      1267,  729, 7, "지붕이 무너진 대성당. 제단은 비어 있고, 바람도 없는데 종탑의 종이 흔들린다." },
		{ 4, "캄파나 종탑",        1226,  778, 6, "금 간 종이 매달린 종탑. 종지기의 백골이 아직도 밧줄을 쥐고 있다." },
		{ 5, "올하르 사원",         228, 1726, 7, "이끼 낀 돌기둥마다 옛 글자가 새겨져 있다. 올란이 하르벤을 섬기던 첫 사원이다." },
		{ 6, "칼둔 균열",          1737, 1735, 7, "갈라진 땅에서 붉은 빛이 샌다. 심장처럼 뛰는 소리가 들린다 — 훔쳐 온 박동이다." },
		{ 7, "이레린 샘",           227,  731, 6, "물 위에 달이 둘 떠 있다. 하나는 하늘의 달, 하나는 샘이 기억하는 달이다." },
		{ 8, "그락 야영지",        1272, 1268, 6, "꺼지지 않은 모닥불과 훔친 짐 꾸러미. 사라진 상인들의 짐이 여기 다 있었다." },
		{ 9, "오를리차호 난파선",   772, 1772, 6, "모래에 반쯤 묻힌 배. 뱃머리의 여신상이 아직도 먼 바다를 보고 있다." },
	};
	inline constexpr size_t kPoiCount = sizeof(kPois) / sizeof(kPois[0]);

	// ── 장(章) — 제목 카드 ─────────────────────────────────────────────────────
	struct Chapter
	{
		const char* title;
		const char* place;
		const char* line;         // 제목 아래 한 줄
	};
	inline constexpr Chapter kChapters[] =
	{
		{ "", "", "" },
		{ "늦어지는 새벽", "데니차",       "하르벤이 멈춘 뒤로, 새벽은 날마다 조금씩 늦게 왔다." },
		{ "속삭이는 숲",   "올벤 숲",      "나무들은 밤마다 같은 말을 되뇌었다. 돌려줘, 돌려줘." },
		{ "세 번의 종소리", "캄파나 폐허", "아무도 없는 종탑에서, 종은 세 번 울렸다." },
		{ "깨어진 서약",   "서약의 언덕",  "지키겠다고 맹세한 자들은, 죽어서도 칼을 놓지 못했다." },
		{ "심장의 불씨",   "칼둔 균열",    "마지막 불씨는 가장 깊은 아가리 속에서 타고 있었다." },
	};
	inline constexpr int kChapterCount = 5;

	// ── 표 ─────────────────────────────────────────────────────────────────
	using K = QuestKind;
	using O = ObjKind;
	using I = QuestIcon;
	using R = Rarity;
	namespace M = ItemTableData::Id;
	inline constexpr uint8_t kKnightOnly = ItemTableData::kMaskKnight;
	inline constexpr uint8_t kRuneOnly   = ItemTableData::kMaskRune;
	inline constexpr uint8_t kHunterOnly = ItemTableData::kMaskHunter;

	inline constexpr QuestRow kQuests[] =
	{
		// ════ 제1장 · 늦어지는 새벽 (데니차) ════════════════════════════
		{ 1, K::Main, 1, "촌장의 근심", 6, 7, 1, 0, 0,
		  "촌장 고란이 예배당의 사제 밀레나를 만나 보라고 했다.",
		  "어서 오게, 젊은이. 이백 년 동안 이 마을의 새벽은 늦은 적이 없었다네. 그런데 하르벤이 멈춘 그 밤부터… 날마다 조금씩 늦어져. 예배당의 밀레나를 찾아가 보게. 그 아이는 새벽 불꽃의 말을 듣는다네.",
		  "예배당은 광장 남서쪽이라네. 불이 꺼지지 않는 곳이지.",
		  "촌장님이 보내셨군요. …네, 저도 느껴요. 예배당의 불꽃이 요즘 숨을 쉬듯 흔들려요. 누군가 아주 먼 데서 부르는 것처럼요.",
		  {}, { 30, 0, 100, { M::HerbPotion, M::HerbPotion, M::HerbPotion }, 5 } },

		{ 2, K::Main, 1, "성문 밖의 기척", 7, 14, 1, 1, 0,
		  "성문 밖 들녘에서 날뛰는 수액덩이와 그락 약탈꾼을 쫓아낸다.",
		  "불꽃이 흔들리는 밤이면 성문 밖 짐승들도 잠들지 못해요. 남문의 페타르가 요즘 수액덩이와 그락 약탈꾼이 성벽까지 온다고 하더군요. 가서 좀 도와주시겠어요? 새벽이 함께할 거예요.",
		  "남문 밖 들녘이에요. 다치면 언제든 돌아오세요. 기도는 값을 받지 않으니까요.",
		  "허, 혼자서 이만큼이나? 덕분에 오늘 밤은 성문이 조용하겠소. …그런데 말이오, 녀석들이 무언가에 쫓기듯 달려들었소. 숲 쪽에서 뭔가가 내려오고 있소.",
		  { { O::Kill, 1, 6, 5 }, { O::Kill, 2, 4, 5 } },
		  { 100, 0, 200, { M::LeatherHood, M::LeatherHood, M::LeatherHood }, 1, R::Uncommon } },

		{ 3, K::Main, 1, "맨몸으로는 못 간다", 2, 2, 3, 2, 0,
		  "대장장이 도브란의 말대로 장비를 다섯 부위 이상 갖춰 입는다.",
		  "페타르한테 들었네. 성문 밖에서 제법 했다며? 허허. 하지만 그 차림으로 숲에 갔다간 늑대 밥이야. 무기, 갑옷, 장화에 투구 — 거기에 장갑이든 망토든 하나 더. 다섯 군데는 제대로 걸치고 오게. 모자라면 내 가게에서 사도 좋고.",
		  "장비창(I)에서 입을 수 있지. 다섯 부위, 잊지 말게.",
		  "이제야 모험가 태가 나는군. 이건 내가 쓰던 걸 고쳐 둔 걸세. 숲에서 살아 돌아오면 그때 제대로 된 걸 만들어 주지.",
		  { { O::Equip, 0, 5 } },
		  { 150, 0, 300, { M::LeatherGloves, M::LeatherGloves, M::LeatherGloves }, 1, R::Rare } },

		// ════ 제2장 · 속삭이는 숲 (올벤 숲) ═════════════════════════════════════════════════
		{ 4, K::Main, 2, "숲이 속삭인다", 12, 12, 5, 3, 0,
		  "마르파 할멈의 부탁으로 올벤 숲 어귀에 가서 숲의 소리를 듣는다.",
		  "아이고, 젊은이. 내가 어릴 적엔 숲이 노래를 했다우. 근데 요즘은… 속삭여. 밤마다 성벽 너머로 들려. 올벤 숲 어귀에 가서 무슨 소린지 좀 들어 봐 주겠수? 늑대들이 거칠어졌으니 조심하고.",
		  "길잡이 마테오한테 가면 숲까지 금방이우. 공짜는 아니지만.",
		  "…나무들이 '돌려줘'라고 했다고? 아이고… 그 말을 다시 듣게 될 줄이야. 숲이 뭘 잃어버린 게야.",
		  { { O::Explore, 1, 1 }, { O::Kill, 5, 6, 1 } },
		  { 300, 0, 500, { M::IronSword, M::OakStaff, M::BlackironDagger }, 1, R::Uncommon } },

		{ 5, K::Main, 2, "빛 잃은 이끼", 12, 12, 6, 4, 0,
		  "숲의 병을 알아보려고 빛 잃은 이끼를 모으고, 굶주린 불곰들을 쫓는다.",
		  "숲이 아프면 이끼가 먼저 빛을 잃는다우. 붉은갓버섯 등에 붙은 이끼를 좀 모아다 주겠수? 굶주린 불곰들이 사람 사는 데까지 내려오기 전에 좀 쫓아 주고.",
		  "붉은갓버섯 등을 잘 보우. 희미하게 빛나는 게 붙어 있을 게야.",
		  "봐, 봐. 이끼 빛이 이렇게 약해. 하르벤이… 정말로 멈춘 게야.",
		  { { O::Collect, 4, 6, 1, 45, "빛 잃은 이끼", I::Moss }, { O::Kill, 6, 4, 1 } },
		  { 400, 0, 600, { M::TravelerCloak, M::TravelerCloak, M::TravelerCloak }, 1, R::Rare } },

		{ 6, K::Main, 2, "멈춘 심장나무", 12, 9, 8, 5, 0,
		  "올벤 숲 한가운데의 하르벤을 보고 와서, 옛 노래를 아는 음유시인 보얀에게 이야기한다.",
		  "하르벤에게 가 보우. 숲 한가운데 제일 큰 나무 말이우. 가슴께에 옹이가 있는데, 옛날엔 거기서 새벽빛이 났지. 보고 와서 음유시인 보얀한테 이야기해 주우. 그 사람은 옛 노래를 다 외우고 있으니께.",
		  "하르벤은 숲 어귀에서 북서쪽이우.",
		  "♪ 종이 세 번 울리면, 심장나무는 잠드네… 옹이가 비어 있었다고? 거긴 노래 속 '사엔', 올란이 심었다는 새벽의 씨앗이 있던 자리요. 그리고 요즘 밤마다 들리는 종소리… 캄파나의 종이오. 노래는 늘 그 둘을 함께 부르지.",
		  { { O::Explore, 2, 1 } },
		  { 600, 0, 800, { M::BoneCharm, M::BoneCharm, M::BoneCharm }, 1, R::Rare } },

		// ════ 제3장 · 세 번의 종소리 (캄파나 폐허) ═══════════════════════════════
		{ 7, K::Main, 3, "세 번의 종소리", 9, 7, 12, 6, 0,
		  "밤마다 저절로 울리는 캄파나 대성당의 종을 조사한다.",
		  "노래가 맞다면, 누군가 종을 울려 하르벤을 재운 거요. 캄파나 폐허 한가운데 대성당이 있소. 백골 수비병들이 종탑을 지킨다더군. 돌아오면 밀레나 사제에게 들려주시오 — 그 종은 교회의 것이었으니.",
		  "캄파나 폐허는 멀고 험하오. 마테오가 데려다줄 거요.",
		  "종지기의 백골이 아직도 종을 친다고요…? 누군가 그들을 부리고 있어요. 새벽을 부르던 교회의 종이… 장례의 종을 치다니. 그리고 사엔이 옹이에 없었다면, 누군가 가져간 거예요.",
		  { { O::Explore, 3, 1 }, { O::Kill, 13, 10, 6 } },
		  { 1300, 0, 1500, { M::SilverRing, M::SilverRing, M::SilverRing }, 1, R::Rare } },

		{ 8, K::Main, 3, "호박빛 흔적", 7, 7, 14, 7, 0,
		  "망령들이 묻혀 온 호박빛 수액을 모아, 사엔이 어디로 옮겨졌는지 알아낸다.",
		  "사엔은 하르벤의 수액 속에서 자랐대요. 옮겨진 자리마다 호박빛 수액이 묻어 있을 거예요. 폐허의 망령들 몸에서 그 빛이 났다는 사람이 있어요. 폐허를 뒤지는 그락 창잡이들이 흔적을 밟아 버리기 전에 수액을 모아 오세요. 불꽃에 비춰 볼게요.",
		  "망령은 빛을 싫어해요. 그런데 수액 빛에는 이끌린대요. 이상하죠?",
		  "(수액을 불꽃에 비추자 연기가 남서쪽으로 흐른다) …서약의 언덕. 기사단이 묻힌 곳이에요. 그 아래 올하르 사원이 있어요. 그리고 수액에 이 문장이 찍혀 있어요… 서약 기사단의 문장이요.",
		  { { O::Collect, 18, 5, 6, 35, "호박빛 수액", I::Sap }, { O::Kill, 8, 8, 6 } },
		  { 1800, 0, 2000, { M::ChainMail, M::EngraverRobe, M::HunterLeather }, 1, R::Rare } },

		{ 9, K::Main, 3, "종을 멈춰라", 7, 6, 16, 8, 0,
		  "캄파나 종탑의 종을 멈춰 하르벤이 더는 잠들지 않게 하고, 촌장에게 알린다.",
		  "종이 계속 울리면 사엔을 되찾아도 소용없어요. 캄파나 종탑으로 가서 종을 멈춰 주세요. 망령이 된 종지기들이 막아설 거예요. 그다음엔 촌장님께 — 마을이 준비를 해야 해요.",
		  "종탑은 대성당 남서쪽이에요.",
		  "종이 멈췄다고? …그래, 오늘 새벽은 조금 일찍 왔다네. 자네 덕이야. 하지만 서약 기사단의 문장이라니… 백 년 전 올하르에서 사라진 기사단장의 이름을 다시 입에 올리게 될 줄은 몰랐네. 세베린.",
		  { { O::Explore, 4, 1 }, { O::Kill, 18, 10, 6 } },
		  { 2400, 0, 3000, { M::EngravingScroll, M::EngravingScroll, M::EngravingScroll }, 2 } },

		// ════ 제4장 · 깨어진 서약 (서약의 언덕 · 올하르 사원) ════════════════════
		{ 10, K::Main, 4, "기사들의 언덕", 6, 6, 20, 9, 0,
		  "서약의 언덕 깊은 곳, 올하르 사원으로 가는 길을 연다.",
		  "서약의 언덕은 백 년 전 기사들이 묻힌 곳이네. 그 아래 올하르 사원은 올란이 하르벤을 섬기던 첫 사원이지. 걷는 망자들이 길을 막고 있을 걸세. 길을 뚫고 사원을 확인해 주게.",
		  "서약의 언덕은 남서쪽 끝이라네. 마테오에게 부탁하게.",
		  "사원 기둥에 기사단 문장이 새겨져 있었다고? …기사들이 거기서 싸웠다는 이야기가 사실이었군. 그리고 아직도 싸우고 있다니.",
		  { { O::Explore, 5, 1 }, { O::Kill, 20, 12, 12 } },
		  { 3600, 0, 5000, { M::IronGauntlets, M::IronGauntlets, M::IronGauntlets }, 1, R::Heroic } },

		{ 11, K::Main, 4, "파약 기사", 6, 7, 22, 10, 0,
		  "서약을 잃고 떠도는 파약 기사들을 쓰러뜨리고, 녹슨 문장을 거둬 예배당에서 안식을 빈다.",
		  "그 기사들은 이 마을을 지키겠다고 맹세한 이들이네. 누군가 그 서약을 깨뜨렸고, 그들은 파약의 사슬에 묶여 아직도 칼을 휘두르지. 그들을 쉬게 해 주게. 문장을 거둬 밀레나에게 가져가면 안식의 기도를 올려 줄 걸세.",
		  "파약 기사는 단단하다네. 자세를 무너뜨린 뒤에 치게.",
		  "(문장을 제단에 올리자 불꽃이 잔잔해진다) 편히 잠드소서… 문장들이 말해요. 서약을 깬 건 기사들이 아니었어요. 기사단장 세베린 — 그가 칼둔의 것에게 무릎을 꿇었대요. 사엔을 삼키려 했지만, 사엔이 그를 거부했대요.",
		  { { O::Kill, 21, 8, 12 }, { O::Collect, 21, 6, 12, 40, "녹슨 기사단 문장", I::Crest } },
		  { 4400, 0, 6000, { M::SilverPendant, M::SilverPendant, M::SilverPendant }, 1, R::Heroic } },

		{ 12, K::Main, 4, "몰락한 기사단장", 7, 6, 24, 11, 0,
		  "올하르 사원의 주인이 된 세베린을 쓰러뜨리고 사엔을 되찾는다.",
		  "사엔은 세베린 곁에 있어요. 혼자서는 무리예요 — 다른 모험가들과 함께 가세요. 그가 쓰러질 때 곁에 있기만 해도, 사엔이 당신을 알아볼 거예요.",
		  "세베린의 칼은 땅을 가른대요. 바닥의 붉은 빛을 조심하세요.",
		  "이것이… 사엔인가. (차갑다.) 빛이 없군. 껍데기만 남았어. 불씨가… 불씨는 어디로 간 겐가?",
		  { { O::Boss, MonsterTable::Id::Severin, 1, 12 } },
		  { 8000, 0, 15000, { M::SeverinBlade, M::EmberStaff, M::VenomFang }, 1, R::Heroic } },

		// ════ 제5장 · 심장의 불씨 (칼둔 균열 → 하르벤) ═══════════════════════════════════════════════════
		{ 13, K::Main, 5, "꺼진 불씨", 6, 3, 26, 12, 0,
		  "사엔의 불씨가 어디로 흘러갔는지, 옛길을 읽는 길잡이 마테오에게 묻는다.",
		  "마테오는 옛길이 흐르는 방향을 읽는다네. 사엔의 불씨가 어디로 흘러갔는지 그라면 알 걸세.",
		  "마테오는 광장 한가운데 있다네.",
		  "(사엔 위로 손을 펼치자 발밑의 각인이 붉게 탄다) …옛길이 흔들렸던 이유를 알겠어요. 불씨는 옛길을 거슬러 칼둔 균열로 끌려갔어요. 누군가, 아니 무언가가 그걸 먹고 있어요.",
		  {}, { 2500, 0, 3000, { M::SapElixir, M::SapElixir, M::SapElixir }, 10 } },

		{ 14, K::Main, 5, "칼둔 균열", 3, 3, 28, 13, 0,
		  "칼둔 균열에 들어가 칼둔 전사들을 물리치고, 사엔에서 흘러나간 불씨 조각을 되찾는다.",
		  "칼둔 균열까지 옛길을 열어 드릴 수는 있어요. 돌아오는 길은… 당신 몫이에요. 칼둔 사냥개들이 불씨 조각을 물고 다닌대요. 세 조각이면 사엔이 다시 숨을 쉴 거예요.",
		  "균열에선 오래 머물지 마세요. 물약을 넉넉히.",
		  "(불씨 조각이 사엔 속으로 스며든다) 됐어요, 사엔이 다시 따뜻해요. 그런데… 가장 큰 불씨가 아직 저 안에 있어요. 그라목. 균열의 주인이 그걸 삼켰어요.",
		  { { O::Explore, 6, 1 }, { O::Kill, 28, 10, 15 }, { O::Collect, 27, 3, 15, 30, "꺼지지 않는 불씨", I::Ember } },
		  { 7000, 0, 12000, { M::ConsecratedScroll, M::ConsecratedScroll, M::ConsecratedScroll }, 1 } },

		{ 15, K::Main, 5, "삼키는 주인", 3, 6, 30, 14, 0,
		  "칼둔 균열의 주인 그라목을 쓰러뜨리고 마지막 불씨를 되찾는다.",
		  "그라목은 혼자 상대할 수 있는 존재가 아니에요. 모든 모험가를 불러 모으세요. 그가 쓰러질 때 곁에만 있어도, 불씨는 사엔을 가진 당신에게 돌아올 거예요.",
		  "균열 한가운데예요. 새벽이 함께하기를.",
		  "(사엔이 새벽빛으로 타오른다) 오오… 이백 년 동안 날마다 보던 빛인데, 이렇게 반가울 줄이야. 가게, 젊은이. 하르벤에게. 이 마을의 새벽을 돌려주게.",
		  { { O::Boss, MonsterTable::Id::Gramok, 1, 15 } },
		  { 14000, 0, 30000, { M::OathPlate, M::IrerinRobe, M::SilverwolfCloak }, 1, R::Heroic } },

		{ 16, K::Main, 5, "다시 뛰는 심장", 6, 0, 30, 15, 0,
		  "사엔을 하르벤의 옹이에 돌려놓는다.",
		  "사엔을 옹이에 돌려놓게. 나머지는… 숲이 알 걸세.",
		  "올벤 숲 한가운데, 하르벤. 자네는 이미 가 본 곳이지.",
		  "사엔을 옹이에 대자, 거목 깊은 곳에서 무언가가 뛰었다. 쿵. …쿵. 이백 년을 이어 온 박동이 다시 숲을 채운다. 속삭임은 노래가 되고, 긴 밤 끝에 — 새벽이 왔다.",
		  { { O::Explore, 2, 1 } },
		  { 20000, 0, 50000, { M::Dawnkeeper, M::HarvenBranch, M::Nightfang }, 1, R::Legendary } },

		// ════ 부가 이야기 ══════════════════════════════════════════════════════════
		{ 30, K::Side, 0, "누렁이는 어디에?", 10, 10, 1, 0, 0,
		  "꼬마 미샤의 개 누렁이를 찾아 쓰다듬어 준다.",
		  "형! 누렁이 못 봤어? 또 어디 갔나 봐… 누렁이는 쓰다듬어 주면 좋아서 꼬리를 막 흔들어. 찾으면 한 번만 쓰다듬어 줘!",
		  "누렁이는 광장이랑 분수 근처를 막 뛰어다녀!",
		  "진짜? 누렁이가 꼬리 흔들었어? 헤헤, 형 좋은 사람인가 봐. 이거 줄게. 엄마가 아껴 둔 건데… 비밀이야!",
		  { { O::Talk, 17, 1, kAnyZone, 100, "누렁이 쓰다듬어 주기" } },
		  { 20, 0, 50, { M::WindstepPotion, M::WindstepPotion, M::WindstepPotion }, 1 } },

		{ 31, K::Side, 0, "허수아비 도둑", 13, 13, 2, 0, 0,
		  "허수아비를 뜯어 간 그락 약탈꾼들에게서 허수아비 조각을 되찾는다.",
		  "그락 녀석들이 내 허수아비를 뜯어 갔소. 옷가지며 밀짚이며… 대체 그걸로 뭘 하려는지. 조각을 좀 찾아다 주시오. 올해 농사가 걸렸소!",
		  "들녘의 그락 약탈꾼들이오. 밀짚 냄새가 나는 녀석들!",
		  "허허, 이걸로 다시 세울 수 있겠소. 고맙소! 닭들도 고맙다고 할 거요. …아마도.",
		  { { O::Kill, 2, 6, 5 }, { O::Collect, 2, 4, 5, 50, "허수아비 조각", I::Straw } },
		  { 120, 0, 250, { M::LeatherBoots, M::LeatherBoots, M::LeatherBoots }, 1, R::Uncommon } },

		{ 32, K::Side, 0, "여관의 저녁거리", 8, 8, 7, 0, 0,
		  "여관 주인 베스나의 스튜에 넣을 곰 고기를 구해 온다.",
		  "요즘 손님들이 전부 숲 얘기만 하면서 밥은 두 그릇씩 먹어요! 곰 고기가 다 떨어졌지 뭐예요. 곰 고기 좀 구해다 줄래요? 스튜 끓여 둘게요!",
		  "불곰은 올벤 숲이나 이레린 숲에 있어요. 크니까 금방 보일 거예요!",
		  "어머, 싱싱하기도 해라! 오늘 스튜는 특별 메뉴예요. 이건 수고비, 물약은 제 선물이고요!",
		  { { O::Collect, 6, 5, kAnyZone, 55, "곰 고기", I::Meat } },
		  { 500, 0, 700, { M::StrongHerbPotion, M::StrongHerbPotion, M::StrongHerbPotion }, 10 } },

		{ 33, K::Side, 0, "반짝반짝 정령의 빛", 11, 11, 6, 0, 0,
		  "꼬마 안카에게 이레린 숲 잎새 정령들이 흘리는 빛을 모아다 준다.",
		  "할머니가 그러는데, 이레린 숲에는 반짝반짝 빛나는 정령이 산대! 정령의 빛을 병에 담으면 밤에도 안 무섭대. 나 다섯 개만 갖다 줘, 응?",
		  "이레린 숲은 서쪽이래! 마테오 아저씨가 보내 준대!",
		  "우와아… 진짜 반짝반짝해! 이제 밤이 하나도 안 무서워! 이거, 내 보물이야. 가져!",
		  { { O::Collect, 11, 5, 4, 45, "정령의 빛", I::SpiritLight } },
		  { 400, 0, 400, { M::CopperRing, M::CopperRing, M::CopperRing }, 1, R::Rare } },

		{ 34, K::Side, 0, "성벽 밖의 불빛", 16, 16, 10, 0, 0,
		  "순찰병 일리야가 본 수상한 불빛 — 그락 야영지를 찾아 녀석들을 흩어 놓는다.",
		  "밤마다 성벽 밖 먼 데서 불빛이 보였소. 알아보니 그락 창잡이들이 아스린 사막에 야영지를 차렸더군. 마을로 오는 상인들을 노리는 거요. 야영지를 찾아서 녀석들을 흩어 주시오.",
		  "아스린 사막 한가운데요. 모래전갈도 조심하시오.",
		  "야영지를 흩었다고? 잘했소! 이제 상인들이 안심하고 다니겠군. 이건 경비대 창고에서 꺼내 온 거요.",
		  { { O::Explore, 8, 1 }, { O::Kill, 8, 10 } },
		  { 700, 0, 1500, { M::IronHelm, M::IronHelm, M::IronHelm }, 1, R::Rare } },

		{ 35, K::Side, 0, "음유시인의 새 노래", 9, 9, 12, 6, 0,
		  "새 노래의 영감을 찾는 음유시인 보얀을 위해 오를리차호 난파선과 이레린 샘을 보고 온다.",
		  "노래가 바닥났소. 새 이야기가 필요하오! 베레그 해안에 반쯤 묻힌 오를리차호가 있다던데, 이레린 샘은 달을 둘 비춘다더군. 두 곳을 보고 와서 이야기해 주시오. 당신 이름을 노래에 넣어 드리리다.",
		  "난파선은 남쪽 베레그 해안, 샘은 서쪽 이레린 숲이오.",
		  "♪ 파도에 잠긴 배, 달을 품은 샘… 그 사이를 걸은 모험가여! …어떻소? 괜찮지 않소? 이건 노래값이오.",
		  { { O::Explore, 9, 1 }, { O::Explore, 7, 1 } },
		  { 900, 0, 1200, { M::NightCloak, M::NightCloak, M::NightCloak }, 1, R::Rare } },

		// ════ 반복 의뢰(하루 한 번) ═══════════════════════════════════════════════════
		{ 60, K::Daily, 0, "경비대 사냥 의뢰", 15, 15, 1, 0, 0,
		  "(반복) 데니차 들녘의 짐승을 정리한다.",
		  "오늘도 들녘이 시끄럽소. 열다섯 마리만 정리해 주면 경비대에서 사례하리다. 매일 부탁할 거요.",
		  "데니차 들녘에 있는 녀석이면 무엇이든 좋소.",
		  "수고했소. 내일도 부탁하오.",
		  { { O::KillIn, 0, 15, 5 } },
		  { 0, 20, 300, { M::HerbPotion, M::HerbPotion, M::HerbPotion }, 5 }, 20 * 60 },

		{ 61, K::Daily, 0, "숲 달래기", 12, 12, 6, 4, 0,
		  "(반복) 올벤 숲의 거칠어진 짐승들을 잠재운다.",
		  "숲이 앓는 동안엔 짐승들도 앓는다우. 스무 마리만 잠재워 주우. 날마다 부탁하는 게 미안하구먼.",
		  "올벤 숲이면 아무 짐승이나 괜찮수.",
		  "고생했수. 숲이 조금은 조용해졌을 게야.",
		  { { O::KillIn, 0, 20, 1 } },
		  { 0, 20, 800, { M::StrongHerbPotion, M::StrongHerbPotion, M::StrongHerbPotion }, 5 }, 20 * 60 },

		{ 62, K::Daily, 0, "언덕의 안식", 7, 7, 20, 10, 0,
		  "(반복) 서약의 언덕에서 잠들지 못한 자들에게 안식을 준다.",
		  "서약의 언덕은 오늘도 소란해요. 스물다섯 영혼만 쉬게 해 주세요. 매일 기도할게요.",
		  "서약의 언덕에서요. 새벽이 함께하기를.",
		  "고마워요. 오늘 밤은 조금 더 조용할 거예요.",
		  { { O::KillIn, 0, 25, 12 } },
		  { 0, 20, 3000, { M::EngravingScroll, M::EngravingScroll, M::EngravingScroll }, 1 }, 20 * 60 },

		{ 63, K::Daily, 0, "모로즈 토벌", 5, 5, 18, 0, 0,
		  "(반복) 벨고라 설원의 모로즈를 쓰러뜨린다.",
		  "모로즈가 또 깨어났다는 소식이오. 녀석이 설원을 넘기 전에 막아야 하오. 혼자선 무리니 동료를 모으시오.",
		  "북쪽 벨고라 설원이오. 몸을 따뜻하게 하시오.",
		  "모로즈를 쓰러뜨렸다니! 경비대가 한턱내겠소.",
		  { { O::Boss, MonsterTable::Id::Moroz, 1, 0 } },
		  { 0, 35, 8000, { M::ConsecratedScroll, M::ConsecratedScroll, M::ConsecratedScroll }, 1 }, 20 * 60 },

		// ════ 직업의 길(Lv.12) ══════════════════════════════════════════════════════
		{ 80, K::Class, 0, "방패의 서약", 4, 4, 12, 0, kKnightOnly,
		  "서약기사의 길 — 숲의 짐승 무리를 방패로 받아 낸다.",
		  "서약기사가 되려는 자는 먼저 무리를 막아서는 법을 배워야 하오. 올벤 숲의 늑대와 곰 — 그 무리를 방패로 받아 내시오. 도망치지 말고. 철벽 태세로 버티다 되받아치는 거요.",
		  "올벤 숲이오. 등 뒤의 마을을 생각하시오.",
		  "좋소. 이제 당신 뒤에 선 사람들은 안심할 수 있겠군. 이 검은 내가 젊을 적 쓰던 것이오.",
		  { { O::Kill, 5, 8, 1 }, { O::Kill, 6, 6, 1 } },
		  { 900, 0, 1500, { M::SteelSword, M::SteelSword, M::SteelSword }, 1, R::Heroic } },

		{ 81, K::Class, 0, "옛 글자의 목소리", 3, 3, 12, 0, kRuneOnly,
		  "각인술사의 길 — 이슬덩이가 삼킨 각인 조약돌을 모은다.",
		  "각인술사라면 옛 글자의 목소리를 들어야지요. 이슬덩이들이 가끔 각인이 새겨진 조약돌을 삼켜요. 여섯 개를 모아 오세요. 그 글자들이 당신 지팡이에 깃들 거예요.",
		  "이슬덩이는 이레린 숲과 베레그 해안에 많아요.",
		  "(조약돌을 지팡이에 대자 푸른 불이 번진다) 들리지요? 이제 이 지팡이는 당신 목소리로 말할 거예요.",
		  { { O::Collect, 7, 6, kAnyZone, 40, "각인 조약돌", I::CarvedStone } },
		  { 900, 0, 1500, { M::EngravedStaff, M::EngravedStaff, M::EngravedStaff }, 1, R::Heroic } },

		{ 82, K::Class, 0, "밤사냥꾼의 시험", 15, 15, 12, 0, kHunterOnly,
		  "밤사냥꾼의 길 — 등 뒤를 보이지 않는 모래전갈을 사냥한다.",
		  "사냥꾼은 보이지 않는 곳에서 친다고들 하지. 아스린 사막의 전갈은 등 뒤를 잘 내주지 않소. 열 마리. 그늘 걸음으로 등 뒤를 잡아 보시오.",
		  "아스린 사막이오. 독을 조심하시오.",
		  "전갈 열 마리를… 소리도 없이? 당신은 진짜 밤사냥꾼이오. 이 단검이 달빛을 머금는 걸 보게 될 거요.",
		  { { O::Kill, 9, 10, 10 } },
		  { 900, 0, 1500, { M::MoonlightDagger, M::MoonlightDagger, M::MoonlightDagger }, 1, R::Heroic } },
	};
	inline constexpr size_t kQuestCount = sizeof(kQuests) / sizeof(kQuests[0]);

	// ── 조회 ────────────────────────────────────────────────────────────────
	[[nodiscard]] constexpr const QuestRow* Find(uint16_t id) noexcept
	{
		for (const auto& q : kQuests)
			if (q.id == id)
				return &q;
		return nullptr;
	}

	[[nodiscard]] constexpr const Poi* FindPoi(uint16_t id) noexcept
	{
		for (const auto& p : kPois)
			if (p.id == id)
				return &p;
		return nullptr;
	}

	[[nodiscard]] constexpr int ObjCount(const QuestRow& q) noexcept
	{
		int n = 0;
		for (const auto& o : q.objs)
			if (o.kind != ObjKind::None) ++n;
		return n;
	}

	[[nodiscard]] constexpr bool IsRepeatable(const QuestRow& q) noexcept { return q.cooldownMin > 0; }

	[[nodiscard]] constexpr bool ClassCanTake(const QuestRow& q, ClassId cls) noexcept
	{
		return q.classMask == 0 || (q.classMask & (1u << static_cast<unsigned>(cls))) != 0;
	}

	// 모든 목표를 이뤘나(목표가 없으면 — 전하기 퀘스트 — 받자마자 보고할 수 있다)
	[[nodiscard]] constexpr bool ObjectivesDone(const QuestRow& q, const uint16_t* progress) noexcept
	{
		for (int i = 0; i < kMaxObjs; ++i)
			if (q.objs[i].kind != ObjKind::None && progress[i] < q.objs[i].count)
				return false;
		return true;
	}

	// 직업에 맞는 보상 아이템(없으면 0)
	[[nodiscard]] constexpr uint16_t RewardItem(const QuestRow& q, ClassId cls) noexcept
	{
		const int c = static_cast<int>(cls);
		return (c >= 0 && c < 3) ? q.reward.item[c] : q.reward.item[0];
	}

	// 받을 경험치(반복 퀘스트는 지금 레벨에 맞춰)
	[[nodiscard]] constexpr uint32_t RewardExp(const QuestRow& q, uint8_t level) noexcept
	{
		if (q.reward.expPct > 0)
			return LevelTable::ExpToNext(level) * q.reward.expPct / 100u;
		return q.reward.exp;
	}

	[[nodiscard]] constexpr bool InPoi(const Poi& p, int x, int y) noexcept
	{
		const int dx = x - p.x, dy = y - p.y;
		return (dx < 0 ? -dx : dx) <= p.radius && (dy < 0 ? -dy : dy) <= p.radius;
	}

	// 완료 기록(1~255 비트)
	[[nodiscard]] constexpr bool TestBit(const uint8_t* bits, uint16_t id) noexcept
	{
		return id > 0 && id < 256 && (bits[id >> 3] & (1u << (id & 7))) != 0;
	}

	// 메인 이야기에서 지금 "다음" 퀘스트(이미 받았거나 끝난 것은 건너뛴다). 없으면 nullptr.
	template <typename IsDoneFn>
	[[nodiscard]] constexpr const QuestRow* NextMain(IsDoneFn&& isDone) noexcept
	{
		for (const auto& q : kQuests)
			if (q.kind == QuestKind::Main && !isDone(q.id))
				return &q;
		return nullptr;
	}

	// ── 컴파일 타임 검증 ──────────────────────────────────────────────────────
	namespace Detail
	{
		constexpr bool ValidPoi(const Poi& p) noexcept
		{
			return p.radius > 0 && !WorldData::IsBlocked(p.x, p.y) && !WorldData::IsInTown(p.x, p.y);
		}

		constexpr bool ValidObjective(const Objective& o) noexcept
		{
			if (o.zone != kAnyZone && o.zone >= WorldData::kRegionCount) return false;
			switch (o.kind)
			{
			case ObjKind::None: return o.count == 0;
			case ObjKind::Kill: return o.count > 0 && MonsterTable::Find(o.target) != nullptr;
			case ObjKind::KillIn: return o.count > 0 && o.zone != kAnyZone;
			case ObjKind::Collect: return o.count > 0 && MonsterTable::Find(o.target) != nullptr && o.chance > 0 && o.chance <= 100 && o.label != nullptr && o.icon != QuestIcon::None;
			case ObjKind::Talk: return o.count == 1 && WorldData::FindNpc(o.target) != nullptr;
			case ObjKind::Explore: return o.count == 1 && FindPoi(o.target) != nullptr;
			case ObjKind::Boss: return o.count == 1 && MonsterTable::Find(o.target) != nullptr && MonsterTable::Find(o.target)->boss;
			case ObjKind::Equip: return o.count > 0 && o.count <= 8;
			}
			return false;
		}

		constexpr bool Validate() noexcept
		{
			for (size_t i = 0; i < kPoiCount; ++i)
			{
				if (!ValidPoi(kPois[i])) return false;
				for (size_t j = i + 1; j < kPoiCount; ++j)
					if (kPois[i].id == kPois[j].id) return false;
			}
			for (size_t i = 0; i < kQuestCount; ++i)
			{
				const QuestRow& q = kQuests[i];
				if (q.id == 0 || q.id > 255) return false;
				for (size_t j = i + 1; j < kQuestCount; ++j)
					if (kQuests[j].id == q.id) return false;
				if (WorldData::FindNpc(q.giver) == nullptr) return false;
				if (q.turnIn != 0 && WorldData::FindNpc(q.turnIn) == nullptr) return false;
				if (q.turnIn == 0 && ObjCount(q) == 0) return false;          // 보고할 곳도 목표도 없으면 끝낼 수 없다
				if (q.prereq != 0 && (q.prereq == q.id || Find(q.prereq) == nullptr)) return false;
				if (q.minLevel < 1 || q.minLevel > LevelTable::kMaxLevel) return false;
				if ((q.kind == QuestKind::Main) != (q.chapter != 0) || q.chapter > kChapterCount) return false;
				if ((q.kind == QuestKind::Daily) != (q.cooldownMin > 0)) return false;
				for (const auto& o : q.objs)
					if (!ValidObjective(o)) return false;
				for (uint16_t item : q.reward.item)
					if (item != 0 && ItemTableData::Find(item) == nullptr) return false;
				if (q.reward.count == 0) return false;
			}
			// 메인 이야기는 id 순서대로 이어진다(앞 퀘스트가 곧 선행 퀘스트)
			uint16_t prevMain = 0;
			for (const auto& q : kQuests)
			{
				if (q.kind != QuestKind::Main) continue;
				if (q.prereq != prevMain) return false;
				prevMain = q.id;
			}
			bool finalOk = false;
			for (const auto& q : kQuests)
				if (q.id == kFinalQuest) finalOk = q.kind == QuestKind::Main && q.turnIn == 0;
			return finalOk;
		}
	}
	static_assert(Detail::Validate(), "QuestData: 퀘스트/장소 표가 잘못되었습니다(id·NPC·목표·보상·선행 퀘스트·장소 좌표를 확인하세요).");
}
