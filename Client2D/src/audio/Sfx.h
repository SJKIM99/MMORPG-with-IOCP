#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 효과음 — 전부 시작할 때 절차적으로 합성한다(사인/톱니/노이즈 + 엔벨로프/필터).
// 같은 소리가 겹쳐도 끊기지 않도록 소리마다 별칭(alias) 여러 개를 돌려 쓰고,
// 재생할 때마다 음높이를 조금씩 흔들어 반복감을 줄인다.
// ─────────────────────────────────────────────────────────────────────────────
namespace sfx
{
	enum class Id
	{
		Click, Hover, Open, Close, Error,
		Swing, Hit, HitHeavy, Kill, PlayerHurt, Death, MonsterSwipe, Skill,
		Coin, Pickup, Drop, Equip, Buy,
		Potion, Heal, LevelUp, Teleport, Haste,
		EnchantCharge, EnchantSuccess, EnchantFail,
		BossRoar, Slam, SlamWarn,
		Step, Notice, Chat,
		AmbBird, AmbCricket, AmbWind, AmbBubble, AmbCrackle, AmbWave, AmbCrow,
		// V2 전투(직업 기술·연계·상태이상)
		ShieldBash, Whirl, Dash, GuardUp, ChargeUp, Execute,
		RuneBolt, FrostSeal, FireRing, Blink, Thunder, MeteorFall, MeteorImpact,
		Stab, ShadowStep, PoisonSlash, Evade, Backstab, MoonDance,
		ComboSting, SkillReady, NoMana, Counter, WallSlam, PoisonBurst, SteamBurst,
		TeleWarn, Interrupt, StatUp, DotTick, Miss, Freeze, Stun,
		// 살아 있는 마을
		Voice, DogBark, CatMeow, Cluck, Lute, Pet,
		// 아이템 성장: 희귀도별 획득음
		LootRare, LootHeroic, LootLegendary, LootMythic,
		// 퀘스트 · 메인 이야기
		QuestAccept, QuestTick, QuestObjDone, QuestReady, QuestComplete, ChapterBell, Discover, Heartbeat, QuestFly,
		// Phase 1-C: 바닥 종류별 발소리(풀 · 돌 · 나무 · 눈 · 모래)
		StepGrass, StepStone, StepWood, StepSnow, StepSand,
		// Phase 2-A: 기술 소리 겹(예고 · 발동 · 타격 · 여운을 섞어 쓴다 — Layered)
		BellToll,     // 큰 종(심판의 종 · 단죄) — 어긋난 배음, 긴 울림
		BellSmall,    // 작은 종(서약 · 방패)
		Scribe,       // 허공에 글자를 새기는 반짝임(각인술사의 시작)
		Whoosh,       // 공기를 가르는 빠른 쉭(돌진 · 투척 · 휘두름 꼬리)
		BoomLow,      // 땅울림(서브 베이스)
		Burst,        // 폭발 몸통(노이즈 파열 + 딱딱 튀는 잔불)
		FireRoar,     // 불길이 치솟는 웅
		IceCrack,     // 얼음 금 가는 소리 + 유리 배음
		ThunderClap,  // 벼락: 쩍 + 우르릉
		InkSplash,    // 먹물이 튀는 젖은 소리
		MetalRing,    // 쨍한 쇳소리(배음 셋)
		TailVerb,     // 공기 같은 긴 여운
		ChargeLoop,   // 힘이 차오르는 상승음(0.9초)
		ChargeStage,  // 차지 단계 도달 — 짧은 딩
		Perfect,      // 완벽 구간 — 맑은 화음
		Roll,         // 몸 굴리기(쿵 + 옷 스침)
		Rally,        // 외침(뿔나팔 같은 톤)
		BladeHum,     // 칼날 여럿이 도는 빠른 쉭쉭
		Cutin,        // 각성기 컷인: 휙 + 쿵
		Drone,        // 암전 저음(각성기 예고)
		// Phase 4-B: 마을 소리 구역(audio/Ambience)
		Anvil,        // 모루를 두드리는 쨍(대장간 망치)
		// Phase 4 자기 검토: 마을의 작은 생명(render/Critters)
		Flutter,      // 비둘기 떼가 한꺼번에 날아오르는 푸드덕(날개 치는 소리 열두어 번)
		FishSplash,   // 물고기가 튀어 올랐다 떨어지는 퐁당(작게)
		// Phase 5: 결사 · 경매장
		Gavel,        // 경매 망치: 나무 두 번 탁탁(낙찰)
		Seal,         // 결사 인장: 묵직한 쿵 + 종이 스침 + 작은 종(세움 · 들어옴)
		CoinPour,     // 동전이 쏟아지는 짤랑짤랑(정산 받기 · 팔림)
		Count
	};

	// 겹 하나: delay초 뒤 volume, 음높이 pitch(1 = 원래)
	struct Layer { Id id; float delay = 0.f; float volume = 1.f; float pitch = 1.f; };

	void Init();
	void Shutdown();
	[[nodiscard]] bool Ready();

	// volume 0~1, pitchVar: ±비율(0.05 = ±5%), pan: 0 왼쪽 ~ 1 오른쪽(0.5 가운데)
	void Play(Id id, float volume = 1.f, float pitchVar = 0.04f, float pan = 0.5f);
	// 음높이를 정해서(목소리: 아이는 높게, 노인은 낮게)
	void PlayPitched(Id id, float volume, float pitch, float pan = 0.5f);
	void SetMasterVolume(float v);
	[[nodiscard]] float MasterVolume();

	// Phase 2-A: 늦게 울리기 · 겹 여러 개(예고 · 발동 · 타격 · 여운) · 매 프레임 진행
	void PlayAfter(Id id, float delay, float volume = 1.f, float pitchVar = 0.04f, float pan = 0.5f);
	void Layered(const Layer* layers, int count, float volume = 1.f, float pan = 0.5f);
	void Update(float dt);
	// 같은 소리를 이 시간(초) 안에 다시 부르면 무시한다(겹쳐 찢어지는 소리 방지 — 기본 0.03)
	void SetDedupe(float seconds);
}
