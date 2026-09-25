#include "game/KeyBinds.h"
#include "ui/Ui.h"
#include "platform/Ime.h"

#include "raylib.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace kb
{
	namespace
	{
		struct Row { Act act; const char* id; const char* name; const char* group; Chord def; };

		// 기본값 · 이름 · 묶음. id는 저장 파일에 쓰는 이름(바꾸지 않는다).
		constexpr Row kRows[kActCount] =
		{
			{ Act::BasicAttack, "BasicAttack", "기본 공격", "전투", { KEY_C, 0 } },
			{ Act::Skill1,  "Skill1",  "기술 1", "전투", { KEY_Q, 0 } },
			{ Act::Skill2,  "Skill2",  "기술 2", "전투", { KEY_W, 0 } },
			{ Act::Skill3,  "Skill3",  "기술 3", "전투", { KEY_E, 0 } },
			{ Act::Skill4,  "Skill4",  "기술 4", "전투", { KEY_R, 0 } },
			{ Act::Skill5,  "Skill5",  "기술 5", "전투", { KEY_A, 0 } },
			{ Act::Skill6,  "Skill6",  "기술 6", "전투", { KEY_S, 0 } },
			{ Act::Skill7,  "Skill7",  "기술 7", "전투", { KEY_D, 0 } },
			{ Act::Skill8,  "Skill8",  "기술 8", "전투", { KEY_F, 0 } },
			{ Act::Awaken,  "Awaken",  "각성기", "전투", { KEY_V, 0 } },
			{ Act::Mobility, "Mobility", "이동기", "전투", { KEY_SPACE, 0 } },
			{ Act::Identity, "Identity", "아이덴티티", "전투", { KEY_Z, 0 } },
			{ Act::Identity2, "Identity2", "아이덴티티 2", "전투", { KEY_X, 0 } },
			{ Act::ForceAttack, "ForceAttack", "강제 공격(누르고 있기)", "전투", { KEY_LEFT_CONTROL, 0 } },
			{ Act::Battle1, "Battle1", "배틀 칸 1", "소모품", { KEY_ONE, 0 } },
			{ Act::Battle2, "Battle2", "배틀 칸 2", "소모품", { KEY_TWO, 0 } },
			{ Act::Battle3, "Battle3", "배틀 칸 3", "소모품", { KEY_THREE, 0 } },
			{ Act::Battle4, "Battle4", "배틀 칸 4", "소모품", { KEY_FOUR, 0 } },
			{ Act::Potion,  "Potion",  "체력 물약", "소모품", { KEY_F1, 0 } },
			{ Act::Aux1,    "Aux1",    "보조 칸 1", "소모품", { KEY_FIVE, 0 } },
			{ Act::Aux2,    "Aux2",    "보조 칸 2", "소모품", { KEY_SIX, 0 } },
			{ Act::Aux3,    "Aux3",    "보조 칸 3", "소모품", { KEY_SEVEN, 0 } },
			{ Act::Aux4,    "Aux4",    "보조 칸 4", "소모품", { KEY_EIGHT, 0 } },
			{ Act::Interact, "Interact", "상호작용(대화 · 줍기)", "창 · 상호작용", { KEY_G, 0 } },
			{ Act::Inventory, "Inventory", "소지품", "창 · 상호작용", { KEY_I, 0 } },
			{ Act::Character, "Character", "캐릭터", "창 · 상호작용", { KEY_P, 0 } },
			{ Act::SkillWindow, "SkillWindow", "기술", "창 · 상호작용", { KEY_K, 0 } },
			{ Act::QuestLog, "QuestLog", "퀘스트 일지", "창 · 상호작용", { KEY_J, 0 } },
			{ Act::WorldMap, "WorldMap", "월드 지도", "창 · 상호작용", { KEY_M, 0 } },
			{ Act::BigMap,  "BigMap",  "확장 미니맵", "창 · 상호작용", { KEY_TAB, 0 } },
			{ Act::Guild,   "Guild",   "결사", "창 · 상호작용", { KEY_U, ModAlt } },
			{ Act::Auction, "Auction", "경매장", "창 · 상호작용", { KEY_Y, ModAlt } },
			{ Act::Chat,    "Chat",    "채팅", "기타", { KEY_ENTER, 0 } },
			{ Act::Command, "Command", "명령어", "기타", { KEY_SLASH, 0 } },
			{ Act::ShowNames, "ShowNames", "이름 모두 보기(누르고 있기)", "기타", { KEY_LEFT_ALT, 0 } },
			{ Act::Help,    "Help",    "도움말", "기타", { KEY_H, 0 } },
			{ Act::DebugInfo, "DebugInfo", "개발 정보", "기타", { KEY_F3, 0 } },
			{ Act::ZoomIn,  "ZoomIn",  "확대", "기타", { KEY_EQUAL, 0 } },
			{ Act::ZoomOut, "ZoomOut", "축소", "기타", { KEY_MINUS, 0 } },
		};

		// 표의 순서 = 열거형 순서(RowOf가 번호로 찾는다)
		constexpr bool RowsInOrder()
		{
			for (int i = 0; i < kActCount; ++i)
				if (static_cast<int>(kRows[i].act) != i) return false;
			return true;
		}
		static_assert(RowsInOrder(), "kb::kRows 순서가 Act 열거형과 다릅니다");

		std::array<Chord, kActCount> g_bind{};
		bool g_init = false;
		Options g_opt;
		bool g_blocked = false;
		bool g_capturing = false;
		// 자동 시나리오: 요청(g_scriptHold) → 프레임 맨 앞에서 이번 프레임 상태(seen)로 옮기고, 지난 프레임 상태(prev)와 비교한다
		std::array<bool, kActCount> g_scriptPress{}, g_scriptHold{}, g_scriptSeen{}, g_scriptPrev{};

		void EnsureInit()
		{
			if (g_init) return;
			g_init = true;
			for (int i = 0; i < kActCount; ++i) g_bind[static_cast<size_t>(i)] = kRows[i].def;
		}

		const Row& RowOf(Act a) { return kRows[static_cast<int>(a) < kActCount ? static_cast<int>(a) : 0]; }

		bool IsModKey(int k)
		{
			return k == KEY_LEFT_CONTROL || k == KEY_RIGHT_CONTROL || k == KEY_LEFT_ALT || k == KEY_RIGHT_ALT
				|| k == KEY_LEFT_SHIFT || k == KEY_RIGHT_SHIFT;
		}

		// 같은 뜻의 다른 키(숫자패드 · 오른쪽 수식키)
		int Twin(int k)
		{
			if (k >= KEY_ZERO && k <= KEY_NINE) return KEY_KP_0 + (k - KEY_ZERO);
			switch (k)
			{
			case KEY_ENTER: return KEY_KP_ENTER;
			case KEY_EQUAL: return KEY_KP_ADD;
			case KEY_MINUS: return KEY_KP_SUBTRACT;
			case KEY_LEFT_CONTROL: return KEY_RIGHT_CONTROL;
			case KEY_LEFT_ALT: return KEY_RIGHT_ALT;
			case KEY_LEFT_SHIFT: return KEY_RIGHT_SHIFT;
			default: return 0;
			}
		}

		bool CtrlDown() { return IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL); }
		bool AltDown() { return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT); }
		bool ShiftDown() { return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT); }

		// 요구한 보조키가 눌려 있는가. 요구하지 않은 보조키가 눌려 있으면, 같은 키를 그 보조키와 함께 쓰는
		// 동작이 따로 있을 때만 막는다(더 자세한 조합이 이긴다: Alt+U 결사와 U가 따로 있어도 헷갈리지 않게).
		// 그런 동작이 없으면 막지 않는다 — Ctrl + 기술 = 강제 공격.
		bool ModsOk(const Chord& c)
		{
			const bool held[3] = { CtrlDown(), ShiftDown(), AltDown() };
			constexpr uint8_t bit[3] = { ModCtrl, ModShift, ModAlt };
			for (int m = 0; m < 3; ++m)
			{
				if ((c.mods & bit[m]) && !held[m]) return false;
				if (!(c.mods & bit[m]) && held[m] && !IsModKey(c.key))
					for (int i = 0; i < kActCount; ++i)
					{
						const Chord& o = g_bind[static_cast<size_t>(i)];
						if (o.key == c.key && (o.mods & bit[m]) && (o.mods & ~c.mods) == bit[m]) return false;
					}
			}
			return true;
		}

		bool KeyPressed(int k) { const int t = Twin(k); return IsKeyPressed(k) || (t && IsKeyPressed(t)); }
		bool KeyDown(int k) { const int t = Twin(k); return IsKeyDown(k) || (t && IsKeyDown(t)); }
		bool KeyReleased(int k) { const int t = Twin(k); return IsKeyReleased(k) || (t && IsKeyReleased(t)); }
	}

	const char* Name(Act a) { return RowOf(a).name; }
	const char* Group(Act a) { return RowOf(a).group; }
	Chord Default(Act a) { return RowOf(a).def; }
	Chord Get(Act a) { EnsureInit(); return g_bind[static_cast<size_t>(a)]; }
	void Set(Act a, Chord c) { EnsureInit(); g_bind[static_cast<size_t>(a)] = c; }
	void ResetDefaults() { g_init = false; EnsureInit(); }
	Options& Opt() { return g_opt; }

	Act Conflict(Act a, Chord c)
	{
		EnsureInit();
		if (c.Empty()) return Act::Count;
		for (int i = 0; i < kActCount; ++i)
		{
			const Act o = static_cast<Act>(i);
			if (o != a && g_bind[static_cast<size_t>(i)] == c) return o;
		}
		return Act::Count;
	}

	std::string KeyName(int k)
	{
		if (k >= KEY_A && k <= KEY_Z) return std::string(1, static_cast<char>('A' + (k - KEY_A)));
		if (k >= KEY_ZERO && k <= KEY_NINE) return std::string(1, static_cast<char>('0' + (k - KEY_ZERO)));
		if (k >= KEY_F1 && k <= KEY_F12) return "F" + std::to_string(k - KEY_F1 + 1);
		if (k >= KEY_KP_0 && k <= KEY_KP_9) return "Num" + std::to_string(k - KEY_KP_0);
		switch (k)
		{
		case KEY_SPACE: return "Spc";
		case KEY_TAB: return "Tab";
		case KEY_ENTER: return "Enter";
		case KEY_SLASH: return "/";
		case KEY_BACKSLASH: return "\\";
		case KEY_COMMA: return ",";
		case KEY_PERIOD: return ".";
		case KEY_SEMICOLON: return ";";
		case KEY_APOSTROPHE: return "'";
		case KEY_GRAVE: return "`";
		case KEY_LEFT_BRACKET: return "[";
		case KEY_RIGHT_BRACKET: return "]";
		case KEY_EQUAL: return "=";
		case KEY_MINUS: return "-";
		case KEY_LEFT_CONTROL: case KEY_RIGHT_CONTROL: return "Ctrl";
		case KEY_LEFT_ALT: case KEY_RIGHT_ALT: return "Alt";
		case KEY_LEFT_SHIFT: case KEY_RIGHT_SHIFT: return "Shift";
		case KEY_CAPS_LOCK: return "Caps";
		case KEY_BACKSPACE: return "Bksp";
		case KEY_INSERT: return "Ins";
		case KEY_DELETE: return "Del";
		case KEY_HOME: return "Home";
		case KEY_END: return "End";
		case KEY_PAGE_UP: return "PgUp";
		case KEY_PAGE_DOWN: return "PgDn";
		case KEY_UP: return "↑";
		case KEY_DOWN: return "↓";
		case KEY_LEFT: return "←";
		case KEY_RIGHT: return "→";
		default: return k > 0 ? "#" + std::to_string(k) : "";
		}
	}

	std::string Label(Chord c)
	{
		if (c.Empty()) return "—";
		std::string s;
		if (c.mods & ModCtrl) s += "Ctrl+";
		if (c.mods & ModShift) s += "Shift+";
		if (c.mods & ModAlt) s += "Alt+";
		return s + KeyName(c.key);
	}

	std::string Label(Act a) { return Label(Get(a)); }

	void BeginFrame()
	{
		EnsureInit();
		g_blocked = g_capturing || ui::TextFocused() || !ime::Composition().empty();
		g_scriptPrev = g_scriptSeen;
		g_scriptSeen = g_scriptHold;
	}

	bool Blocked() { return g_blocked; }
	void SetCapturing(bool on) { g_capturing = on; }

	bool Pressed(Act a)
	{
		const size_t i = static_cast<size_t>(a);
		if (g_scriptPress[i]) { g_scriptPress[i] = false; return true; }
		if (g_scriptSeen[i] && !g_scriptPrev[i]) return true;
		if (g_blocked) return false;
		const Chord& c = g_bind[i];
		return !c.Empty() && KeyPressed(c.key) && ModsOk(c);
	}

	bool Down(Act a)
	{
		const size_t i = static_cast<size_t>(a);
		if (g_scriptSeen[i]) return true;
		if (g_blocked) return false;
		const Chord& c = g_bind[i];
		return !c.Empty() && KeyDown(c.key) && ModsOk(c);
	}

	bool Released(Act a)
	{
		const size_t i = static_cast<size_t>(a);
		if (!g_scriptSeen[i] && g_scriptPrev[i]) return true;
		if (g_blocked) return false;
		const Chord& c = g_bind[i];
		return !c.Empty() && KeyReleased(c.key);
	}

	Act HotAct(int hot)
	{
		static constexpr Act kHot[12] = {
			Act::BasicAttack,
			Act::Skill1, Act::Skill2, Act::Skill3, Act::Skill4, Act::Skill5, Act::Skill6, Act::Skill7, Act::Skill8,
			Act::Awaken, Act::Mobility, Act::Identity,
		};
		return hot >= 0 && hot < 12 ? kHot[hot] : Act::Count;
	}

	Act ItemAct(int index)
	{
		static constexpr Act kItem[9] = {
			Act::Battle1, Act::Battle2, Act::Battle3, Act::Battle4, Act::Potion, Act::Aux1, Act::Aux2, Act::Aux3, Act::Aux4,
		};
		return index >= 0 && index < 9 ? kItem[index] : Act::Count;
	}

	bool IsModifierKey(int key) { return IsModKey(key); }

	void ScriptPress(Act a) { g_scriptPress[static_cast<size_t>(a)] = true; }
	void ScriptHold(Act a, bool down) { g_scriptHold[static_cast<size_t>(a)] = down; }

	void Load(const std::string& character)
	{
		ResetDefaults();
		g_opt = Options{};
		std::ifstream in("controls_" + character + ".txt");
		if (!in) return;
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::istringstream ss(line);
			std::string kind, id;
			ss >> kind >> id;
			if (kind == "bind")
			{
				int key = 0, mods = 0;
				ss >> key >> mods;
				for (const Row& r : kRows)
					if (id == r.id) { g_bind[static_cast<size_t>(r.act)] = Chord{ key, static_cast<uint8_t>(mods & 7) }; break; }
			}
			else if (kind == "opt")
			{
				int v = 0;
				ss >> v;
				if (id == "swapMouse") g_opt.swapMouse = v != 0;
				else if (id == "castMode") g_opt.castMode = v == 1 ? CastMode::ShowThenCast : CastMode::Quick;
				else if (id == "showGrid") g_opt.showGrid = v != 0;
			}
		}
	}

	void Save(const std::string& character)
	{
		if (character.empty()) return;
		EnsureInit();
		std::ofstream out("controls_" + character + ".txt");
		if (!out) return;
		out << "# 하르벤 조작 설정 — bind <동작> <raylib 키 번호> <보조키 1 Ctrl · 2 Shift · 4 Alt>\n";
		for (const Row& r : kRows)
		{
			const Chord& c = g_bind[static_cast<size_t>(r.act)];
			out << "bind " << r.id << ' ' << c.key << ' ' << static_cast<int>(c.mods) << '\n';
		}
		out << "opt swapMouse " << (g_opt.swapMouse ? 1 : 0) << '\n';
		out << "opt castMode " << (g_opt.castMode == CastMode::ShowThenCast ? 1 : 0) << '\n';
		out << "opt showGrid " << (g_opt.showGrid ? 1 : 0) << '\n';
	}
}
