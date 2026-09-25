#include "game/Input.h"
#include "ui/Ui.h"

namespace in
{
	namespace
	{
		bool g_script = false;
		Vector2 g_pos{};
		bool g_down[2]{};          // 시나리오가 지금 누르고 있다고 한 상태
		bool g_seen[2]{}, g_prev[2]{};   // 이번 프레임에 보는 상태 · 지난 프레임에 본 상태
	}

	void BeginFrame()
	{
		for (int i = 0; i < 2; ++i)
		{
			g_prev[i] = g_seen[i];
			g_seen[i] = g_down[i];
		}
	}

	Vector2 MousePos() { return g_script ? g_pos : GetMousePosition(); }

	bool MouseDown(int button)
	{
		if (g_script && (button == MOUSE_BUTTON_LEFT || button == MOUSE_BUTTON_RIGHT))
			return g_seen[button == MOUSE_BUTTON_LEFT ? 0 : 1];
		return IsMouseButtonDown(button);
	}

	bool MousePressed(int button)
	{
		if (g_script && (button == MOUSE_BUTTON_LEFT || button == MOUSE_BUTTON_RIGHT))
		{
			const int i = button == MOUSE_BUTTON_LEFT ? 0 : 1;
			return g_seen[i] && !g_prev[i];
		}
		return IsMouseButtonPressed(button);
	}

	bool MouseReleased(int button)
	{
		if (g_script && (button == MOUSE_BUTTON_LEFT || button == MOUSE_BUTTON_RIGHT))
		{
			const int i = button == MOUSE_BUTTON_LEFT ? 0 : 1;
			return !g_seen[i] && g_prev[i];
		}
		return IsMouseButtonReleased(button);
	}

	void SetScriptMouse(Vector2 pos, bool leftDown, bool rightDown)
	{
		g_script = true;
		g_pos = pos;
		g_down[0] = leftDown;
		g_down[1] = rightDown;
		ui::SetMouseOverride(true, pos);
	}

	void ClearScriptMouse()
	{
		if (!g_script) return;
		g_script = false;
		g_down[0] = g_down[1] = g_seen[0] = g_seen[1] = g_prev[0] = g_prev[1] = false;
		ui::SetMouseOverride(false);
	}

	bool ScriptMouse() { return g_script; }
}
