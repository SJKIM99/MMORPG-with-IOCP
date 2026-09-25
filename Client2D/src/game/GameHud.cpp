#include "game/Game.h"
#include "game/KeyBinds.h"
#include "game/Texts.h"
#include "fx/SkillFx.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "ui/Font.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 전투 HUD 뼈대(Phase 1-A) — 로스트아크식 배치, 「새김」 모양. 최종 모양 · 옮기기는 Phase 3.
//
//   [보조 5~8 / 배틀 1~4] [물약 F1]  [Q W E R / A S D F]  [V 큰 칸] [Space / Z]
//                   └──── HP · MP 막대(위) ────┘
// 조작 설정(Esc 메뉴) · 기술 창(K) · 확장 미니맵(Tab)도 여기에 있다.
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;
using ui::Align;
using ui::ButtonKind;

namespace
{
	std::string Fmt(const char* f, ...)
	{
		char buf[256];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	struct Hud
	{
		Rectangle panel{};
		Rectangle hp{}, mp{}, stats{};
		Rectangle skill[8]{};           // Q W E R / A S D F
		Rectangle awaken{}, mobility{}, identity{};
		Rectangle item[9]{};            // 배틀 1~4 · 물약 · 보조 5~8
	};

	constexpr float kS = 46.f, kG = 5.f;           // 기술 칸 · 간격
	constexpr float kItemS = 40.f, kItemG = 4.f;   // 소모품 칸
	constexpr float kPotS = 50.f;                  // 물약(F1)
	constexpr float kAwakenS = 72.f;               // 각성기(V)
	constexpr float kSmallS = 44.f;                // 이동기 · 아이덴티티
	constexpr float kHpH = 16.f, kMpH = 12.f, kBarGap = 3.f;

	Hud Layout(float W, float H)
	{
		Hud L;
		const float gridW = 4 * kS + 3 * kG, gridH = 2 * kS + kG;
		const float barsH = kHpH + kBarGap + kMpH;
		const float padT = 10.f, padB = 10.f, gapBars = 9.f;
		const float panelH = padT + barsH + gapBars + gridH + padB;
		const float bottom = H - 12.f;
		const float gy = bottom - padB - gridH;
		const float gx = std::round(W * 0.5f - gridW * 0.5f);
		for (int i = 0; i < 8; ++i)
			L.skill[i] = Rectangle{ gx + (i % 4) * (kS + kG), gy + (i / 4) * (kS + kG), kS, kS };
		// 오른쪽: 각성기 큰 칸(가운데 맞춤) → 이동기 · 아이덴티티(위아래)
		const float vx = gx + gridW + 14.f;
		L.awaken = Rectangle{ vx, std::round(gy + (gridH - kAwakenS) * 0.5f), kAwakenS, kAwakenS };
		const float sx = vx + kAwakenS + 10.f;
		L.mobility = Rectangle{ sx, gy, kSmallS, kSmallS };
		L.identity = Rectangle{ sx, gy + gridH - kSmallS, kSmallS, kSmallS };
		// 왼쪽: 물약(F1, 가운데 맞춤) → 배틀 1~4(윗줄) · 보조 5~8(아랫줄)
		const float px = gx - 14.f - kPotS;
		L.item[4] = Rectangle{ px, std::round(gy + (gridH - kPotS) * 0.5f), kPotS, kPotS };
		const float bw = 4 * kItemS + 3 * kItemG;
		const float bx = px - 10.f - bw;
		for (int i = 0; i < 4; ++i)
		{
			L.item[i] = Rectangle{ bx + i * (kItemS + kItemG), gy, kItemS, kItemS };
			L.item[5 + i] = Rectangle{ bx + i * (kItemS + kItemG), gy + gridH - kItemS, kItemS, kItemS };
		}
		const float left = bx - 12.f, right = sx + kSmallS + 12.f;
		L.panel = Rectangle{ left, bottom - panelH, right - left, panelH };
		const float barY = L.panel.y + padT;
		L.hp = Rectangle{ px, barY, right - 12.f - px, kHpH };
		L.mp = Rectangle{ px, barY + kHpH + kBarGap, right - 12.f - px, kMpH };
		L.stats = Rectangle{ bx, barY, bw, barsH };
		return L;
	}

	// 칸 왼쪽 위 단축키 표시(글자 길이에 맞춰 넓어진다: "Spc" "Alt+U")
	void KeyTag(Rectangle slot, const std::string& key, bool active, float scale = 1.f)
	{
		const float kh = std::round(13.f * scale);
		const float fs = std::min(ui::type::kMicro, kh - 3.f);
		const float kw = std::max(kh + 1.f, std::round(ui::Fonts().Measure(key, fs, 0.f, ui::Face::Strong).x + 8.f));
		ui::Keycap(Rectangle{ std::round(slot.x - 4.f), std::round(slot.y - 6.f), kw, kh }, key, active);
	}

	void CooldownSweep(Rectangle r, float frac, unsigned char alpha)
	{
		BeginScissorMode(static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.width), static_cast<int>(r.height));
		DrawCircleSector(Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, r.width * 0.75f, -90.f, -90.f + 360.f * util::Clamp01(frac), 36, Color{ 0, 0, 0, alpha });
		EndScissorMode();
	}
}

float Game::HudTop() const { return Layout(static_cast<float>(m_screenW), static_cast<float>(m_screenH)).panel.y; }
float Game::HudLeft() const { return Layout(static_cast<float>(m_screenW), static_cast<float>(m_screenH)).panel.x; }

