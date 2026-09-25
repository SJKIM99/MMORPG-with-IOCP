#pragma once

#include "raylib.h"

// ─────────────────────────────────────────────────────────────────────────────
// 입력 한 곳 — 월드 조작(이동 · 공격 · 기술)이 raylib 입력을 직접 부르지 않고 여기를 거친다.
//  · 자동 시나리오(--script)가 사람과 **같은 경로**로 마우스를 누르고 옮길 수 있게 한다
//    (SetScriptMouse — UI 판정 위치도 같이 옮긴다).
//  · 키 재지정(game/KeyBinds)은 이 위에 "동작 → 키"를 얹는다.
// ─────────────────────────────────────────────────────────────────────────────
namespace in
{
	void BeginFrame();                                   // 프레임 맨 앞(눌림 판정 갱신)

	[[nodiscard]] Vector2 MousePos();
	[[nodiscard]] bool MouseDown(int button);
	[[nodiscard]] bool MousePressed(int button);
	[[nodiscard]] bool MouseReleased(int button);

	// 자동 시나리오: 이번 프레임부터 마우스를 이 위치 · 이 버튼 상태로 본다. off로 되돌린다.
	void SetScriptMouse(Vector2 pos, bool leftDown, bool rightDown = false);
	void ClearScriptMouse();
	[[nodiscard]] bool ScriptMouse();
}
