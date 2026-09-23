#pragma once

#include "raylib.h"

// ─────────────────────────────────────────────────────────────────────────────
// 절차적으로 만드는 공용 텍스처(빛번짐/연기/불꽃/별 등). 이미지 파일 없이 시작 시
// 픽셀을 직접 계산해서 만든다 — 저장소에 바이너리 에셋을 늘리지 않기 위함.
// ─────────────────────────────────────────────────────────────────────────────
namespace assets
{
	struct Textures
	{
		Texture2D glow{};      // 부드러운 원형 빛(가장자리로 갈수록 투명)
		Texture2D soft{};      // 조금 더 단단한 원(파티클 기본)
		Texture2D smoke{};     // 울퉁불퉁한 연기 뭉치
		Texture2D spark{};     // 가로로 긴 불꽃(속도 방향으로 늘여 그린다)
		Texture2D star{};      // 네 갈래 반짝임
		Texture2D ring{};      // 얇은 고리
		Texture2D beam{};      // 세로 빛기둥(가운데 밝고 좌우로 사라짐)
		Texture2D vignette{};  // 화면 가장자리 어둡게
		Texture2D noise{};     // 256x256 값 노이즈(셰이더용)
	};

	void Load();
	void Unload();
	const Textures& Get();
}