// ─────────────────────────────────────────────────────────────────────────────
// 판 · HP · MP
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawCombatPanel()
{
	const Hud L = Layout(static_cast<float>(m_screenW), static_cast<float>(m_screenH));
	ui::Panel(L.panel, 0.94f, false);
	const float pulse = (m_maxhp > 0 && m_hp * 4 < m_maxhp) ? 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 8.f) : 0.f;
	ui::Bar(L.hp, m_hpShown, m_hpLag, util::Mix(ui::theme::kHp, Rgb(0xE0705A), pulse), Color{ 34, 14, 14, 255 },
		Fmt("HP  %d / %d", m_hp, m_maxhp), 13.f);
	const float mpFlash = static_cast<float>(std::max(0.0, 0.5 - (m_now - m_mpFlashAt)) * 2.0);
	ui::Bar(L.mp, m_mpShown, m_mpShown, util::Mix(ui::theme::kMp, Rgb(0x9AC0F0), mpFlash), Color{ 14, 18, 34, 255 },
		Fmt("MP  %d / %d", m_mp, m_maxmp), 11.f);
	if (mpFlash > 0.f)
		ui::Plate(Rectangle{ L.mp.x - 3, L.mp.y - 3, L.mp.width + 6, L.mp.height + 6 }, BLANK, util::WithA(Rgb(0x9AC0F0), mpFlash), 2.f, 1.5f);

	// 능력치 한 줄(소모품 칸 위 — 라벨은 흐리게, 값은 숫자 글꼴)
	const bool magic = m_classId == ClassId::Engraver;
	float sx = L.stats.x + 1.f;
	const float sy = std::round(L.stats.y + (L.stats.height - ui::type::kBodyS) * 0.5f);
	auto stat = [&](const char* label, const std::string& value)
	{
		ui::TextStyle ls; ls.size = ui::type::kCaption; ls.color = ui::theme::kTextDim; ls.face = ui::Face::Body;
		sx += ui::Fonts().Draw(label, Vector2{ sx, sy + 1.f }, ls) + 4.f;
		ui::TextStyle vs; vs.size = ui::type::kBodyS; vs.color = ui::theme::kText; vs.face = ui::Face::Number;
		sx += ui::Fonts().Draw(value, Vector2{ sx, sy }, vs) + 10.f;
	};
	stat(magic ? "마공" : "공격", std::to_string(magic ? m_stats.magicAttack : m_stats.attack));
	stat("방어", std::to_string(m_stats.defense));
	stat("치명", Fmt("%.0f%%", m_stats.critChance / 10.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// 기술 칸
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawHotSlot(Rectangle base, int hot, float keySize)
{
	const std::string key = kb::Label(kb::HotAct(hot));
	const SkillRow* row = HotSkill(hot);
	const bool hover = ui::Hovered(base);
	ui::Block(base);

	// 기술을 끌어다 놓기(기술 창 · 다른 칸에서) — 기본 공격 칸은 고정
	if (hot != kHotBasic && hover && ui::DragReleased() && ui::Drag().kind == kDragSkill)
	{
		const uint16_t id = ui::Drag().itemId;
		const int from = ui::Drag().index;
		if (const SkillRow* dr = SkillTable::Find(id); dr && dr->slot != 0 && dr->cls == m_classId)
		{
			if (from > 0 && from < kHotSlots && from != hot)
				std::swap(m_hot[static_cast<size_t>(from)], m_hot[static_cast<size_t>(hot)]);
			else if (from < 0)
			{
				const int had = HotOfSkill(id);
				if (had > 0 && had != hot) m_hot[static_cast<size_t>(had)] = m_hot[static_cast<size_t>(hot)];
				m_hot[static_cast<size_t>(hot)] = id;
			}
			sfx::Play(sfx::Id::Equip, 0.5f);
			SaveHotbar();
		}
		ui::EndDrag();
		row = HotSkill(hot);
	}

	const bool dropTarget = ui::Dragging() && ui::Drag().kind == kDragSkill && hot != kHotBasic;
	if (!row)
	{
		ui::SlotFrame(base, hover || (dropTarget && hover), dropTarget);
		KeyTag(base, key, false, keySize);
		return;
	}

	const int slot = row->slot;
	const float S = base.width;
	// 누른 순간 살짝 눌렸다가 튀어나온다
	const float press = static_cast<float>(std::max(0.0, 0.14 - (m_now - m_castSentAt[static_cast<size_t>(slot)])) / 0.14);
	const float sc = 1.f - 0.1f * press;
	const Rectangle r{ base.x + S * (1.f - sc) * 0.5f, base.y + S * (1.f - sc) * 0.5f, S * sc, S * sc };
	const bool locked = m_level < row->unlockLevel;
	const bool noMana = !locked && m_mp < static_cast<int>(row->mp);
	const double remain = m_skillReady[static_cast<size_t>(slot)] - m_now;
	const bool cooling = remain > 0.02 && !locked;
	const Color cc = Rgb(row->color);

	ui::Plate(Rectangle{ r.x - 2, r.y - 2, r.width + 4, r.height + 4 }, Color{ 10, 12, 15, 245 }, BLANK, 3.f, 0.f);
	skillfx::DrawSkillIcon(row->id, Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, r.width - 4.f, m_time, locked);
	// 재사용 대기: 시계 방향으로 걷히는 그늘 + 남은 초
	if (cooling)
	{
		CooldownSweep(r, static_cast<float>(remain / std::max(0.05, m_skillCdTotal[static_cast<size_t>(slot)])), 165);
		if (slot != 0 && remain > 0.5)
		{
			ui::TextStyle cs; cs.size = std::round(S * 0.4f); cs.color = ui::theme::kTextHi; cs.outline = 1.5f; cs.outlineColor = Color{ 0, 0, 0, 210 }; cs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(remain >= 10.0 ? Fmt("%.0f", remain) : Fmt("%.1f", remain), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, cs);
		}
	}
	if (noMana)
		ui::Plate(r, Color{ 20, 36, 90, 130 }, BLANK, 2.f, 0.f);
	// 방금 재사용 대기가 끝났다: 반짝 + 퍼지는 테
	const double flash = m_now - m_skillFlash[static_cast<size_t>(slot)];
	if (flash < 0.4)
	{
		const float u = static_cast<float>(flash / 0.4);
		BeginBlendMode(BLEND_ADDITIVE);
		ui::Plate(r, util::WithA(util::Mix(cc, WHITE, 0.4f), 0.45f * (1.f - u)), BLANK, 2.f, 0.f);
		EndBlendMode();
		ui::Plate(Rectangle{ r.x - 5 * u, r.y - 5 * u, r.width + 10 * u, r.height + 10 * u }, BLANK, util::WithA(util::Mix(cc, WHITE, 0.5f), 1.f - u), 3.f, 1.5f);
	}
	// 예약 · 선입력 · 표시 후 시전 중이면 테두리가 맥동. 각성기는 쓸 수 있으면 은은하게 숨 쉰다.
	const bool queued = m_queuedSkill == slot || m_bufferSlot == slot || m_aimingHot == hot;
	const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 12.f);
	Color border = queued ? util::Mix(ui::theme::kDawn, WHITE, 0.3f * pulse) : (hover ? ui::theme::kTextDim : (locked ? ui::theme::kLine : ui::theme::kLineStrong));
	if (!queued && hot == kHotAwaken && !locked && !cooling && !noMana)
	{
		const float breathe = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 2.2f);
		border = util::Mix(ui::theme::kLineStrong, ui::theme::kDawn, 0.35f + 0.45f * breathe);
	}
	if (dropTarget && hover) border = ui::theme::kDawn;
	ui::Plate(r, BLANK, border, 2.f, queued || hot == kHotAwaken ? 2.f : 1.f);
	KeyTag(r, key, queued, keySize);
	if (row->mp > 0 && !locked)
	{
		ui::TextStyle ms; ms.size = 12.f; ms.color = noMana ? Rgb(0xE08A80) : Rgb(0x9DB8E8); ms.outline = 1.2f; ms.face = ui::Face::Number;
		const std::string mp = std::to_string(row->mp);
		ui::Fonts().Draw(mp, Vector2{ r.x + r.width - 3.f - ui::Fonts().Measure(mp, ms.size, 0.f, ui::Face::Number).x, r.y + r.height - 14.f }, ms);
	}
	if (locked)
	{
		ui::TextStyle ls; ls.size = 13.f; ls.color = ui::theme::kTextDim; ls.outline = 1.2f; ls.face = ui::Face::Number;
		ui::Fonts().DrawCentered(Fmt("Lv.%d", row->unlockLevel), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, ls);
	}

	// 끌어서 다른 칸으로(자리 바꾸기)
	if (hot != kHotBasic && ui::Pressed(base))
	{
		m_pressKind = kDragSkill; m_pressIndex = hot; m_pressPos = ui::Mouse();
		ui::ConsumeMouse();
	}
	if (m_pressKind == kDragSkill && m_pressIndex == hot && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
		&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
	{
		ui::BeginDrag(ui::DragPayload{ kDragSkill, hot, row->id });
		m_pressKind = 0;
	}
	if (hover && !ui::Dragging())
	{
		DrawSkillTooltip(*row);
		m_previewSkill = slot;   // 월드에 사거리 · 범위를 보여 준다
	}
}

void Game::DrawSkillBar()
{
	const Hud L = Layout(static_cast<float>(m_screenW), static_cast<float>(m_screenH));
	m_previewSkill = -1;
	for (int i = 0; i < 8; ++i)
		DrawHotSlot(L.skill[i], 1 + i, 1.f);
	DrawHotSlot(L.awaken, kHotAwaken, 1.15f);
	DrawHotSlot(L.mobility, kHotMobility, 1.f);
	DrawHotSlot(L.identity, kHotIdentity, 1.f);
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == kDragSkill)
		m_pressKind = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 소모품 칸(배틀 1~4 · 물약 F1 · 보조 5~8) — 소지품에서 끌어다 등록, 오른쪽 클릭 비우기
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuickBar()
{
	const Hud L = Layout(static_cast<float>(m_screenW), static_cast<float>(m_screenH));
	for (int i = 0; i < kItemSlots; ++i)
	{
		const Rectangle r = L.item[i];
		const QuickSlot& q = m_quick[static_cast<size_t>(i)];
		const bool hover = ui::Hovered(r);
		const bool potion = i == kPotionSlot;
		ui::SlotFrame(r, hover, false, potion ? util::Mix(ui::theme::kHp, ui::theme::kLineStrong, 0.35f) : ui::theme::kBorder);
		ui::Block(r);
		double cdEnd = 0.0, cdLen = 1.0;
		int count = 0;
		if (q.kind == 1 && q.itemId)
		{
			count = m_inv.CountOf(q.itemId);
			icons::Draw(q.itemId, Rectangle{ r.x + 3, r.y + 3, r.width - 6, r.height - 6 }, count > 0 ? WHITE : Color{ 255, 255, 255, 90 });
			const ItemTableRow* row = ItemTableData::Find(q.itemId);
			if (row && row->effect == ItemEffect::eHeal) { cdEnd = m_lastPotionAt + CombatRule::kPotionCooldownMs / 1000.0; cdLen = CombatRule::kPotionCooldownMs / 1000.0; }
			else if (row && row->type == ItemType::eConsumable) { cdEnd = m_lastScrollAt + CombatRule::kScrollCooldownMs / 1000.0; cdLen = CombatRule::kScrollCooldownMs / 1000.0; }
			if (hover && !ui::Dragging()) DrawItemTooltip(q.itemId, 0, count, false, false);
		}
		if (cdEnd > m_now)
		{
			CooldownSweep(r, static_cast<float>((cdEnd - m_now) / cdLen), 150);
			ui::TextStyle cs; cs.size = 14.f; cs.color = ui::theme::kTextHi; cs.outline = 1.2f; cs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(Fmt("%.1f", cdEnd - m_now), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, cs);
		}
		if (count > 0)
		{
			ui::TextStyle ns; ns.size = 13.f; ns.color = ui::theme::kTextHi; ns.outline = 1.2f; ns.face = ui::Face::Number;
			const std::string cstr = std::to_string(count);
			ui::Fonts().Draw(cstr, Vector2{ r.x + r.width - 3.f - ui::Fonts().Measure(cstr, ns.size, 0.f, ui::Face::Number).x, r.y + r.height - 15.f }, ns);
		}
		KeyTag(r, kb::Label(kb::ItemAct(i)), false, potion ? 1.1f : 1.f);

		// 입력: 클릭 사용 / 오른쪽 클릭 비우기 / 끌어서 옮기기
		if (ui::Pressed(r) && q.kind != 0)
		{
			m_pressKind = 2; m_pressIndex = i; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == 2 && m_pressIndex == i && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ 2, i, q.itemId });
			m_pressKind = 0;
		}
		if (ui::Clicked(r) && m_pressKind == 2 && m_pressIndex == i && !ui::Dragging())
		{
			UseItemSlot(i);
			m_pressKind = 0;
		}
		if (ui::Pressed(r, MOUSE_BUTTON_RIGHT) && q.kind != 0)
		{
			m_quick[static_cast<size_t>(i)] = QuickSlot{};
			sfx::Play(sfx::Id::Close, 0.4f);
			ui::ConsumeMouse();
			SaveQuickSlots();
		}
		if (ui::DragReleased() && hover)
		{
			const ui::DragPayload& d = ui::Drag();
			if (d.kind == 1 && d.itemId)
			{
				const ItemTableRow* row = ItemTableData::Find(d.itemId);
				if (row && row->type == ItemType::eConsumable)
				{
					m_quick[static_cast<size_t>(i)] = QuickSlot{ 1, d.itemId };
					sfx::Play(sfx::Id::Equip, 0.5f);
				}
				ui::EndDrag();
				SaveQuickSlots();
			}
			else if (d.kind == 2 && d.index != i && d.index >= 0 && d.index < kItemSlots)
			{
				std::swap(m_quick[static_cast<size_t>(i)], m_quick[static_cast<size_t>(d.index)]);
				ui::EndDrag();
				SaveQuickSlots();
			}
		}
	}
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == 2)
		m_pressKind = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 메뉴(Esc — 열린 창이 없을 때)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawSystemMenu()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	DrawRectangle(0, 0, m_screenW, m_screenH, Color{ 0, 0, 0, 90 });
	const Rectangle r{ std::round(W * 0.5f - 150.f), std::round(H * 0.5f - 150.f), 300.f, 290.f };
	ui::Window(r, "메뉴", &m_menuOpen);
	float y = r.y + 58.f;
	auto item = [&](const char* label, ButtonKind kind)
	{
		const Rectangle b{ r.x + 24.f, y, r.width - 48.f, 40.f };
		y += 48.f;
		return ui::Button(b, label, kind);
	};
	if (item("게임으로 돌아가기", ButtonKind::Primary)) { m_menuOpen = false; sfx::Play(sfx::Id::Close, 0.5f); }
	if (item("조작 설정", ButtonKind::Normal)) { m_menuOpen = false; m_keysOpen = true; sfx::Play(sfx::Id::Open, 0.5f); }
	if (item("도움말", ButtonKind::Normal)) { m_menuOpen = false; m_helpOpen = true; sfx::Play(sfx::Id::Open, 0.5f); }
	if (item("게임 종료", ButtonKind::Danger)) { SaveControls(); m_quit = true; }
}

