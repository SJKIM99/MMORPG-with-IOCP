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
// 이야기의 뼈대: "백 년을 뛰던 숲의 심장이 멈췄다."
//   제1장 새벽이 오지 않는 밤(여명의 마을) → 제2장 속삭이는 숲 → 제3장 울리는 종(폐성당)
//   → 제4장 잠들지 못한 자들(고대 유적) → 제5장 숲의 심장
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
	enum class QuestIcon : uint8_t { None, Moss, Straw, Meat, SpiritLight, RelicShard, Crest, Ember, RuneStone, Count };

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
		uint32_t adena   = 0;
		uint16_t item[3] = {};        // 직업별(기사 · 룬술사 · 그림자 사냥꾼). 0 = 없음
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
		{ 1, "속삭이는 숲 어귀",      752,  272, 6, "나뭇잎이 바람도 없이 흔들린다. 귀를 기울이면… 누군가 '돌려줘'라고 속삭인다." },
		{ 2, "숲의 심장 · 잠든 거목", 716,  226, 6, "하늘을 가릴 만큼 큰 나무가 숨을 멈춘 채 서 있다. 가슴께의 옹이가 텅 비어 있다." },
		{ 3, "폐성당",               1267,  729, 7, "지붕이 무너진 성당. 제단은 비어 있고, 바람도 없는데 종탑의 종이 흔들린다." },
		{ 4, "무너진 종탑",          1226,  778, 6, "금 간 종이 매달린 종탑. 종지기의 해골이 아직도 밧줄을 쥐고 있다." },
		{ 5, "고대 유적",             228, 1726, 7, "이끼 낀 돌기둥 사이로 오래된 문장이 보인다. 심장나무를 새긴, 첫 사원의 흔적이다." },
		{ 6, "심연의 문",            1737, 1735, 7, "갈라진 땅에서 붉은 빛이 샌다. 무언가 심장처럼 뛰는 소리 — 훔쳐 온 박동이다." },
		{ 7, "요정의 샘",             227,  731, 6, "물 위에 달이 둘 떠 있다. 하나는 하늘의 달, 하나는 샘이 기억하는 달이다." },
		{ 8, "코볼트 야영지",        1272, 1268, 6, "꺼지지 않은 모닥불과 훔친 짐 꾸러미. 상인들의 짐이 여기 다 있었다." },
		{ 9, "파도 해안의 난파선",    772, 1772, 6, "모래에 반쯤 묻힌 배. 뱃머리의 여신상이 아직도 먼 바다를 보고 있다." },
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
		{ "새벽이 오지 않는 밤", "여명의 마을", "백 년을 뛰던 숲의 심장이 멈춘 날, 새벽은 길을 잃었다." },
		{ "속삭이는 숲",         "속삭이는 숲", "나무들은 밤마다 같은 말을 되뇌었다. 돌려줘, 돌려줘." },
		{ "울리는 종",           "폐성당",      "아무도 없는 종탑에서, 종은 세 번 울렸다." },
		{ "잠들지 못한 자들",    "고대 유적",   "지키던 것을 잃은 기사들은 죽어서도 칼을 놓지 못했다." },
		{ "숲의 심장",           "심연의 문 너머", "마지막 불씨는 가장 깊은 어둠 속에서 타고 있었다." },
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
		// ════ 제1장 · 새벽이 오지 않는 밤 (여명의 마을) ════════════════════════════
		{ 1, K::Main, 1, "촌장의 근심", 6, 7, 1, 0, 0,
		  "촌장 알데릭이 신전의 사제 세라핀을 만나 보라고 했다.",
		  "어서 오게, 젊은이. 이 마을엔 백 년 동안 새벽이 늦은 적이 없었다네. 그런데 숲의 심장이 멈춘 그날부터… 밤이 끝나질 않아. 신전의 세라핀을 찾아가 보게. 그 아이는 불꽃의 말을 듣는다네.",
		  "신전은 광장 남서쪽이라네. 불이 꺼지지 않는 곳이지.",
		  "촌장님이 보내셨군요. …네, 저도 느껴요. 신전의 불꽃이 요즘 숨을 쉬듯 흔들려요. 누군가 아주 먼 데서 부르는 것처럼요.",
		  {}, { 30, 0, 100, { M::HealthPotion, M::HealthPotion, M::HealthPotion }, 5 } },

		{ 2, K::Main, 1, "성문 밖의 기척", 7, 14, 1, 1, 0,
		  "성문 밖 평원에서 날뛰는 젤리와 고블린을 쫓아낸다.",
		  "불꽃이 흔들리는 밤이면 성문 밖 짐승들도 잠들지 못해요. 남문 경비병이 요즘 젤리와 고블린이 성벽까지 온다고 하더군요. 가서 좀 도와주시겠어요? 빛이 함께할 거예요.",
		  "남쪽 성문 밖 평원이에요. 다치면 언제든 돌아오세요 — 기도는 공짜니까요.",
		  "허, 혼자서 이만큼이나? 덕분에 오늘 밤은 성문이 조용하겠소. …그런데 말이오, 녀석들이 무언가에 쫓기듯 달려들었소. 숲 쪽에서 뭔가가 내려오고 있소.",
		  { { O::Kill, 1, 6, 5 }, { O::Kill, 2, 4, 5 } },
		  { 100, 0, 200, { M::LeatherHood, M::LeatherHood, M::LeatherHood }, 1, R::Uncommon } },

		{ 3, K::Main, 1, "맨몸으로는 못 간다", 2, 2, 3, 2, 0,
		  "대장장이 브론의 말대로 장비를 다섯 부위 이상 갖춰 입는다.",
		  "경비병한테 들었네. 성문 밖에서 제법 했다며? 허허. 하지만 그 차림으로 숲에 갔다간 늑대 밥이야. 무기, 갑옷, 장화에 투구 — 거기에 장갑이든 망토든 하나 더. 다섯 군데는 제대로 걸치고 오게. 모자라면 내 가게에서 사도 좋고.",
		  "장비창(I)에서 입을 수 있지. 다섯 부위, 잊지 말게.",
		  "이제야 모험가 태가 나는군. 이건 내가 쓰던 걸 고쳐 둔 걸세. 숲에서 살아 돌아오면 그때 제대로 된 걸 만들어 주지.",
		  { { O::Equip, 0, 5 } },
		  { 150, 0, 300, { M::LeatherGloves, M::LeatherGloves, M::LeatherGloves }, 1, R::Rare } },

		// ════ 제2장 · 속삭이는 숲 ═════════════════════════════════════════════════
		{ 4, K::Main, 2, "숲이 속삭인다", 12, 12, 5, 3, 0,
		  "오르다 할멈의 부탁으로 속삭이는 숲에 들어가 숲의 소리를 듣는다.",
		  "아이고, 젊은이. 내가 어릴 적엔 숲이 노래를 했다우. 근데 요즘은… 속삭여. 밤마다 성벽 너머로 들려. 숲 어귀에 가서 무슨 소린지 좀 들어 봐 주겠수? 늑대들이 거칠어졌으니 조심하고.",
		  "텔레포터 루시엔한테 가면 숲까지 금방이우. 공짜는 아니지만.",
		  "…나무들이 '돌려줘'라고 했다고? 아이고… 그 말을 다시 듣게 될 줄이야. 숲이 뭘 잃어버린 게야.",
		  { { O::Explore, 1, 1 }, { O::Kill, 5, 6, 1 } },
		  { 300, 0, 500, { M::IronSword, M::OakStaff, M::ShadowDagger }, 1, R::Uncommon } },

		{ 5, K::Main, 2, "빛을 잃은 이끼", 12, 12, 6, 4, 0,
		  "숲의 병을 알아보려고 빛나는 이끼를 모으고, 굶주린 곰들을 쫓는다.",
		  "숲이 아프면 이끼가 먼저 빛을 잃는다우. 버섯돌이 등에 붙은 빛나는 이끼를 좀 모아다 주겠수? 굶주린 곰들이 사람 사는 데까지 내려오기 전에 좀 쫓아 주고.",
		  "버섯돌이 등을 잘 보우. 반짝이는 게 붙어 있을 게야.",
		  "봐, 봐. 이끼 빛이 이렇게 약해. 숲의 심장이… 정말로 멈춘 게야.",
		  { { O::Collect, 4, 6, 1, 45, "빛나는 이끼", I::Moss }, { O::Kill, 6, 4, 1 } },
		  { 400, 0, 600, { M::TravelerCloak, M::TravelerCloak, M::TravelerCloak }, 1, R::Rare } },

		{ 6, K::Main, 2, "잠든 거목", 12, 9, 8, 5, 0,
		  "숲 한가운데의 잠든 거목을 보고 와서, 옛 노래를 아는 음유시인 리안에게 이야기한다.",
		  "거목에게 가 보우. 숲 한가운데 제일 큰 나무 말이우. 가슴께에 옹이가 있는데, 옛날엔 거기서 빛이 났지. 보고 와서 음유시인 리안한테 이야기해 주우. 그 사람은 옛 노래를 다 외우고 있으니께.",
		  "거목은 숲 어귀에서 북서쪽이우.",
		  "♪ 종이 세 번 울리면, 심장나무는 잠드네… 옹이가 비어 있었다고요? 거긴 노래 속 '새벽의 성물'이 있던 자리요. 성물은 폐성당에 모셔졌다고 노래는 말하오. 그리고 요즘 밤마다 들리는 종소리… 폐성당의 종이오.",
		  { { O::Explore, 2, 1 } },
		  { 600, 0, 800, { M::BoneCharm, M::BoneCharm, M::BoneCharm }, 1, R::Rare } },

		// ════ 제3장 · 울리는 종 (폐성당 — 황혼의 폐허) ═══════════════════════════════
		{ 7, K::Main, 3, "울리는 종", 9, 7, 12, 6, 0,
		  "밤마다 저절로 울리는 폐성당의 종을 조사한다.",
		  "노래가 맞다면, 누군가 종을 울려 심장나무를 재운 거요. 황혼의 폐허 한가운데 폐성당이 있소. 해골 병사들이 종탑을 지킨다더군. 돌아오면 세라핀 사제에게 들려주시오 — 성물은 원래 신전의 것이었으니.",
		  "황혼의 폐허는 멀고 험하오. 루시엔이 데려다줄 거요.",
		  "종지기의 해골이 아직도 종을 친다고요…? 누군가 그들을 부리고 있어요. 그리고 성물이 제단에 없었다면… 누군가 가져간 거예요.",
		  { { O::Explore, 3, 1 }, { O::Kill, 13, 10, 6 } },
		  { 1300, 0, 1500, { M::SilverRing, M::SilverRing, M::SilverRing }, 1, R::Rare } },

		{ 8, K::Main, 3, "성물의 행방", 7, 7, 14, 7, 0,
		  "망령들이 흩뜨린 성물 조각을 모아, 성물이 어디로 갔는지 알아낸다.",
		  "성물이 부서질 때 조각이 흩어졌을 거예요. 폐허의 망령들은 반짝이는 걸 좋아한다죠. 조각을 모아 오시면 불꽃에 비춰 볼게요. 성물이 어디로 갔는지 불꽃이 알려 줄 거예요.",
		  "망령은 빛을 싫어해요. 그래도 반짝이는 건 좋아하죠. 이상하죠?",
		  "(조각을 불꽃에 비추자 푸른 연기가 남서쪽으로 흐른다) …망자의 묘지. 성물은 망자의 묘지 깊은 곳, 고대 유적에 있어요. 그리고 그걸 지키는 건… 데스나이트예요.",
		  { { O::Collect, 18, 5, 6, 35, "성물 조각", I::RelicShard }, { O::Kill, 8, 8, 6 } },
		  { 1800, 0, 2000, { M::ChainMail, M::MageRobe, M::ShadowLeather }, 1, R::Rare } },

		{ 9, K::Main, 3, "무너진 종탑", 7, 6, 16, 8, 0,
		  "무너진 종탑의 종을 멈춰 심장나무가 더는 잠들지 않게 하고, 촌장에게 알린다.",
		  "종이 계속 울리면 성물을 되찾아도 소용없어요. 무너진 종탑으로 가서 종을 멈춰 주세요. 종지기의 망령들이 막아설 거예요. 그다음엔 촌장님께 — 마을이 준비를 해야 해요.",
		  "종탑은 폐성당 남서쪽이에요.",
		  "종이 멈췄다고? …그래, 오늘 밤엔 새벽이 조금 일찍 왔다네. 자네 덕이야. 하지만 데스나이트라니… 백 년 전 기사단을 무너뜨린 그 이름을 다시 듣게 될 줄은 몰랐네.",
		  { { O::Explore, 4, 1 }, { O::Kill, 18, 10, 6 } },
		  { 2400, 0, 3000, { M::EnchantScroll, M::EnchantScroll, M::EnchantScroll }, 2 } },

		// ════ 제4장 · 잠들지 못한 자들 (고대 유적 — 망자의 묘지) ════════════════════
		{ 10, K::Main, 4, "망자의 길", 6, 6, 20, 9, 0,
		  "망자의 묘지 깊은 곳, 고대 유적으로 가는 길을 연다.",
		  "망자의 묘지는 원래 우리 조상들의 쉼터였네. 그 아래 고대 유적은 심장나무를 섬기던 첫 사원이지. 좀비들이 길을 막고 있을 걸세. 길을 뚫고 유적을 확인해 주게.",
		  "망자의 묘지는 남서쪽 끝이라네. 루시엔에게 부탁하게.",
		  "유적 입구에 기사단 문장이 새겨져 있었다고? …백 년 전 기사들이 거기서 싸웠다는 이야기가 사실이었군.",
		  { { O::Explore, 5, 1 }, { O::Kill, 20, 12, 12 } },
		  { 3600, 0, 5000, { M::IronGauntlets, M::IronGauntlets, M::IronGauntlets }, 1, R::Heroic } },

		{ 11, K::Main, 4, "망령 기사단", 6, 7, 22, 10, 0,
		  "저주에 묶인 망령 기사들을 쓰러뜨리고, 그들의 문장을 거둬 신전에서 안식을 빈다.",
		  "그 기사들은 우리 마을을 지키다 쓰러진 이들이네. 저주에 묶여 아직도 싸우고 있지. 그들을 쉬게 해 주게. 문장을 거둬 세라핀에게 가져가면 안식의 기도를 올려 줄 걸세.",
		  "망령 기사는 단단하다네. 방어를 무너뜨린 뒤에 치게.",
		  "(문장을 제단에 올리자 불꽃이 잔잔해진다) 편히 잠드소서… 이 문장들이 말해요. 데스나이트는 성물을 삼키려다 실패했대요. 성물이 그를 거부했거든요.",
		  { { O::Kill, 21, 8, 12 }, { O::Collect, 21, 6, 12, 40, "녹슨 기사 문장", I::Crest } },
		  { 4400, 0, 6000, { M::SilverPendant, M::SilverPendant, M::SilverPendant }, 1, R::Heroic } },

		{ 12, K::Main, 4, "데스나이트", 7, 6, 24, 11, 0,
		  "고대 유적의 주인 데스나이트를 쓰러뜨리고 새벽의 성물을 되찾는다.",
		  "성물은 데스나이트 곁에 있어요. 혼자서는 무리예요 — 다른 모험가들과 함께 가세요. 그가 쓰러질 때 곁에 있기만 해도, 성물의 빛이 당신을 알아볼 거예요.",
		  "데스나이트의 불검은 땅을 가릅니다. 바닥의 붉은 빛을 조심하세요.",
		  "이것이… 새벽의 성물인가. (차갑다.) 빛이 없군. 껍데기만 남았어. 불씨가… 불씨는 어디로 간 겐가?",
		  { { O::Boss, MonsterTable::Id::DeathKnight, 1, 12 } },
		  { 8000, 0, 15000, { M::DkFlameSword, M::EmberStaff, M::VenomFang }, 1, R::Heroic } },

		// ════ 제5장 · 숲의 심장 ═══════════════════════════════════════════════════
		{ 13, K::Main, 5, "꺼진 불씨", 6, 3, 26, 12, 0,
		  "성물의 불씨가 어디로 흘러갔는지, 마력의 길을 보는 텔레포터 루시엔에게 묻는다.",
		  "루시엔은 마력이 흐르는 길을 본다네. 성물의 불씨가 어디로 흘러갔는지 그라면 알 걸세.",
		  "루시엔은 광장 한가운데 있다네.",
		  "(성물 위로 손을 펼치자 룬이 붉게 탄다) …순간이동 길이 흔들렸던 이유를 알겠네요. 불씨는 심연의 문 너머로 끌려갔어요. 누군가, 아니 무언가가 그걸 먹고 있어요.",
		  {}, { 2500, 0, 3000, { M::ClearPotion, M::ClearPotion, M::ClearPotion }, 10 } },

		{ 14, K::Main, 5, "심연의 문", 3, 3, 28, 13, 0,
		  "심연의 문을 넘어 악마들을 물리치고, 성물에서 흘러나간 불씨 조각을 되찾는다.",
		  "심연의 문으로 보내 드릴 수는 있어요. 돌아오는 길은… 당신 몫이에요. 지옥견들이 불씨 조각을 물고 다닌대요. 세 조각이면 성물이 다시 숨을 쉴 거예요.",
		  "심연에선 오래 머물지 마세요. 물약을 넉넉히.",
		  "(불씨 조각이 성물 속으로 스며든다) 됐어요, 성물이 다시 따뜻해요. 그런데… 가장 큰 불씨가 아직 저 안에 있어요. 바포메트. 심연의 주인이 그걸 삼켰어요.",
		  { { O::Explore, 6, 1 }, { O::Kill, 28, 10, 15 }, { O::Collect, 27, 3, 15, 30, "꺼지지 않는 불씨", I::Ember } },
		  { 7000, 0, 12000, { M::BlessedEnchant, M::BlessedEnchant, M::BlessedEnchant }, 1 } },

		{ 15, K::Main, 5, "심연의 주인", 3, 6, 30, 14, 0,
		  "심연의 주인 바포메트를 쓰러뜨리고 마지막 불씨를 되찾는다.",
		  "바포메트는 혼자 상대할 수 있는 존재가 아니에요. 모든 모험가를 불러 모으세요. 그가 쓰러질 때 곁에만 있어도, 불씨는 성물을 가진 당신에게 돌아올 거예요.",
		  "심연의 문 한가운데예요. 무운을.",
		  "(성물이 새벽빛으로 타오른다) 오오… 백 년 만에 보는 빛이군. 가게, 젊은이. 심장나무에게. 이 마을의, 이 숲의 새벽을 돌려주게.",
		  { { O::Boss, MonsterTable::Id::Baphomet, 1, 15 } },
		  { 14000, 0, 30000, { M::DawnPlate, M::StarlightRobe, M::MoonshadowCloak }, 1, R::Heroic } },

		{ 16, K::Main, 5, "다시 뛰는 심장", 6, 0, 30, 15, 0,
		  "새벽의 성물을 숲의 심장에 돌려놓는다.",
		  "성물을 옹이에 돌려놓게. 나머지는… 숲이 알 걸세.",
		  "속삭이는 숲 한가운데, 잠든 거목. 자네는 이미 가 본 곳이지.",
		  "성물을 옹이에 대자, 거목 깊은 곳에서 무언가가 뛰었다. 쿵. …쿵. 백 년을 이어 온 박동이 다시 숲을 채운다. 속삭임은 노래가 되고, 긴 밤 끝에 — 새벽이 왔다.",
		  { { O::Explore, 2, 1 } },
		  { 20000, 0, 50000, { M::DragonSlayer, M::ArchmageStaff, M::Nightfang }, 1, R::Legendary } },

		// ════ 부가 이야기 ══════════════════════════════════════════════════════════
		{ 30, K::Side, 0, "누렁이는 어디에?", 10, 10, 1, 0, 0,
		  "꼬마 토비의 개 누렁이를 찾아 쓰다듬어 준다.",
		  "형! 누렁이 못 봤어? 또 어디 갔나 봐… 누렁이는 쓰다듬어 주면 좋아서 꼬리를 막 흔들어. 찾으면 한 번만 쓰다듬어 줘!",
		  "누렁이는 광장이랑 분수 근처를 막 뛰어다녀!",
		  "진짜? 누렁이가 꼬리 흔들었어? 헤헤, 형 좋은 사람인가 봐. 이거 줄게. 엄마가 아껴 둔 건데… 비밀이야!",
		  { { O::Talk, 17, 1, kAnyZone, 100, "누렁이 쓰다듬어 주기" } },
		  { 20, 0, 50, { M::HastePotion, M::HastePotion, M::HastePotion }, 1 } },

		{ 31, K::Side, 0, "허수아비 도둑", 13, 13, 2, 0, 0,
		  "허수아비를 뜯어 간 고블린들에게서 허수아비 조각을 되찾는다.",
		  "고블린 녀석들이 내 허수아비를 뜯어 갔소. 옷가지며 밀짚이며… 대체 그걸로 뭘 하려는지. 조각을 좀 찾아다 주시오. 올해 농사가 걸렸소!",
		  "평원의 고블린들이오. 밀짚 냄새가 나는 녀석들!",
		  "허허, 이걸로 다시 세울 수 있겠소. 고맙소! 닭들도 고맙다고 할 거요. …아마도.",
		  { { O::Kill, 2, 6, 5 }, { O::Collect, 2, 4, 5, 50, "허수아비 조각", I::Straw } },
		  { 120, 0, 250, { M::LeatherBoots, M::LeatherBoots, M::LeatherBoots }, 1, R::Uncommon } },

		{ 32, K::Side, 0, "여관의 저녁거리", 8, 8, 7, 0, 0,
		  "여관 주인 마르타의 스튜에 넣을 곰 고기를 구해 온다.",
		  "요즘 손님들이 전부 숲 얘기만 하면서 밥은 두 그릇씩 먹어요! 곰 고기가 다 떨어졌지 뭐예요. 곰 고기 좀 구해다 줄래요? 스튜 끓여 둘게요!",
		  "곰은 속삭이는 숲이나 요정의 숲에 있어요. 크니까 금방 보일 거예요!",
		  "어머, 싱싱하기도 해라! 오늘 스튜는 특별 메뉴예요. 이건 수고비, 물약은 제 선물이고요!",
		  { { O::Collect, 6, 5, kAnyZone, 55, "곰 고기", I::Meat } },
		  { 500, 0, 700, { M::OrangePotion, M::OrangePotion, M::OrangePotion }, 10 } },

		{ 33, K::Side, 0, "반짝반짝 정령의 빛", 11, 11, 6, 0, 0,
		  "꼬마 미나에게 요정의 숲 정령들이 흘리는 빛을 모아다 준다.",
		  "할머니가 그러는데, 요정의 숲에는 반짝반짝 빛나는 정령이 산대! 정령의 빛을 병에 담으면 밤에도 안 무섭대. 나 다섯 개만 갖다 줘, 응?",
		  "요정의 숲은 서쪽이래! 루시엔 아저씨가 보내 준대!",
		  "우와아… 진짜 반짝반짝해! 이제 밤이 하나도 안 무서워! 이거, 내 보물이야. 가져!",
		  { { O::Collect, 11, 5, 4, 45, "정령의 빛", I::SpiritLight } },
		  { 400, 0, 400, { M::CopperRing, M::CopperRing, M::CopperRing }, 1, R::Rare } },

		{ 34, K::Side, 0, "성벽 밖의 불빛", 16, 16, 10, 0, 0,
		  "순찰 경비병이 본 수상한 불빛 — 코볼트 야영지를 찾아 녀석들을 흩어 놓는다.",
		  "밤마다 성벽 밖 먼 데서 불빛이 보였소. 알아보니 코볼트 놈들이 사막 쪽에 야영지를 차렸더군. 마을로 오는 상인들을 노리는 거요. 야영지를 찾아서 녀석들을 흩어 주시오.",
		  "모래바람 사막 한가운데요. 모래 전갈도 조심하시오.",
		  "야영지를 흩었다고? 잘했소! 이제 상인들이 안심하고 다니겠군. 이건 경비대 창고에서 꺼내 온 거요.",
		  { { O::Explore, 8, 1 }, { O::Kill, 8, 10 } },
		  { 700, 0, 1500, { M::IronHelm, M::IronHelm, M::IronHelm }, 1, R::Rare } },

		{ 35, K::Side, 0, "음유시인의 새 노래", 9, 9, 12, 6, 0,
		  "새 노래의 영감을 찾는 음유시인 리안을 위해 난파선과 요정의 샘을 보고 온다.",
		  "노래가 바닥났소. 새 이야기가 필요하오! 파도 해안에 반쯤 묻힌 난파선이 있다던데, 요정의 샘은 달을 둘 비춘다더군. 두 곳을 보고 와서 이야기해 주시오. 당신 이름을 노래에 넣어 드리리다.",
		  "난파선은 남쪽 파도 해안, 샘은 서쪽 요정의 숲이오.",
		  "♪ 파도에 잠긴 배, 달을 품은 샘… 그 사이를 걸은 모험가여! …어떻소? 괜찮지 않소? 이건 노래값이오.",
		  { { O::Explore, 9, 1 }, { O::Explore, 7, 1 } },
		  { 900, 0, 1200, { M::NightCloak, M::NightCloak, M::NightCloak }, 1, R::Rare } },

		// ════ 반복 의뢰(하루 한 번) ═══════════════════════════════════════════════════
		{ 60, K::Daily, 0, "경비대 사냥 의뢰", 15, 15, 1, 0, 0,
		  "(반복) 여명의 평원의 몬스터를 정리한다.",
		  "오늘도 평원이 시끄럽소. 열다섯 마리만 정리해 주면 경비대에서 사례하리다. 매일 부탁할 거요.",
		  "여명의 평원에 있는 녀석이면 무엇이든 좋소.",
		  "수고했소. 내일도 부탁하오.",
		  { { O::KillIn, 0, 15, 5 } },
		  { 0, 20, 300, { M::HealthPotion, M::HealthPotion, M::HealthPotion }, 5 }, 20 * 60 },

		{ 61, K::Daily, 0, "숲의 정화", 12, 12, 6, 4, 0,
		  "(반복) 속삭이는 숲의 거칠어진 짐승들을 잠재운다.",
		  "숲이 앓는 동안엔 짐승들도 앓는다우. 스무 마리만 잠재워 주우. 날마다 부탁하는 게 미안하구먼.",
		  "속삭이는 숲이면 아무 짐승이나 괜찮수.",
		  "고생했수. 숲이 조금은 조용해졌을 게야.",
		  { { O::KillIn, 0, 20, 1 } },
		  { 0, 20, 800, { M::OrangePotion, M::OrangePotion, M::OrangePotion }, 5 }, 20 * 60 },

		{ 62, K::Daily, 0, "망자의 안식", 7, 7, 20, 10, 0,
		  "(반복) 망자의 묘지에서 잠들지 못한 자들에게 안식을 준다.",
		  "망자의 묘지는 오늘도 소란해요. 스물다섯 영혼만 쉬게 해 주세요. 매일 기도할게요.",
		  "망자의 묘지에서요. 빛이 함께하기를.",
		  "고마워요. 오늘 밤은 조금 더 조용할 거예요.",
		  { { O::KillIn, 0, 25, 12 } },
		  { 0, 20, 3000, { M::EnchantScroll, M::EnchantScroll, M::EnchantScroll }, 1 }, 20 * 60 },

		{ 63, K::Daily, 0, "서리 거인 토벌", 5, 5, 18, 0, 0,
		  "(반복) 북쪽 설원의 서리 거인을 쓰러뜨린다.",
		  "서리 거인이 또 깨어났다는 소식이오. 녀석이 설원을 넘기 전에 막아야 하오. 혼자선 무리니 동료를 모으시오.",
		  "북쪽 서리 설원이오. 몸을 따뜻하게 하시오.",
		  "서리 거인을 쓰러뜨렸다니! 경비대가 한턱내겠소.",
		  { { O::Boss, MonsterTable::Id::FrostGiant, 1, 0 } },
		  { 0, 35, 8000, { M::BlessedEnchant, M::BlessedEnchant, M::BlessedEnchant }, 1 }, 20 * 60 },

		// ════ 직업의 길(Lv.12) ══════════════════════════════════════════════════════
		{ 80, K::Class, 0, "철벽의 맹세", 4, 4, 12, 0, kKnightOnly,
		  "기사의 길 — 숲의 짐승 무리를 방패로 받아 낸다.",
		  "기사가 되려는 자는 먼저 무리를 막아서는 법을 배워야 하오. 숲의 늑대와 곰 — 그 무리를 방패로 받아 내시오. 도망치지 말고. 철벽 태세로 버티다 반격하는 거요.",
		  "속삭이는 숲이오. 등 뒤의 마을을 생각하시오.",
		  "좋소. 이제 당신 뒤에 선 사람들은 안심할 수 있겠군. 이 검은 경비대장이 쓰던 것이오.",
		  { { O::Kill, 5, 8, 1 }, { O::Kill, 6, 6, 1 } },
		  { 900, 0, 1500, { M::SteelSword, M::SteelSword, M::SteelSword }, 1, R::Heroic } },

		{ 81, K::Class, 0, "룬의 속삭임", 3, 3, 12, 0, kRuneOnly,
		  "룬술사의 길 — 푸른 젤리가 삼킨 룬 돌을 모은다.",
		  "룬술사라면 룬의 목소리를 들어야죠. 푸른 젤리들이 가끔 룬이 새겨진 돌을 삼켜요. 여섯 개를 모아 오세요. 그 돌들이 당신 지팡이에 깃들 거예요.",
		  "푸른 젤리는 요정의 숲과 파도 해안에 많아요.",
		  "(룬 돌을 지팡이에 대자 푸른 불이 번진다) 들리죠? 이제 이 지팡이는 당신 목소리로 말할 거예요.",
		  { { O::Collect, 7, 6, kAnyZone, 40, "룬이 새겨진 돌", I::RuneStone } },
		  { 900, 0, 1500, { M::RuneStaff, M::RuneStaff, M::RuneStaff }, 1, R::Heroic } },

		{ 82, K::Class, 0, "그림자의 시험", 15, 15, 12, 0, kHunterOnly,
		  "그림자 사냥꾼의 길 — 등 뒤를 보이지 않는 모래 전갈을 사냥한다.",
		  "사냥꾼은 보이지 않는 곳에서 친다고들 하지. 모래바람 사막의 전갈은 등 뒤를 잘 내주지 않소. 열 마리. 그림자 걸음으로 등 뒤를 잡아 보시오.",
		  "모래바람 사막이오. 독을 조심하시오.",
		  "전갈 열 마리를… 소리도 없이? 당신은 진짜 그림자요. 이 단검이 달빛을 머금는 걸 보게 될 거요.",
		  { { O::Kill, 9, 10, 10 } },
		  { 900, 0, 1500, { M::MoonlightBlade, M::MoonlightBlade, M::MoonlightBlade }, 1, R::Heroic } },
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
