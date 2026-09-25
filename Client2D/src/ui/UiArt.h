#pragma once

#include "raylib.h"

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// UI 그림(Higgsfield — tools/assetgen/process.py가 assets/ui에 만든다).
//   panel_tex  창 바탕의 돌결(아주 옅게 · 반복)   corner  청동 모서리 장식   divider  제목 아래 장식선
//   cursor_*   마우스 커서(화살 · 공격 · 줍기 · 대화)
// 파일이 없으면 nullptr — 「새김」의 단색 판 · 벡터 커서로 그린다.
// ─────────────────────────────────────────────────────────────────────────────
namespace uiart
{
	enum Id : int { PanelTex, Corner, Divider, Plaque, CursorArrow, CursorAttack, CursorPick, CursorTalk, Count };

	bool Init(const std::string& dir);
	void Shutdown();
	[[nodiscard]] const Texture2D* Get(Id id);
}
