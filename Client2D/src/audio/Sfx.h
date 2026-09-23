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
		Count
	};

	void Init();
	void Shutdown();
	[[nodiscard]] bool Ready();

	// volume 0~1, pitchVar: ±비율(0.05 = ±5%), pan: 0 왼쪽 ~ 1 오른쪽(0.5 가운데)
	void Play(Id id, float volume = 1.f, float pitchVar = 0.04f, float pan = 0.5f);
	void SetMasterVolume(float v);
	[[nodiscard]] float MasterVolume();
}
