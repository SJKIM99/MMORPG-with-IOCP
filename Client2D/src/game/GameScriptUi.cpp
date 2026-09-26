#include "game/Game.h"
#include "game/Input.h"
#include "ui/Wm.h"
#include "core/Json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3 시험 시나리오(--script) — 사람과 같은 입력 경로(in::SetScriptMouse → ui 단추 대체)로 누르고 끈다.
//
//   wm        소지품 · 캐릭터를 열고 → 캐릭터 창 끌기 → 퀘스트 일지를 열어 왼쪽 가장자리로(자석 0px) →
//             캐릭터 창 눌러 맨 위로 → Esc(맨 위 창부터) → 배치 저장 확인
//   wm2       같은 이름으로 다시 들어와 창을 열고 자리를 찍는다(저장 → 복원 확인)
//   uiedit    UI 편집: 미니맵 끌기 · 전투 판 휠 110% · 저장
//   hudres    HUD 덩어리 · 채팅창 사각형이 서로 겹치는지 · 화면 밖인지 찍는다([hudcheck]) — 해상도마다 --size로
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
	Rectangle g_before{};   // 끌기 전 사각형(단계를 넘길 때 잡는다)
	Vector2 Lerp(Vector2 a, Vector2 b, float u) { return Vector2{ a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u }; }
	bool Overlap(Rectangle a, Rectangle b)
	{
		return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
	}
	void PrintRect(const char* tag, const char* id, Rectangle r)
	{
		std::printf("[%s] %s %.0f %.0f %.0f %.0f\n", tag, id, r.x, r.y, r.width, r.height);
	}
}

