#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 소리 구역(Phase 4-B) — 마을의 자리마다 깔리는 바탕 소리.
//
// 마을 원본 ambient.txt → TownMap::kAmbient(종류 · 자리 · 반경)를 읽어, 듣는 사람(내 캐릭터)과의 거리로
// 종류마다 음량을 정한다. 분수 · 강물 · 바람 · 시장 웅성임 · 대장간 불길은 끊김 없는 고리 소리(합성)를
// 오디오 스레드가 섞어 한 스트림으로 내보내고(프레임이 느려도 끊기지 않는다), 망치 · 예배당 종은
// 이 모듈이 박자를 정해 sfx 한 번 소리로 울린다.
//
// 서버와는 아무 관계가 없다(연출). 소리 장치가 없으면(--mute · 샌드박스) 음량 계산만 하고 조용히 지나간다.
// ─────────────────────────────────────────────────────────────────────────────
#include "raylib.h"

namespace amb
{
	enum class Bed { Fountain, River, Wind, Market, Forge, Count };

	// 종류마다 0~1 음량과 소리가 오는 쪽(0 왼쪽 ~ 1 오른쪽)
	struct Mix
	{
		float gain[static_cast<int>(Bed::Count)]{};
		float pan[static_cast<int>(Bed::Count)]{};
		float bell = 0.f;        // 예배당 종이 들리는 정도(종이 울릴 때 음량)
	};

	// 소리 장치가 준비된 뒤(sfx::Init 다음). 고리 소리를 합성하고 섞는 스트림을 연다.
	void Init();
	void Shutdown();

	// 이 자리(월드 칸)에서 들리는 음량 — 소리 장치 없이도 계산된다(스크립트 · 점검용)
	// night 0 낮 ~ 1 한밤, wind 0~1 바람 세기
	[[nodiscard]] Mix MixAt(Vector2 tile, float night, float wind);

	// 매 프레임: 듣는 자리 · 시각 · 바람 → 목표 음량(오디오 스레드가 부드럽게 따라간다) + 망치 · 종 박자
	// hour는 게임 시각(0~24) — 낮의 정시마다 예배당 종
	void Update(float dt, Vector2 listenerTile, float night, float hour, float wind, bool enabled);

	// 개발용: 이 자리의 섞인 소리를 seconds초 WAV로 쓴다(소리 장치 없이 — 샌드박스 점검용)
	bool DumpWav(const char* path, Vector2 tile, float night, float wind, float seconds);
	// 개발용: 고리 소리 다섯을 하나씩 dir/bed_<종류>.wav로(합성 점검)
	void DumpBeds(const char* dir);
}
