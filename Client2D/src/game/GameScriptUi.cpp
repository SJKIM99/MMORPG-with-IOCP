#include "game/Game.h"
#include "game/Input.h"
#include "ui/Wm.h"
#include "core/Json.h"
#include "Data/WorldData.h"
#include "Item/ItemTableData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3 시험 시나리오(--script) — 사람과 같은 입력 경로(in::SetScriptMouse → ui 단추 대체)로 누르고 끈다.
//
//   wm        소지품 · 캐릭터 · 퀘스트 일지를 열고 → 캐릭터 창 끌기 → 퀘스트 일지를 왼쪽 가장자리로(자석 0px) →
//             캐릭터 창 눌러 맨 위로 → 채팅창 오른쪽 가장자리로 넓히기 → Esc(맨 위 창부터) → 배치 저장 확인
//   wm2       같은 이름으로 다시 들어와 창을 열고 자리를 찍는다(저장 → 복원 확인)
//   chat3:a|b|c  세 클라이언트: a가 일반 · 전체 · 거래 · 귓속말 · 아이템 고리 · 도배(토큰 버킷) · 되풀이를, b가 /r 답 · 일반을,
//             c가 일반을 보낸다. 받은 줄을 [chat]으로 찍는다(tools/sandbox/chat3.sh가 모아 판정)
//   hudres    HUD 덩어리 · 채팅창 사각형이 서로 겹치는지 · 화면 밖인지 찍는다([hudcheck]) — 해상도마다 --size로
//   chatcfg · chatcfg2  채팅창 설정(탭 · 거르기 · 투명도 · 글자 · 시각 · 차단 · 잠금)을 바꾸고 → 같은 이름으로 다시 들어와 그대로인지.
//             귓속말 이름 완성(Tab) · 조용하면 흐려지기도 여기서
//   uiedit · boss3  UI 편집(미니맵 끌기 · 전투 판 배율) · 보스전(여러 줄 체력바 · 무력화) 녹화
//   hotfeel   기술 칸 손맛 — 재사용 대기 중 누름(눌림 · 붉은 반짝 · 글 안내 1초에 한 번) · 마나 0(자원 부족). 레벨 30 이름(lv30_…)으로
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
				// 1) 아무 창도 없을 때: 채팅창을 오른쪽 가장자리로 120px 넓힌다(지난 시험의 배치는 지우고 기본에서)
				wm::ResetWindows();
				next(9);
			}
			break;
		case 9:
			if (elapsed() > 0.5f)
			{
				const Rectangle ch = wm::LastRect("chat");
				m_scriptFrom = Vector2{ ch.x + ch.width - 2.f, ch.y + ch.height * 0.5f };
				m_scriptTo = Vector2{ m_scriptFrom.x + 120.f, m_scriptFrom.y };
				g_before = ch;
				next(10);
			}
			break;
		case 1:   // wm2: 저장된 자리로 열렸나
			if (elapsed() > 1.0f)
			{
				PrintRect("wm", "inv", wm::LastRect("inv"));
				PrintRect("wm", "char", wm::LastRect("char"));
				PrintRect("wm", "quest", wm::LastRect("quest"));
				PrintRect("wm", "chat", wm::LastRect("chat"));
				std::printf("[wm2] done\n");
				next(99);
			}
			break;
		case 10:
		{
			if (drag(m_scriptFrom, m_scriptTo, 0.5f))
			{
				const Rectangle ch = wm::LastRect("chat");
				PrintRect("wm", "chat-resized", ch);
				check(std::fabs(ch.width - g_before.width - 120.f) < 10.f, "채팅창 오른쪽 가장자리로 120px 넓힘");
				in::ClearScriptMouse();
				// 2) 소지품 · 캐릭터를 연다(같은 프레임 — 목록 순서상 캐릭터가 위)
				m_invOpen = m_charOpen = true;
				next(11);
			}
			break;
		}
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
				const bool ok = json::LoadFile(UiLayoutPath(), root) && root.Get("win") && root.Get("win")->Get("char") && root.Get("win")->Get("chat");
				check(ok, "config/ui_<이름>.json에 창 배치가 저장됨(캐릭터 · 채팅)");
				if (ok)
				{
					const json::Value& c = *root.Get("win")->Get("char");
					std::printf("[wm] saved char dx=%.0f dy=%.0f · chat w=%.0f\n", c.NumOr("dx", 0), c.NumOr("dy", 0), root.Get("win")->Get("chat")->NumOr("w", 0));
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

	if (s.rfind("chat3:", 0) == 0)
	{
		const char role = s.size() > 6 ? s[6] : 'a';
		// 한 줄을 사람처럼 친다: 입력칸 열기 → 0.5초 동안 글자가 들어간다 → Enter
		struct Line { float at; const char* text; bool link; };
		static const Line kA[] = {
			{ 4.0f, "안녕하세요, 일반 대화 시험입니다", false },
			{ 5.0f, "/전 전체 대화 시험", false },
			{ 7.5f, "/거래 체력 물약 삽니다", false },
			{ 8.5f, "/귓 c3b 귓속말 시험", false },
			{ 10.0f, "/일반 이거 어때요?", true },
			{ 16.0f, "같은 말 되풀이 시험", false },
			{ 17.2f, "같은 말 되풀이 시험", false },
			{ 18.4f, "같은 말 되풀이 시험", false },
		};
		static const Line kB[] = {
			{ 11.0f, "/r 받았어요 — 답장 시험", false },
			{ 12.5f, "/일반 b도 일반으로 말합니다", false },
		};
		static const Line kC[] = {
			{ 13.5f, "c입니다 — 같은 지역에서 봅니다", false },
		};
		const Line* lines = role == 'a' ? kA : (role == 'b' ? kB : kC);
		const int n = role == 'a' ? static_cast<int>(std::size(kA)) : (role == 'b' ? static_cast<int>(std::size(kB)) : static_cast<int>(std::size(kC)));
		const int i = m_scriptStep;
		// b의 답(/r)은 귓속말을 받은 뒤에(클라마다 시작 시각이 조금 다르다)
		const bool waitWhisper = role == 'b' && i == 0 && m_lastWhisperFrom.empty();
		if (i < n && !waitWhisper)
		{
			const Line& L = lines[i];
			if (m_scriptT >= L.at - 0.6f && !m_chatOpen)
			{
				OpenChatInput();
				m_chatField.text.clear();
				m_chatLinks.clear();
				if (L.link)
				{
					// 소지품의 첫 장비를 고리로(Shift+클릭과 같은 함수)
					for (const auto& sl : m_inv.slots)
						if (!sl.Empty()) { ChatInsertItemLink(sl.itemId, sl.enchant, static_cast<uint8_t>(SlotRarity(sl))); break; }
				}
			}
			if (m_chatOpen && m_scriptT < L.at)
			{
				// 글자가 하나씩(명령 부분은 한 번에)
				std::vector<int> cps;
				ui::DecodeUtf8(L.text, cps);
				const float u = std::clamp((m_scriptT - (L.at - 0.5f)) / 0.45f, 0.f, 1.f);
				std::string shown;
				for (size_t k = 0; k < static_cast<size_t>(u * static_cast<float>(cps.size())); ++k) ui::AppendUtf8(shown, cps[k]);
				std::string link;
				for (const auto& l : m_chatLinks) link += l.shown + " ";
				m_chatField.text = L.link ? shown + " " + link : shown;
			}
			if (m_scriptT >= L.at)
			{
				std::string link;
				for (const auto& l : m_chatLinks) link += l.shown + " ";
				m_chatField.text = L.link ? std::string(L.text) + " " + link : std::string(L.text);
				SubmitChat();
				ui::Focus(nullptr);
				m_chatOpen = false;
				// 전체 재사용 대기(10초): 같은 프레임에 한 줄 더 — 소프트웨어 렌더링으로 프레임이 느려도 확실히 10초 안
				if (role == 'a' && i == 1)
					SendChatLine(chat::Target{ chat::Ch::World, "" }, "10초 안에 다시 전체(막혀야 한다)");
				++m_scriptStep;
			}
		}
		// a: 도배 시험 — 12초에 일곱 줄을 한 번에(토큰 버킷 5줄 → 뒤의 두 줄은 막힌다). 입력칸 없이 바로
		if (role == 'a' && m_scriptT > 12.f && m_scriptMark < 1.f)
		{
			for (int k = 1; k <= 7; ++k)
				SendChatLine(chat::Target{ chat::Ch::Normal, "" }, "도배 시험 " + std::to_string(k));
			m_scriptMark = 1.f;
		}
		if (m_scriptT > 24.f && m_scriptStep < 1000)
		{
			std::printf("[chat3] done role=%c\n", role);
			m_scriptStep = 1000;
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

	if (s == "chatcfg" || s == "chatcfg2")
	{
		// 채팅창 설정이 재접속 뒤에도 남는가(완료 기준 1) + 귓속말 이름 완성(Tab) + 조용하면 흐려지기.
		//   chatcfg   탭 하나 만들기(전체 · 거래만, 반짝임 끔) · 결사 탭에 시스템 더하기 · 투명도 50/100 · 글자 크게 · 시각 · 흐리게 끔 ·
		//             차단 한 명 · 창 잠금 → 저장 파일 확인. 이름 완성: "/귓 누" → 누리별 → 누렁바위, "/w " → 달빛여우, 차단한 이름은 안 나온다
		//   chatcfg2  같은 이름으로 다시 들어와 위 값이 그대로인지 → 흐리게를 켜고 20초 조용히 → 글자가 흐려졌는지
		const uint32_t kMarket = chat::Bit(chat::Ch::World) | chat::Bit(chat::Ch::Trade);
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT < 1.5f) break;
			if (s == "chatcfg")
			{
				wm::ResetWindows();
				chat::DefaultTabs(m_chatTabs);
				m_chatSet = chat::Settings{};
				chat::Tab t;
				t.name = "장터"; t.user = true; t.flash = false; t.mask = kMarket;
				m_chatTabs.push_back(std::move(t));
				m_chatTab = static_cast<int>(m_chatTabs.size()) - 1;
				m_chatTabs[2].mask |= chat::Bit(chat::Ch::System);
				m_chatSet.idleAlpha = 0.5f; m_chatSet.activeAlpha = 1.f; m_chatSet.font = 2; m_chatSet.clock = true; m_chatSet.fadeText = false;
				m_chatSet.blocked.push_back("도배꾼");
				wm::SetLocked("chat", true);
				wm::MarkDirty();
				// 이름 완성 — 입력칸에 친 것처럼 두고 Tab과 같은 함수를 부른다
				m_lastWhisperFrom = "달빛여우";
				AddChatMsg(chat::Ch::Normal, "누렁바위", 0, "이름 완성 시험 하나", ui::theme::kChatNormal);
				AddChatMsg(chat::Ch::World, "도배꾼", 0, "차단한 이름", ui::theme::kChatWorld);
				AddChatMsg(chat::Ch::World, "누리별", 0, "이름 완성 시험 둘", ui::theme::kChatWorld);
				m_chatField.text = "/귓 누";
				CompleteWhisperName();
				check(m_chatField.text == "/귓 누리별", "Tab: \"/귓 누\" → 가장 최근에 말한 누리별");
				CompleteWhisperName();
				check(m_chatField.text == "/귓 누렁바위", "Tab 한 번 더 → 다음 후보 누렁바위");
				m_chatField.text = "/w ";
				CompleteWhisperName();
				check(m_chatField.text == "/w 달빛여우", "Tab: 빈 이름 → 마지막 귓속말 상대 달빛여우");
				m_chatField.text = "/귓 도";
				check(!CompleteWhisperName() && m_chatField.text == "/귓 도", "차단한 이름(도배꾼)은 후보에 없다");
				m_chatField.text.clear();
				next(1);
			}
			else
			{
				check(m_chatTabs.size() == 7 && m_chatTabs[6].name == "장터" && m_chatTabs[6].mask == kMarket && !m_chatTabs[6].flash && m_chatTabs[6].user,
					"만든 탭(장터 · 전체+거래 · 반짝임 끔)이 그대로");
				check(m_chatTabs.size() > 2 && (m_chatTabs[2].mask & chat::Bit(chat::Ch::System)) != 0, "결사 탭 거르기(+시스템)가 그대로");
				check(m_chatTab == 6, "보던 탭(장터)으로 열림");
				check(std::fabs(m_chatSet.idleAlpha - 0.5f) < 0.01f && std::fabs(m_chatSet.activeAlpha - 1.f) < 0.01f, "투명도(평소 50% · 활성 100%)");
				check(m_chatSet.font == 2 && m_chatSet.clock && !m_chatSet.fadeText, "글자 크게 · 시각 표시 · 흐리게 끔");
				check(ChatBlocked("도배꾼"), "차단 목록");
				check(wm::Locked("chat"), "창 잠금");
				std::printf("[chatcfg2] tabs=%d active=%d idle=%.2f busy=%.2f font=%d clock=%d fade=%d lock=%d\n", static_cast<int>(m_chatTabs.size()), m_chatTab,
					m_chatSet.idleAlpha, m_chatSet.activeAlpha, m_chatSet.font, m_chatSet.clock ? 1 : 0, m_chatSet.fadeText ? 1 : 0, wm::Locked("chat") ? 1 : 0);
				// 흐리게를 켜고 조용히 기다린다(마우스는 창 밖)
				m_chatSet.fadeText = true;
				m_chatActiveAt = m_now;
				in::SetScriptMouse(phys(Vector2{ m_uiW * 0.5f, m_uiH * 0.3f }), false);
				next(2);
			}
			break;
		case 1:   // chatcfg: 자동 저장(2초) 뒤 파일
			if (elapsed() > 3.0f)
			{
				json::Value root;
				const bool ok = json::LoadFile(UiLayoutPath(), root) && root.Get("chat") && root.Get("win") && root.Get("win")->Get("chat");
				check(ok, "config/ui_<이름>.json에 채팅 설정이 저장됨");
				if (ok)
				{
					const json::Value& c = *root.Get("chat");
					const json::Value* tabs = c.Get("tabs");
					check(tabs && tabs->IsArray() && tabs->a.size() == 7 && c.NumOr("active", 0) == 6 && c.NumOr("font", 1) == 2 && c.BoolOr("clock", false) && !c.BoolOr("fade", true),
						"저장 파일: 탭 7 · 보던 탭 6 · 글자 · 시각 · 흐리게");
					check(root.Get("win")->Get("chat")->BoolOr("lock", false), "저장 파일: 채팅창 잠금");
				}
				std::printf("[wmcheck] done pass=%d fail=%d\n", m_checkPass, m_checkFail);
				next(99);
			}
			break;
		case 2:   // chatcfg2: 20초 조용 → 흐려짐
			if (elapsed() > chat::kQuietSec + 3.f)
			{
				std::printf("[chatfade] textA=%.2f (quiet %.0fs)\n", m_chatTextA, elapsed());
				check(m_chatTextA < chat::kQuietTextA + 0.1f, "20초 조용하면 글자가 흐려진다");
				std::printf("[wmcheck] done pass=%d fail=%d\n", m_checkPass, m_checkFail);
				next(99);
			}
			break;
		default:
			break;
		}
		return true;
	}

	if (s == "hotfeel")
	{
		// 기술 칸 손맛(3-C): 훈련장에서 Q를 쓰고 → 재사용 대기 중에 두 번 더 누른다(칸은 누를 때마다 눌림 + 붉은 반짝, 글 안내는 1초에 한 번)
		// → 마나 0에서 W(자원 부족 반짝). 사람과 같은 함수(CastHot)로 누른다.
		// 누름 사이 기다림은 시계(m_now)로 잰다 — 재사용 대기 · 글 안내 1초가 시계 기준이다. 시나리오 시간(프레임마다 최대 0.05초)으로
		// 재면 느린 프레임(넓어진 마을 · 소프트웨어 렌더링 0.3초)에서 0.9초가 5초를 넘어 재사용 대기(6초)가 먼저 풀린다
		static double sayAt = 0.0, markAt = 0.0;
		auto dummy = [this]() -> Entity*
		{
			Entity* best = nullptr;
			float bd = 1e9f;
			for (auto& [id, e] : m_ents)
				if (e.kind == EntityKind::Monster && !e.dead)
				{
					const float d = std::fabs(e.pos.x - static_cast<float>(m_predX)) + std::fabs(e.pos.y - static_cast<float>(m_predY));
					if (d < bd) { bd = d; best = &e; }
				}
			return best;
		};
		const SkillRow* q = HotSkill(1);
		const SkillRow* w = HotSkill(2);
		if (!q || !w) return true;
		const size_t kq = static_cast<size_t>(CdIndex(q->id)), kw = static_cast<size_t>(CdIndex(w->id));
		switch (m_scriptStep)
		{
		case 0:
			// 허수아비 바로 앞(방패 강타 사거리 1 — 두 칸 앞이면 첫 누름이 "다가가서 쓰기"가 되어 재사용 대기가 늦게 시작한다)
			if (m_scriptT > 1.2f) { SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kDummySpots[1].x, WorldData::kDummySpots[1].y - 1 }); next(1); }
			break;
		case 1:
			// 넓어진 데니차(Phase 4)에서는 첫 접속 자리에서 훈련장까지 20초 가까이 걷는다 — 12초에 끊으면 안전지대 한가운데서 누르게 된다
			if (m_goal.kind == GoalKind::None || elapsed() > 30.f)
			{
				if (Entity* d = dummy()) { m_target = d->id; SetScriptAim(d->tx, d->ty, d->id); }
				CastHot(1);
				std::printf("[hotfeel] cast q=%u ready_in=%.2f\n", static_cast<unsigned>(q->id), m_skillReady[kq] - m_now);
				markAt = m_now;
				next(2);
			}
			break;
		case 2:
			if (m_now - markAt > 0.6)
			{
				const double before = m_cdSayAt;
				CastHot(1);
				check(m_skillReady[kq] - m_now > 0.45 && m_now - m_cdDeniedAt[kq] < 0.001 && m_now - m_hotPressAt[1] < 0.001, "재사용 대기 중 누름 → 칸이 눌리고 붉게 반짝");
				check(m_cdSayAt > before, "첫 누름은 글 안내(재사용 대기 중 n초)");
				sayAt = m_cdSayAt;
				markAt = m_now;
				next(3);
			}
			break;
		case 3:
			if (m_now - markAt > 0.3)
			{
				CastHot(1);
				check(m_now - m_cdDeniedAt[kq] < 0.001 && m_now - m_hotPressAt[1] < 0.001, "0.3초 뒤 다시 누름 → 다시 눌림 · 붉은 반짝");
				check(m_cdSayAt == sayAt, "글 안내는 1초에 한 번(0.3초 뒤에는 건너뜀)");
				markAt = m_now;
				next(4);
			}
			break;
		case 4:
			if (m_now - markAt > 1.2)
			{
				if (m_now >= m_skillReady[kw])
				{
					const int mp = m_mp;
					m_mp = 0;
					CastHot(2);
					check(m_now - m_cdDeniedAt[kw] < 0.001 && m_cdSayAt >= m_now - 0.001, "마나 0에서 누름 → 자원 부족 반짝 · 안내(1초가 지났다)");
					m_mp = mp;
				}
				else
					check(false, "W가 재사용 대기 중이라 자원 부족 시험을 못 했다");
				std::printf("[wmcheck] done pass=%d fail=%d\n", m_checkPass, m_checkFail);
				next(99);
			}
			break;
		default:
			break;
		}
		return true;
	}

	if (s == "boss3")
	{
		// Phase 3-C 보스전: 길잡이 → 벨고라 설원 → 모로즈(체력 12,000 = 12줄)에게 기술을 돌려 쓴다(무력화 게이지를 비운다).
		// 레벨 50 캐릭터로(lv50_ 이름). 무력화 · 줄이 바뀔 때 [boss3]을 찍는다.
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 1.0f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, 3 }); next(1); }
			break;
		case 1:
			if (m_dialogNpc == 3) { m_shopMode = ShopMode::Teleport; m_teleportSel = 0; next(2); }
			break;
		case 2:
			if (elapsed() > 0.8f) { m_lastShopSeq = m_net.SendNpcTeleport(3, 0); next(3); }
			break;
		case 3:
			if (Region() == 0) { CloseNpcWindows(); next(4); std::printf("[boss3] t=%.2f arrived\n", m_scriptT); }
			break;
		default:
		{
			if (m_scriptStep != 4) break;
			Entity* boss = nullptr;
			for (auto& [id, e] : m_ents)
				if (e.IsBoss() && !e.dead) { boss = &e; break; }
			if (!boss)
			{
				if (m_goal.kind == GoalKind::None) SetGoal(Goal{ GoalKind::Move, ObjID{}, 263, 265 });
				break;
			}
			m_target = boss->id;
			static int lastLine = -1;
			static double lastBroken = 0.0;
			const int line = static_cast<int>(std::ceil(boss->hp / 1000.0));
			if (line != lastLine) { std::printf("[boss3] t=%.2f hp=%d/%d line=%d stagger=%u\n", m_scriptT, boss->hp, boss->maxhp, line, boss->stagger); lastLine = line; }
			if (boss->brokenUntil > m_now && boss->brokenUntil != lastBroken) { std::printf("[boss3] t=%.2f BROKEN hp=%d\n", m_scriptT, boss->hp); lastBroken = boss->brokenUntil; }
			const int d = std::max(std::abs(boss->tx - m_predX), std::abs(boss->ty - m_predY));
			if (d > 3) { if (m_goal.kind != GoalKind::Attack) SetGoal(Goal{ GoalKind::Attack, boss->id }); break; }
			if (m_hp * 2 < m_maxhp) UseItemSlot(kPotionSlot);
			// 0.3초마다 쓸 수 있는 기술 하나(각성기 V · Q W E R · A S D F 순)
			if (m_scriptT - m_scriptMark < 0.3f) break;
			static const int kOrder[] = { kHotAwaken, 1, 2, 3, 4, 5, 6, 7, 8 };
			for (int hot : kOrder)
			{
				const SkillRow* row = HotSkill(hot);
				if (!row || m_level < row->unlockLevel) continue;
				const int k = CdIndex(row->id);
				if (m_now < m_skillReady[static_cast<size_t>(k)] || m_mp < static_cast<int>(row->mp)) continue;
				SetScriptAim(boss->tx, boss->ty, boss->id);
				CastHot(hot);
				m_scriptMark = m_scriptT;
				break;
			}
			break;
		}
		}
		return true;
	}

	if (s == "hudres")
	{
		// HUD 덩어리 · 채팅창이 서로 겹치는지 · 화면 밖인지(논리 좌표). 화면을 채워 두고 찍는다(해상도마다 --size):
		// 훈련장 허수아비를 대상으로 · 바람걸음 물약(버프 줄) · 획득 알림 하나(시험용 — AddToast) · 채팅 입력칸 열기
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 1.2f)
			{
				SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kDummySpots[1].x, WorldData::kDummySpots[1].y - 3 });
				next(1);
			}
			break;
		case 1:
			if (m_goal.kind == GoalKind::None || elapsed() > 12.f)
			{
				float best = 1e9f;
				for (auto& [id, e] : m_ents)
					if (e.kind == EntityKind::Monster && !e.dead)
					{
						const float d = std::fabs(e.pos.x - static_cast<float>(m_predX)) + std::fabs(e.pos.y - static_cast<float>(m_predY));
						if (d < best) { best = d; m_target = id; }
					}
				for (int i = 0; i < kItemSlots; ++i)   // 바람걸음 물약(버프 줄에 아이콘)
					if (const ItemTableRow* row = ItemTableData::Find(m_quick[static_cast<size_t>(i)].itemId); row && row->effect == ItemEffect::eHaste) { UseItemSlot(i); break; }
				AddToast(m_quick[0].itemId ? m_quick[0].itemId : 1, "획득 알림 자리 — 시험", ui::theme::kGood);
				OpenChatInput("/전 해상도 시험 중입니다");
				next(2);
			}
			break;
		case 2:
			if (elapsed() > 2.0f)
			{
				const char* ids[] = { "hud.me", "hud.target", "hud.minimap", "hud.quest", "hud.combat", "hud.menu", "hud.toast", "chat" };
				std::vector<std::pair<const char*, Rectangle>> rs;
				for (const char* id : ids)
				{
					const Rectangle r = wm::LastRect(id);
					if (r.width > 0.f) rs.emplace_back(id, r);
				}
				int overlaps = 0, outside = 0;
				for (size_t a2 = 0; a2 < rs.size(); ++a2)
				{
					const Rectangle r = rs[a2].second;
					PrintRect("hud", rs[a2].first, r);
					if (r.x < 0.f || r.y < 0.f || r.x + r.width > m_uiW + 0.5f || r.y + r.height > m_uiH + 0.5f) { ++outside; std::printf("[hudcheck] OUTSIDE %s\n", rs[a2].first); }
					for (size_t b2 = a2 + 1; b2 < rs.size(); ++b2)
						if (Overlap(r, rs[b2].second)) { ++overlaps; std::printf("[hudcheck] OVERLAP %s × %s\n", rs[a2].first, rs[b2].second.width > 0 ? rs[b2].first : "?"); }
				}
				std::printf("[hudcheck] screen=%dx%d ui=%.0fx%.0f scale=%.2f chunks=%d overlaps=%d outside=%d\n", m_screenW, m_screenH, m_uiW, m_uiH, S, static_cast<int>(rs.size()), overlaps, outside);
				next(99);
			}
			break;
		default:
			break;
		}
		return true;
	}
	return false;
}