// ─────────────────────────────────────────────────────────────────────────────
// 조작 설정 — 모든 동작 다시 지정 · 충돌 경고(맞바꾸기) · 기본값 · 캐릭터별 저장
// ─────────────────────────────────────────────────────────────────────────────
void Game::ApplyKeyBind(int act, int key, uint8_t mods, bool swapIfTaken)
{
	if (act < 0 || act >= kb::kActCount) return;
	const kb::Act a = static_cast<kb::Act>(act);
	// 오른쪽 보조키는 왼쪽과 같은 키로 적는다(둘 다 먹는다)
	if (key == KEY_RIGHT_CONTROL) key = KEY_LEFT_CONTROL;
	if (key == KEY_RIGHT_ALT) key = KEY_LEFT_ALT;
	if (key == KEY_RIGHT_SHIFT) key = KEY_LEFT_SHIFT;
	const kb::Chord c{ key, mods };
	m_keysCapture = -1;
	m_keysPendingMod = 0;
	kb::SetCapturing(false);
	const kb::Act other = kb::Conflict(a, c);
	if (other != kb::Act::Count && !swapIfTaken)
	{
		// 이미 다른 동작이 쓰는 키 — 바로 바꾸지 않고 맞바꿀지 묻는다
		m_keysConflict = static_cast<int>(other);
		m_keysConflictFor = act;
		m_keysConflictKey = key;
		m_keysConflictMods = mods;
		sfx::Play(sfx::Id::Error, 0.35f);
		return;
	}
	const kb::Chord prev = kb::Get(a);
	kb::Set(a, c);
	if (other != kb::Act::Count)
	{
		kb::Set(other, prev);
		m_keysNote = Fmt("%s ↔ %s: 키를 맞바꿨습니다.", kb::Name(a), kb::Name(other));
	}
	else
		m_keysNote = Fmt("%s — %s", kb::Name(a), kb::Label(c).c_str());
	m_keysNoteAt = m_now;
	m_keysConflict = -1;
	kb::Save(m_playerName);
	sfx::Play(sfx::Id::Equip, 0.45f);
}

