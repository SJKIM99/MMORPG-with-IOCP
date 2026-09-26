#include "game/Game.h"
#include "game/KeyBinds.h"
#include "ui/Wm.h"
#include "audio/Sfx.h"
#include "core/Json.h"

#include <algorithm>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 창 관리(Phase 3-A) — 창 목록 · Esc · UI 배율 · 배치 저장(config/ui_<캐릭터>.json)
//
//   · 창마다 id가 하나(wm). 그리는 함수 안의 wm::Window(id, 기본 사각형 …)가 이번 프레임 사각형을 준다.
//   · 겹침 순서는 wm이 정한다(누른 창 · 새로 연 창이 맨 위). Esc는 맨 위 열린 창부터.
//   · 닫힐 때 0.12초 동안 한 번 더 그리는 "유령"은 판정이 없다(ui::SetInert) — 그동안만 열림 표시를 켜 둔다.
// ─────────────────────────────────────────────────────────────────────────────

float Game::EffectiveUiScale() const
{
	const int opt = kb::Opt().uiScale;
	if (opt >= 100) return opt / 100.f;
	// 자동: 화면 높이로(1080p까지 100% — 1600×900 기준 배치, 1440p 125%, 1800p 150%, 4K 200%)
	const int h = m_screenH;
	if (h < 1200) return 1.f;
	if (h < 1600) return 1.25f;
	if (h < 2000) return 1.5f;
	return 2.f;
}

bool Game::ModalOpen() const
{
	return m_menuOpen || m_keysOpen || m_discard.open || m_dead || m_enchant.stage >= 2;
}

std::string Game::UiLayoutPath() const
{
	return "config/ui_" + m_playerName + ".json";
}

void Game::LoadUiLayout()
{
	if (m_playerName.empty()) return;
	json::Value root;
	chat::DefaultTabs(m_chatTabs);
	m_chatTab = 0;
	m_chatSet = chat::Settings{};
	if (json::LoadFile(UiLayoutPath(), root) && root.IsObject())
	{
		wm::FromJson(root);
		if (const json::Value* c = root.Get("chat")) chat::FromJson(*c, m_chatTabs, m_chatTab, m_chatSet);
	}
	m_chatBgA = m_chatSet.idleAlpha;
	wm::ClearDirty();
}

void Game::SaveUiLayout()
{
	if (m_playerName.empty() || !m_hasSelf) return;
	json::Value root = json::Value::Obj();
	root.Set("v", json::Value::Num(1));
	wm::ToJson(root);
	root.Set("chat", chat::ToJson(m_chatTabs, m_chatTab, m_chatSet));
	if (json::SaveFile(UiLayoutPath(), root))
		wm::ClearDirty();
}

void Game::DrawWindows()
{
	// 닫히는 창의 유령: 그리는 동안만 열림 표시를 켠다(판정이 없으니 그 사이에 바뀌지 않는다)
	auto ghost = [this](bool& flag, void (Game::*fn)())
	{
		return [this, &flag, fn]
		{
			const bool was = flag;
			flag = true;
			(this->*fn)();
			if (!was) flag = false;
		};
	};
	std::vector<wm::Entry> e;
	e.reserve(16);
	if (!m_uiEdit)
	{
		e.push_back({ "inv", m_invOpen, ghost(m_invOpen, &Game::DrawInventory) });
		e.push_back({ "char", m_charOpen, ghost(m_charOpen, &Game::DrawCharacter) });
		e.push_back({ "quest", m_questLogOpen, ghost(m_questLogOpen, &Game::DrawQuestLog) });
		e.push_back({ "skill", m_skillWinOpen, ghost(m_skillWinOpen, &Game::DrawSkillWindow) });
		e.push_back({ "map", m_mapOpen, ghost(m_mapOpen, &Game::DrawWorldMap) });
		e.push_back({ "help", m_helpOpen, ghost(m_helpOpen, &Game::DrawHelp) });
		// NPC 창들은 닫히면 상태(대화 상대)가 사라진다 — 닫기 애니메이션 없이
		e.push_back({ "npc", m_dialogNpc != 0 && m_shopMode == ShopMode::None, [this] { DrawNpcDialog(); }, false });
		e.push_back({ "shop", m_shopMode == ShopMode::Buy, [this] { DrawShop(); }, false });
		e.push_back({ "sell", m_shopMode == ShopMode::Sell, [this] { DrawSell(); }, false });
		e.push_back({ "tele", m_shopMode == ShopMode::Teleport, [this] { DrawTeleport(); }, false });
	}
	e.push_back({ "chat", true, [this] { DrawChat(); }, false });
	e.push_back({ "chatmenu", m_chatMenu != 0, [this] { DrawChatMenu(); }, false });   // 채팅 이름 메뉴 · 탭 설정(맨 위)
	wm::DrawWindows(e);
}

void Game::CloseWindowById(const char* id)
{
	const std::string s = id ? id : "";
	auto closed = [](bool& flag) { flag = false; sfx::Play(sfx::Id::Close, 0.5f); };
	if (s == "inv") closed(m_invOpen);
	else if (s == "char") closed(m_charOpen);
	else if (s == "quest") closed(m_questLogOpen);
	else if (s == "skill") closed(m_skillWinOpen);
	else if (s == "map") closed(m_mapOpen);
	else if (s == "help") closed(m_helpOpen);
	else if (s == "npc")
	{
		if (m_dialogQuest != 0 && m_dialogNpc != 0) { m_dialogQuest = 0; m_dialogTextAt = m_now; }   // 이야기 → NPC 첫 화면
		else CloseNpcWindows();
	}
	else if (s == "shop" || s == "sell" || s == "tele") CloseNpcWindows();
}
