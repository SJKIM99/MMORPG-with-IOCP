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
		Count
	};

	void Init();
	void Shutdown();
	[[nodiscard]] bool Ready();

	// volume 0~1, pitchVar: ±비율(0.05 = ±5%), pan: 0 왼쪽 ~ 1 오른쪽(0.5 가운데)
	void Play(Id id, float volume = 1.f, float pitchVar = 0.04f, float pan = 0.5f);
	// 음높이를 정해서(목소리: 아이는 높게, 노인은 낮게)
	void PlayPitched(Id id, float volume, float pitch, float pan = 0.5f);
	void SetMasterVolume(float v);
	[[nodiscard]] float MasterVolume();
}