void Game::DrawKeySettings()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	DrawRectangle(0, 0, m_screenW, m_screenH, Color{ 0, 0, 0, 110 });
	const float ww = std::min(W - 32.f, 920.f), wh = std::min(H - 32.f, 628.f);
	const Rectangle r{ std::round((W - ww) * 0.5f), std::round((H - wh) * 0.5f), ww, wh };
	bool open = true;
	ui::Window(r, "조작 설정", &open);
	if (!open)
	{
		m_keysOpen = false;
		m_keysCapture = -1;
		m_keysConflict = -1;
		kb::SetCapturing(false);
		return;
	}

	// ── 새 키 받기(Esc는 HandleHotkeys가 취소로 처리 — Esc 자체는 지정하지 않는다) ──
	if (m_keysCapture >= 0)
	{
		kb::SetCapturing(true);
		for (int k = GetKeyPressed(); k != 0 && m_keysCapture >= 0; k = GetKeyPressed())
		{
			if (k == KEY_ESCAPE) continue;
			if (kb::IsModifierKey(k)) { m_keysPendingMod = k; continue; }
			uint8_t mods = 0;
			if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= kb::ModCtrl;
			if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) mods |= kb::ModShift;
			if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) mods |= kb::ModAlt;
			ApplyKeyBind(m_keysCapture, k, mods, false);
		}
		// 보조키 하나만 눌렀다 떼면 그 키(강제 공격 = Ctrl 같은)
		if (m_keysCapture >= 0 && m_keysPendingMod != 0 && IsKeyReleased(m_keysPendingMod))
			ApplyKeyBind(m_keysCapture, m_keysPendingMod, 0, false);
	}

	// ── 세 칸: 전투 / 소모품 · 기타 / 창 · 상호작용 · 설정 ──
	const float pad = 20.f, colGap = 18.f;
	const float colW = std::floor((r.width - pad * 2.f - colGap * 2.f) / 3.f);
	const float rowH = 26.f, headH = 30.f, chipW = 96.f;
	const float top = r.y + 52.f;
	struct Entry { int act; const char* heading; };   // act < 0 이면 제목
	const auto range = [](kb::Act a, kb::Act b) { return std::pair<int, int>{ static_cast<int>(a), static_cast<int>(b) }; };
	const std::pair<int, int> cols[3][2] = {
		{ range(kb::Act::BasicAttack, kb::Act::ForceAttack), { -1, -1 } },
		{ range(kb::Act::Battle1, kb::Act::Aux4), range(kb::Act::Chat, kb::Act::ZoomOut) },
		{ range(kb::Act::Interact, kb::Act::Auction), { -1, -1 } },
	};
	const char* heads[3][2] = { { "전투", nullptr }, { "소모품", "기타" }, { "창 · 상호작용", nullptr } };
	float colBottom[3]{};
	for (int c = 0; c < 3; ++c)
	{
		const float x = r.x + pad + c * (colW + colGap);
		float y = top;
		for (int g = 0; g < 2; ++g)
		{
			if (cols[c][g].first < 0) continue;
			ui::SectionHeader(Vector2{ x, y + 6.f }, colW, heads[c][g]);
			y += headH;
			for (int i = cols[c][g].first; i <= cols[c][g].second; ++i)
			{
				const kb::Act a = static_cast<kb::Act>(i);
				const Rectangle rowR{ x, y, colW, rowH - 2.f };
				const Rectangle chip{ x + colW - chipW, y + 1.f, chipW, rowH - 4.f };
				const bool capturing = m_keysCapture == i;
				const bool dup = kb::Conflict(a, kb::Get(a)) != kb::Act::Count;
				const bool asked = m_keysConflict == i || m_keysConflictFor == i;
				if (ui::Hovered(rowR))
					ui::Plate(rowR, Color{ 255, 255, 255, 8 }, BLANK, 2.f, 0.f);
				ui::Text(kb::Name(a), Vector2{ x + 4.f, y + 5.f }, ui::type::kBodyS, asked ? ui::theme::kWarn : ui::theme::kText);
				// 키 칩
				const bool hover = ui::Hovered(chip);
				const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 7.f);
				const Color fill = capturing ? Color{ 52, 40, 24, 240 } : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1);
				const Color line = capturing ? util::Mix(ui::theme::kDawn, WHITE, 0.3f * pulse) : (dup ? ui::theme::kBad : (hover ? ui::theme::kTextDim : ui::theme::kLineStrong));
				ui::Plate(chip, fill, line, 3.f, capturing || dup ? 1.5f : 1.f);
				ui::TextStyle ks; ks.size = ui::type::kBodyS; ks.face = ui::Face::Strong;
				ks.color = capturing ? ui::theme::kDawnHi : (kb::Get(a).Empty() ? ui::theme::kTextMute : ui::theme::kTextHi);
				const std::string label = capturing ? std::string("키를 누르세요") : kb::Label(a);
				ui::Fonts().DrawCentered(label, Vector2{ chip.x + chip.width * 0.5f, chip.y + chip.height * 0.5f }, ks);
				if (ui::Clicked(chip))
				{
					m_keysConflict = -1;
					m_keysPendingMod = 0;
					m_keysCapture = capturing ? -1 : i;
					kb::SetCapturing(m_keysCapture >= 0);
					sfx::Play(sfx::Id::Hover, 0.4f);
				}
				if (ui::Pressed(chip, MOUSE_BUTTON_RIGHT))
				{
					kb::Set(a, kb::Chord{});
					kb::Save(m_playerName);
					m_keysNote = Fmt("%s — 비움", kb::Name(a));
					m_keysNoteAt = m_now;
					ui::ConsumeMouse();
				}
				if (hover && dup && !capturing)
					ui::Tooltip({ { "겹친 키", ui::theme::kBad, 14.f, ui::Face::Strong },
						{ Fmt("‘%s’도 이 키를 씁니다.", kb::Name(kb::Conflict(a, kb::Get(a)))), ui::theme::kText, 13.f } });
				y += rowH;
			}
			y += 6.f;
		}
		colBottom[c] = y;
	}

	// ── 설정(세 번째 칸 아래) ──
	{
		const float x = r.x + pad + 2 * (colW + colGap);
		float y = colBottom[2] + 2.f;
		ui::SectionHeader(Vector2{ x, y + 6.f }, colW, "설정");
		y += headH;
		kb::Options& o = kb::Opt();
		auto toggle = [&](const char* name, bool& v)
		{
			ui::Text(name, Vector2{ x + 4.f, y + 5.f }, ui::type::kBodyS, ui::theme::kText);
			const Rectangle b{ x + colW - chipW, y + 1.f, chipW, rowH - 4.f };
			if (ui::Button(b, v ? "켜짐" : "꺼짐", v ? ButtonKind::Primary : ButtonKind::Normal, true, 14.f))
			{
				v = !v;
				kb::Save(m_playerName);
			}
			y += rowH;
		};
		toggle("마우스 좌우 반전", o.swapMouse);
		// 시전 방식: 빠른 시전 / 표시 후 시전
		ui::Text("시전 방식", Vector2{ x + 4.f, y + 5.f }, ui::type::kBodyS, ui::theme::kText);
		{
			const float bw = 62.f;
			const Rectangle b0{ x + colW - bw * 2.f - 4.f, y + 1.f, bw, rowH - 4.f };
			const Rectangle b1{ x + colW - bw, y + 1.f, bw, rowH - 4.f };
			const bool quick = o.castMode == kb::CastMode::Quick;
			if (ui::Button(b0, "빠른", quick ? ButtonKind::Primary : ButtonKind::Normal, true, 14.f)) { o.castMode = kb::CastMode::Quick; kb::Save(m_playerName); }
			if (ui::Button(b1, "표시 후", !quick ? ButtonKind::Primary : ButtonKind::Normal, true, 14.f)) { o.castMode = kb::CastMode::ShowThenCast; kb::Save(m_playerName); }
			const Rectangle hit{ x, y, colW - bw * 2.f - 8.f, rowH };
			if (ui::Hovered(hit))
				ui::Tooltip({ { "시전 방식", ui::theme::kTextHi, 14.f, ui::Face::Strong },
					{ "빠른: 누르는 순간 커서 쪽으로 나간다.", ui::theme::kText, 13.f },
					{ "표시 후: 누르는 동안 범위를 보여 주고 떼면 나간다. 우클릭은 취소.", ui::theme::kText, 13.f } });
		}
		y += rowH;
		toggle("바닥 격자", o.showGrid);
	}

	// ── 아래: 충돌 경고 · 알림 · 버튼 ──
	const float by = r.y + r.height - 50.f;
	if (m_keysConflict >= 0 && m_keysConflictFor >= 0)
	{
		const Rectangle bar{ r.x + pad, by - 44.f, r.width - pad * 2.f, 34.f };
		ui::Plate(bar, Color{ 58, 36, 20, 235 }, ui::theme::kWarn, 3.f, 1.f);
		const std::string what = Fmt("[%s] 키는 이미 ‘%s’에 쓰이고 있습니다. 두 동작의 키를 맞바꿀까요?",
			kb::Label(kb::Chord{ m_keysConflictKey, m_keysConflictMods }).c_str(), kb::Name(static_cast<kb::Act>(m_keysConflict)));
		ui::Text(what, Vector2{ bar.x + 12.f, bar.y + 9.f }, ui::type::kBodyS, ui::theme::kDawnHi);
		const Rectangle yes{ bar.x + bar.width - 190.f, bar.y + 4.f, 92.f, 26.f };
		const Rectangle no{ bar.x + bar.width - 94.f, bar.y + 4.f, 88.f, 26.f };
		if (ui::Button(yes, "맞바꾸기", ButtonKind::Primary, true, 14.f))
			ApplyKeyBind(m_keysConflictFor, m_keysConflictKey, m_keysConflictMods, true);
		if (ui::Button(no, "취소", ButtonKind::Normal, true, 14.f))
			m_keysConflict = -1;
		if (m_keysConflict < 0) m_keysConflictFor = -1;
	}
	else if (m_now - m_keysNoteAt < 3.0 && !m_keysNote.empty())
	{
		const float a = static_cast<float>(std::min(1.0, (3.0 - (m_now - m_keysNoteAt)) / 0.5));
		ui::Text(m_keysNote, Vector2{ r.x + pad, by - 30.f }, ui::type::kBodyS, util::MulA(ui::theme::kGood, a));
	}
	ui::Rule(r.x + 12.f, r.x + r.width - 12.f, by - 8.f, ui::theme::kLine);
	ui::Text("칸을 누르고 새 키를 누르세요 · Esc 취소 · 오른쪽 클릭 비우기 · Esc와 마우스 버튼은 고정 · 캐릭터마다 따로 저장",
		Vector2{ r.x + pad, by + 11.f }, ui::type::kCaption, ui::theme::kTextDim);
	const Rectangle reset{ r.x + r.width - pad - 250.f, by + 2.f, 150.f, 34.f };
	const Rectangle close{ r.x + r.width - pad - 90.f, by + 2.f, 90.f, 34.f };
	if (ui::Button(reset, "기본값으로", ButtonKind::Normal, true, 15.f))
	{
		kb::ResetDefaults();
		kb::Opt() = kb::Options{};
		kb::Save(m_playerName);
		m_keysCapture = -1;
		m_keysConflict = -1;
		m_keysConflictFor = -1;
		kb::SetCapturing(false);
		m_keysNote = "기본값으로 되돌렸습니다.";
		m_keysNoteAt = m_now;
		sfx::Play(sfx::Id::Close, 0.5f);
	}
	if (ui::Button(close, "닫기", ButtonKind::Primary, true, 15.f))
	{
		m_keysOpen = false;
		m_keysCapture = -1;
		m_keysConflict = -1;
		m_keysConflictFor = -1;
		kb::SetCapturing(false);
		sfx::Play(sfx::Id::Close, 0.5f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 기술 창(K) — 직업 기술 · 놓인 칸 · 끌어서 칸 바꾸기(성장 · 변형은 Phase 2-D)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawSkillWindow()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float wh = std::min(HudTop() - 24.f, 520.f);
	const Rectangle r{ std::round(std::max(12.f, W * 0.5f - 470.f)), std::round(std::max(12.f, std::min(H * 0.5f - 280.f, HudTop() - 12.f - wh))), 560.f, wh };
	ui::Window(r, "기술", &m_skillWinOpen);
	const ClassRow& cls = ClassTable::Get(m_classId);
	ui::Text(Fmt("%s · %s", cls.name, cls.role), Vector2{ r.x + r.width - 48.f, r.y + 15.f }, ui::type::kCaption, util::Shade(Rgb(cls.color), 1.1f), Align::Right);
	float y = r.y + 50.f;
	const float rowH = 66.f;
	for (int i = 0; i < 6; ++i)
	{
		const SkillRow* row = SkillTable::Find(cls.skills[i]);
		if (!row) continue;
		const Rectangle rr{ r.x + 12.f, y, r.width - 24.f, rowH - 4.f };
		const bool hover = ui::Hovered(rr);
		const bool locked = m_level < row->unlockLevel;
		ui::Plate(rr, hover ? ui::theme::kSurface2 : ui::theme::kSurface1, ui::theme::kLine, 3.f, 1.f);
		const Rectangle icon{ rr.x + 8.f, rr.y + 7.f, 48.f, 48.f };
		ui::Plate(Rectangle{ icon.x - 2, icon.y - 2, icon.width + 4, icon.height + 4 }, Color{ 10, 12, 15, 245 }, ui::theme::kLineStrong, 3.f, 1.f);
		skillfx::DrawSkillIcon(row->id, Vector2{ icon.x + 24.f, icon.y + 24.f }, 44.f, m_time, locked);
		// 이름 · 놓인 칸
		{
			ui::TextStyle ns; ns.size = ui::type::kHeading; ns.color = locked ? ui::theme::kTextDim : util::Mix(Rgb(row->color), ui::theme::kTextHi, 0.45f); ns.face = ui::Face::Strong;
			const float nw = ui::Fonts().Draw(row->name, Vector2{ icon.x + 62.f, rr.y + 8.f }, ns);
			std::string where;
			if (row->slot == 0) where = kb::Label(kb::Act::BasicAttack) + " · 우클릭";
			else if (const int h = HotOfSkill(row->id); h > 0) where = kb::Label(kb::HotAct(h));
			const float kx = icon.x + 62.f + nw + 10.f;
			if (!where.empty())
			{
				const float kw = std::round(ui::Fonts().Measure(where, ui::type::kMicro, 0.f, ui::Face::Strong).x + 10.f);
				ui::Keycap(Rectangle{ kx, rr.y + 9.f, kw, 16.f }, where, true);
			}
			else
				ui::Text("칸에 없음", Vector2{ kx, rr.y + 10.f }, ui::type::kCaption, ui::theme::kTextMute);
		}
		std::string meta = locked ? Fmt("Lv.%d에 배움", row->unlockLevel) : std::string();
		if (row->mp > 0) meta += (meta.empty() ? "" : " · ") + Fmt("MP %d", row->mp);
		if (row->slot != 0) meta += (meta.empty() ? "" : " · ") + Fmt("재사용 %.1f초", SkillTable::CooldownMs(*row, m_stats.cooldownReduction) / 1000.f);
		if (row->range > 0) meta += (meta.empty() ? "" : " · ") + Fmt("사거리 %d칸", row->range);
		ui::Text(meta, Vector2{ rr.x + rr.width - 10.f, rr.y + 10.f }, ui::type::kCaption, locked ? ui::theme::kBad : ui::theme::kInfo, Align::Right);
		ui::TextWrapped(row->desc, Rectangle{ icon.x + 62.f, rr.y + 31.f, rr.width - 80.f, 30.f }, ui::type::kCaption, ui::theme::kText, 3.f);
		// 끌기(기본 공격은 고정)
		if (row->slot != 0 && ui::Pressed(icon))
		{
			m_pressKind = kDragSkill; m_pressIndex = -1 - i; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == kDragSkill && m_pressIndex == -1 - i && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ kDragSkill, -1, row->id });
			m_pressKind = 0;
		}
		if (hover && !ui::Dragging())
		{
			DrawSkillTooltip(*row);
			m_previewSkill = row->slot;
		}
		y += rowH;
	}
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == kDragSkill)
		m_pressKind = 0;
	ui::Rule(r.x + 12.f, r.x + r.width - 12.f, y + 4.f, ui::theme::kLine);
	ui::TextWrapped("아이콘을 끌어 아래 가운데 기술 칸(각성기 · 이동기 · 아이덴티티 칸 포함)에 놓으면 자리를 바꿉니다. 기술 레벨 · 변형은 다음 단계에서 열립니다.",
		Rectangle{ r.x + 18.f, y + 14.f, r.width - 190.f, 40.f }, ui::type::kCaption, ui::theme::kTextDim, 3.f);
	if (ui::Button(Rectangle{ r.x + r.width - 160.f, y + 14.f, 142.f, 32.f }, "기본 배치로", ButtonKind::Normal, true, 14.f))
	{
		InitHotbar(false);
		SaveHotbar();
		sfx::Play(sfx::Id::Equip, 0.5f);
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 확장 미니맵(Tab) — 화면 가운데 큰 반투명 지도. 창이 아니라서 클릭은 월드로 간다(달리면서 본다).
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawBigMap()
{
	const float W = static_cast<float>(m_screenW);
	// 마름모(2:1)가 화면 가운데 — 전투 HUD 위 공간에 맞춘다
	const float mw = std::round(std::min(W * 0.72f, (HudTop() - 96.f) * 2.f));
	const float mh = std::round(mw * 0.5f);
	const Rectangle m{ std::round(W * 0.5f - mw * 0.5f), std::round(std::max(48.f, (HudTop() - mh) * 0.5f + 4.f)), mw, mh };
	// 뒤판: 먹빛 마름모(밝은 들판 위에서도 읽히게)
	{
		const Vector2 t{ m.x + mw * 0.5f, m.y - 8.f }, rgt{ m.x + mw + 16.f, m.y + mh * 0.5f }, b{ m.x + mw * 0.5f, m.y + mh + 8.f }, lft{ m.x - 16.f, m.y + mh * 0.5f };
		const Color ink = util::WithA(ui::theme::kInk, 0.55f);
		DrawTriangle(t, lft, b, ink);
		DrawTriangle(t, b, rgt, ink);
		DrawLineEx(t, rgt, 1.f, util::WithA(ui::theme::kLineStrong, 0.7f));
		DrawLineEx(rgt, b, 1.f, util::WithA(ui::theme::kLineStrong, 0.7f));
		DrawLineEx(b, lft, 1.f, util::WithA(ui::theme::kLineStrong, 0.7f));
		DrawLineEx(lft, t, 1.f, util::WithA(ui::theme::kLineStrong, 0.7f));
	}
	DrawMinimapAt(m, 56, 0.82f, true);
	const auto* region = WorldData::FindRegion(Region());
	const std::string place = InTown() ? std::string(WorldData::kTownName) : (region ? region->name : "");
	ui::TextStyle ps; ps.size = ui::type::kDisplayS; ps.color = util::WithA(ui::theme::kTextHi, 0.9f); ps.face = ui::Face::Display; ps.spacing = 0.6f;
	ps.outline = 1.5f; ps.outlineColor = Color{ 0, 0, 0, 160 };
	ui::Fonts().DrawCentered(place, Vector2{ W * 0.5f, m.y - 30.f }, ps);
	ui::Text(Fmt("%s 닫기", kb::Label(kb::Act::BigMap).c_str()), Vector2{ W * 0.5f, m.y + m.height + 14.f }, ui::type::kCaption, util::WithA(ui::theme::kTextDim, 0.85f), Align::Center, true);
}