bool Game::RunScriptUi(float dt)
{
	(void)dt;
	const std::string& s = m_opts.script;
	const float S = ui::UiScale();
	auto phys = [S](Vector2 p) { return Vector2{ p.x * S, p.y * S }; };
	auto elapsed = [this] { return m_scriptT - m_scriptMark; };
	auto next = [this](int step) { m_scriptStep = step; m_scriptMark = m_scriptT; };
	// 끌기: 올려 두기 0.15초 → 누름 0.15초 → dur초 동안 옮김 → 0.15초 멈춤 → 뗌 0.2초. 끝나면 true
	auto drag = [&](Vector2 a, Vector2 b, float dur) -> bool
	{
		const float e = elapsed();
		if (e < 0.15f) { in::SetScriptMouse(phys(a), false); return false; }
		if (e < 0.30f) { in::SetScriptMouse(phys(a), true); return false; }
		if (e < 0.30f + dur) { in::SetScriptMouse(phys(Lerp(a, b, (e - 0.30f) / dur)), true); return false; }
		if (e < 0.45f + dur) { in::SetScriptMouse(phys(b), true); return false; }
		if (e < 0.65f + dur) { in::SetScriptMouse(phys(b), false); return false; }
		return true;
	};
	auto check = [this](bool ok, const char* what)
	{
		std::printf("[wmcheck] %s %s\n", ok ? "PASS" : "FAIL", what);
		if (ok) ++m_checkPass; else ++m_checkFail;
	};

	if (s == "wm" || s == "wm2")
	{
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 1.2f)
			{
				if (s == "wm2")
				{
					m_invOpen = m_charOpen = m_questLogOpen = true;
					next(1);
					break;
				}
				// 지난 시험의 배치는 지우고 기본에서
				wm::ResetWindows();
				next(9);
			}
			break;
		case 9:
			if (elapsed() > 0.5f)
			{
				// 1) 소지품 · 캐릭터를 연다(같은 프레임 — 목록 순서상 캐릭터가 위)
				m_invOpen = m_charOpen = true;
				next(11);
			}
			break;
		case 11:
			if (elapsed() > 0.8f)
			{
				const char* top = wm::TopOpen();
				check(top && std::string(top) == "char", "새로 연 창이 맨 위(캐릭터)");
				const Rectangle c = wm::LastRect("char");
				PrintRect("wm", "char", c);
				m_scriptFrom = Vector2{ c.x + 90.f, c.y + 18.f };
				m_scriptTo = Vector2{ m_scriptFrom.x + 260.f, m_scriptFrom.y + 70.f };
				g_before = c;
				next(12);
			}
			break;
		case 12:
		{
			if (drag(m_scriptFrom, m_scriptTo, 0.6f))
			{
				const Rectangle a = wm::LastRect("char");
				PrintRect("wm", "char-moved", a);
				// 자석(8px)이 붙을 수 있다 — 옮긴 거리 ±9px
				check(std::fabs((a.x - g_before.x) - 260.f) < 9.5f && std::fabs((a.y - g_before.y) - 70.f) < 9.5f, "제목줄 끌기로 캐릭터 창이 (260, 70) 옮겨짐(자석 ±8px)");
				in::ClearScriptMouse();
				// 3) 퀘스트 일지를 연다 → 맨 위
				m_questLogOpen = true;
				next(13);
			}
			break;
		}
		case 13:
			if (elapsed() > 0.8f)
			{
				const char* top = wm::TopOpen();
				check(top && std::string(top) == "quest", "나중에 연 퀘스트 일지가 맨 위");
				const Rectangle q = wm::LastRect("quest");
				m_scriptFrom = Vector2{ q.x + 120.f, q.y + 18.f };
				m_scriptTo = Vector2{ m_scriptFrom.x - q.x + 6.f, m_scriptFrom.y + 20.f };   // 왼쪽 가장자리 6px 앞까지
				next(14);
			}
			break;
		case 14:
			if (drag(m_scriptFrom, m_scriptTo, 0.5f))
			{
				const Rectangle q = wm::LastRect("quest");
				PrintRect("wm", "quest-snapped", q);
				check(q.x == 0.f, "화면 가장자리 8px 안에서 자석(퀘스트 일지 x = 0)");
				// 4) 캐릭터 창에서 퀘스트 일지 · 소지품에 가리지 않은 곳을 한 번 누른다 → 맨 위로
				const Rectangle c = wm::LastRect("char");
				m_scriptFrom = Vector2{ std::max(q.x + q.width + 30.f, c.x + 40.f), c.y + c.height - 30.f };
				next(15);
			}
			break;
		case 15:
		{
			const float e = elapsed();
			if (e < 0.15f) in::SetScriptMouse(phys(m_scriptFrom), false);
			else if (e < 0.30f) in::SetScriptMouse(phys(m_scriptFrom), true);
			else if (e < 0.50f) in::SetScriptMouse(phys(m_scriptFrom), false);
			else
			{
				const char* top = wm::TopOpen();
				check(top && std::string(top) == "char", "누른 창(캐릭터)이 맨 위");
				in::ClearScriptMouse();
				next(16);
			}
			break;
		}
		case 16:
			if (elapsed() > 0.3f)
			{
				CloseTopWindow();   // Esc와 같은 경로
				check(!m_charOpen && m_invOpen && m_questLogOpen, "Esc는 맨 위 창(캐릭터)부터 닫는다");
				next(17);
			}
			break;
		case 17:
			if (elapsed() > 2.8f)
			{
				json::Value root;
				const bool ok = json::LoadFile(UiLayoutPath(), root) && root.Get("win") && root.Get("win")->Get("char");
				check(ok, "config/ui_<이름>.json에 창 배치가 저장됨(캐릭터)");
				if (ok)
				{
					const json::Value& c = *root.Get("win")->Get("char");
					std::printf("[wm] saved char dx=%.0f dy=%.0f\n", c.NumOr("dx", 0), c.NumOr("dy", 0));
				}
				PrintRect("wm", "final-inv", wm::LastRect("inv"));
				PrintRect("wm", "final-quest", wm::LastRect("quest"));
				PrintRect("wm", "final-chat", wm::LastRect("chat"));
				std::printf("[wmcheck] done pass=%d fail=%d\n", m_checkPass, m_checkFail);
				next(99);
			}
			break;
		default:
			break;
		}
		return true;
	}

	if (s == "uiedit")
	{
		// UI 편집: 미니맵을 끌어 옮기고 전투 판을 휠로 110% → 끝내고 저장 확인
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 1.5f) { wm::ResetHud(); m_uiEdit = true; next(1); }
			break;
		case 1:
			if (elapsed() > 0.8f)
			{
				const Rectangle mm = wm::LastRect("hud.minimap");
				g_before = mm;
				m_scriptFrom = Vector2{ mm.x + mm.width * 0.5f, mm.y + mm.height * 0.5f };
				m_scriptTo = Vector2{ m_scriptFrom.x - 300.f, m_scriptFrom.y + 90.f };
				next(2);
			}
			break;
		case 2:
			if (drag(m_scriptFrom, m_scriptTo, 0.6f))
			{
				const Rectangle mm = wm::LastRect("hud.minimap");
				PrintRect("uiedit", "minimap-moved", mm);
				check(std::fabs((mm.x - g_before.x) + 300.f) < 9.5f && std::fabs((mm.y - g_before.y) - 90.f) < 9.5f, "편집 모드: 미니맵을 (−300, 90) 옮김");
				const Rectangle cb = wm::LastRect("hud.combat");
				g_before = cb;
				in::SetScriptMouse(phys(Vector2{ cb.x + cb.width * 0.5f, cb.y + cb.height * 0.5f }), false);
				next(3);
			}
			break;
		case 3:
			if (elapsed() > 0.3f && elapsed() < 0.9f && m_scriptMark < 100.f) { ui::SetWheelOverride(1.f); m_scriptMark = m_scriptT + 1000.f; }
			break;
		default:
			break;
		}
		if (m_scriptStep == 3 && m_scriptMark > 500.f && m_scriptT > m_scriptMark - 1000.f + 0.35f)
		{
			// 두 번째 휠
			ui::SetWheelOverride(1.f);
			m_scriptMark = m_scriptT;
			m_scriptStep = 4;
		}
		else if (m_scriptStep == 4 && elapsed() > 1.2f)
		{
			const Rectangle cb = wm::LastRect("hud.combat");
			PrintRect("uiedit", "combat-scaled", cb);
			check(std::fabs(cb.width / std::max(1.f, g_before.width) - 1.10f) < 0.02f, "편집 모드: 휠 두 번 → 전투 판 110%");
			in::ClearScriptMouse();
			next(5);
		}
		else if (m_scriptStep == 5 && elapsed() > 1.5f)
		{
			m_uiEdit = false;   // 완료
			next(6);
		}
		else if (m_scriptStep == 6 && elapsed() > 3.0f)
		{
			json::Value root;
			const bool ok = json::LoadFile(UiLayoutPath(), root) && root.Get("hud") && root.Get("hud")->Get("hud.minimap") && root.Get("hud")->Get("hud.combat");
			check(ok, "HUD 배치가 저장됨(미니맵 · 전투 판)");
			if (ok) std::printf("[uiedit] saved combat k=%.2f · minimap dx=%.0f dy=%.0f\n", root.Get("hud")->Get("hud.combat")->NumOr("k", 1), root.Get("hud")->Get("hud.minimap")->NumOr("dx", 0), root.Get("hud")->Get("hud.minimap")->NumOr("dy", 0));
			std::printf("[wmcheck] done pass=%d fail=%d\n", m_checkPass, m_checkFail);
			next(99);
		}
		return true;
	}

	if (s == "hudres")
	{
		// HUD 덩어리 · 채팅창이 서로 겹치는지 · 화면 밖인지(논리 좌표). 대상 창은 가까운 허수아비를 골라 띄운다
		if (m_scriptStep == 0 && m_scriptT > 1.5f)
		{
			float best = 1e9f;
			for (auto& [id, e] : m_ents)
				if (e.kind == EntityKind::Monster && !e.dead)
				{
					const float d = std::fabs(e.pos.x - static_cast<float>(m_predX)) + std::fabs(e.pos.y - static_cast<float>(m_predY));
					if (d < best) { best = d; m_target = id; }
				}
			next(1);
		}
		else if (m_scriptStep == 1 && elapsed() > 2.5f)
		{
			const char* ids[] = { "hud.me", "hud.buffs", "hud.target", "hud.minimap", "hud.quest", "hud.combat", "hud.menu", "hud.toast", "chat" };
			std::vector<std::pair<const char*, Rectangle>> rs;
			for (const char* id : ids)
			{
				const Rectangle r = wm::LastRect(id);
				if (r.width > 0.f) rs.emplace_back(id, r);
			}
			int overlaps = 0, outside = 0;
			for (size_t a = 0; a < rs.size(); ++a)
			{
				const Rectangle r = rs[a].second;
				PrintRect("hud", rs[a].first, r);
				if (r.x < 0.f || r.y < 0.f || r.x + r.width > m_uiW + 0.5f || r.y + r.height > m_uiH + 0.5f) { ++outside; std::printf("[hudcheck] OUTSIDE %s\n", rs[a].first); }
				for (size_t b = a + 1; b < rs.size(); ++b)
					if (Overlap(r, rs[b].second)) { ++overlaps; std::printf("[hudcheck] OVERLAP %s × %s\n", rs[a].first, rs[b].first); }
			}
			std::printf("[hudcheck] screen=%dx%d ui=%.0fx%.0f scale=%.2f chunks=%d overlaps=%d outside=%d\n", m_screenW, m_screenH, m_uiW, m_uiH, S, static_cast<int>(rs.size()), overlaps, outside);
			next(99);
		}
		return true;
	}
	return false;
}
