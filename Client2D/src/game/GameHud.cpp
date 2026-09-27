#include "game/Game.h"
#include "ui/Wm.h"
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
//   [보조 5~8 / 전투 1~4] [물약 F1]  [Q W E R / A S D F]  [V 큰 칸] [Space / Z]
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
		Rectangle buffs{};              // Phase 3-C: HP 막대 위 버프(왼쪽) · 약화(오른쪽) 줄
		Rectangle skill[8]{};           // Q W E R / A S D F
		Rectangle awaken{}, mobility{}, identity{};
		Rectangle item[9]{};            // 전투 1~4 · 물약 · 보조 5~8
	};

	constexpr float kS = 46.f, kG = 5.f;           // 기술 칸 · 간격
	constexpr float kItemS = 40.f, kItemG = 4.f;   // 소모품 칸
	constexpr float kPotS = 50.f;                  // 물약(F1)
	constexpr float kAwakenS = 72.f;               // 각성기(V)
	constexpr float kSmallS = 44.f;                // 이동기 · 고유 기술
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
		// 오른쪽: 각성기 큰 칸(가운데 맞춤) → 이동기 · 고유 기술(위아래)
		const float vx = gx + gridW + 14.f;
		L.awaken = Rectangle{ vx, std::round(gy + (gridH - kAwakenS) * 0.5f), kAwakenS, kAwakenS };
		const float sx = vx + kAwakenS + 10.f;
		L.mobility = Rectangle{ sx, gy, kSmallS, kSmallS };
		L.identity = Rectangle{ sx, gy + gridH - kSmallS, kSmallS, kSmallS };
		// 왼쪽: 물약(F1, 가운데 맞춤) → 전투 1~4(윗줄) · 보조 5~8(아랫줄)
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
		// 버프 줄: 판 위 — 고유 기술 게이지(판 위 15px) 위에 아이콘 26px + 남은 초 한 줄
		L.buffs = Rectangle{ L.hp.x, L.panel.y - 18.f - 42.f, L.hp.width, 40.f };
		return L;
	}

	// 칸 왼쪽 위 단축키 표시(글자 길이에 맞춰 넓어진다: "Spc" "Alt+U"). far = 사거리 밖(키 글자 붉게, Phase 3-C)
	void KeyTag(Rectangle slot, const std::string& key, bool active, float scale = 1.f, bool far = false)
	{
		const float kh = std::round(13.f * scale);
		const float fs = std::min(ui::type::kMicro, kh - 3.f);
		const float kw = std::max(kh + 1.f, std::round(ui::Fonts().Measure(key, fs, 0.f, ui::Face::Strong).x + 8.f));
		const Rectangle r{ std::round(slot.x - 4.f), std::round(slot.y - 6.f), kw, kh };
		if (!far)
		{
			ui::Keycap(r, key, active);
			return;
		}
		ui::Plate(r, Color{ 46, 16, 14, 235 }, ui::theme::kBad, 2.f, 1.f);
		ui::TextStyle st; st.size = fs; st.color = Color{ 255, 150, 132, 255 }; st.face = ui::Face::Strong;
		const Vector2 m = ui::Fonts().Measure(key, st.size, 0.f, ui::Face::Strong);
		ui::Fonts().Draw(key, Vector2{ std::round(r.x + (r.width - m.x) * 0.5f), std::round(r.y + (r.height - m.y) * 0.5f) }, st);
	}

	void CooldownSweep(Rectangle r, float frac, unsigned char alpha)
	{
		ui::BeginClip(Rectangle{ r.x, r.y, r.width, r.height });
		DrawCircleSector(Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, r.width * 0.75f, -90.f, -90.f + 360.f * util::Clamp01(frac), 36, Color{ 0, 0, 0, alpha });
		ui::EndClip();
	}
}

namespace
{
	// 전투 판 덩어리(Phase 3-A · 3-C): 판 + 위쪽 고유 기술 게이지(18px) + 버프 · 약화 줄(42px)
	Rectangle CombatNatural(const Hud& L) { return Rectangle{ L.panel.x, L.buffs.y - 2.f, L.panel.width, L.panel.y + L.panel.height - (L.buffs.y - 2.f) }; }
}

// 옮기고 배율을 건 전투 판의 위 · 왼쪽(대화창 · 채팅이 피한다)
float Game::HudTop() const { return wm::HudRect("hud.combat", CombatNatural(Layout(m_uiW, m_uiH))).y; }
float Game::HudLeft() const { return wm::HudRect("hud.combat", CombatNatural(Layout(m_uiW, m_uiH))).x; }

// ─────────────────────────────────────────────────────────────────────────────
// 판 · HP · MP
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawCombatPanel()
{
	const Hud L = Layout(m_uiW, m_uiH);
	wm::HudScope hs("hud.combat", "전투 판", CombatNatural(L));
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

	// HP 막대 위: 버프(왼쪽에서) · 약화(오른쪽에서) — Phase 3-C
	DrawStatusRow(L.buffs.x, L.buffs.y, false);
	DrawStatusRow(L.buffs.x + L.buffs.width, L.buffs.y, true);
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
		const SkillDef* dd = SkillDefTable::Find(id);
		if (const SkillRow* dr = SkillTable::Find(id); dr && dd && dd->kind != SkillKind::Basic && dr->cls == m_classId)
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

	const int slot = CdIndex(row->id);
	const SkillDef* def = SkillDefTable::Find(row->id);
	const bool basic = def && def->kind == SkillKind::Basic;
	const float S = base.width;
	// 누른 순간 살짝 눌렸다가 튀어나온다 — 시전을 보냈든 막혔든 누르면 늘(Phase 3-C)
	const double pressAt = std::max(m_castSentAt[static_cast<size_t>(slot)], m_hotPressAt[static_cast<size_t>(hot)]);
	const float press = static_cast<float>(std::max(0.0, 0.14 - (m_now - pressAt)) / 0.14);
	float sc = 1.f - 0.1f * press;
	// 재사용 대기가 끝난 순간 1.08배로 한 번 튄다
	if (const double fl = m_now - m_skillFlash[static_cast<size_t>(slot)]; fl >= 0.0 && fl < 0.22)
		sc *= 1.f + 0.08f * std::sin(util::kPi * static_cast<float>(fl / 0.22));
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
		// 1초 이상 남았을 때만 숫자(올림 정수) — 1초 아래는 부채꼴만(Phase 3-C)
		if (!basic && remain >= 1.0)
		{
			ui::TextStyle cs; cs.size = std::round(S * 0.4f); cs.color = ui::theme::kTextHi; cs.outline = 1.5f; cs.outlineColor = Color{ 0, 0, 0, 210 }; cs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(Fmt("%.0f", std::ceil(remain)), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, cs);
		}
	}
	if (noMana)
		ui::Plate(r, Color{ 44, 56, 74, 165 }, BLANK, 2.f, 0.f);   // 자원 부족 = 푸른 회색
	// 대기 · 자원 부족인데 눌렀다: 붉게 한 번 반짝
	if (const double dn = m_now - m_cdDeniedAt[static_cast<size_t>(slot)]; dn >= 0.0 && dn < 0.35)
	{
		const float u = static_cast<float>(dn / 0.35);
		ui::Plate(r, util::WithA(ui::theme::kBad, 0.42f * (1.f - u)), util::WithA(ui::theme::kBad, 1.f - u), 2.f, 2.f);
	}
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
	const bool queued = m_queuedSkill == row->id || m_bufferSlot == row->id || m_aimingHot == hot;
	// 기술 구조 V3 상태: 고유 기술 게이지 · 토글 켜짐 · 모으는 중 · 체인 입력 창
	if (def && def->kind == SkillKind::Identity && !locked)
	{
		const float g = util::Clamp01(m_identity / 1000.f);
		if (g < 1.f)
		{
			ui::Plate(Rectangle{ r.x, r.y, r.width, r.height * (1.f - g) }, Color{ 0, 0, 0, 150 }, BLANK, 2.f, 0.f);
			ui::TextStyle gs; gs.size = std::round(S * 0.3f); gs.color = ui::theme::kTextHi; gs.outline = 1.5f; gs.outlineColor = Color{ 0, 0, 0, 210 }; gs.face = ui::Face::Number;
			ui::Fonts().DrawCentered(Fmt("%d%%", static_cast<int>(g * 100.f)), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f }, gs);
		}
	}
	const bool toggled = def && def->kind == SkillKind::Toggle && m_toggleSkill == row->id;
	const bool holding = m_heldSkill == row->id;
	const bool chainOpen = def && def->kind == SkillKind::Chain && m_chainSkill == row->id && m_now < m_chainUntil;
	if (toggled || holding || chainOpen)
	{
		const float p2 = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * (holding ? 16.f : 6.f));
		BeginBlendMode(BLEND_ADDITIVE);
		ui::Plate(r, util::WithA(util::Mix(cc, WHITE, 0.3f), 0.18f + 0.18f * p2), BLANK, 2.f, 0.f);
		EndBlendMode();
		if (chainOpen)
		{
			// 체인: 다음 입력 창이 줄어든다 + 남은 단
			const float left = static_cast<float>((m_chainUntil - m_now) / std::max(0.1, def->stageMs / 1000.0));
			ui::Plate(Rectangle{ r.x + 2.f, r.y + r.height - 5.f, (r.width - 4.f) * util::Clamp01(left), 3.f }, util::Mix(cc, WHITE, 0.4f), BLANK, 1.f, 0.f);
			for (int k = 0; k < def->stages; ++k)
				DrawCircleV(Vector2{ r.x + r.width - 7.f - k * 7.f, r.y + 7.f }, 2.5f, k < m_chainStage ? util::Mix(cc, WHITE, 0.5f) : Color{ 60, 64, 70, 230 });
		}
	}
	const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 12.f);
	Color border = queued ? util::Mix(ui::theme::kDawn, WHITE, 0.3f * pulse) : (hover ? ui::theme::kTextDim : (locked ? ui::theme::kLine : ui::theme::kLineStrong));
	if (!queued && hot == kHotAwaken && !locked && !cooling && !noMana)
	{
		const float breathe = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 2.2f);
		border = util::Mix(ui::theme::kLineStrong, ui::theme::kDawn, 0.35f + 0.45f * breathe);
	}
	if (dropTarget && hover) border = ui::theme::kDawn;
	ui::Plate(r, BLANK, border, 2.f, queued || hot == kHotAwaken ? 2.f : 1.f);
	// 사거리 밖: 커서 쪽 적이 기술 사거리 밖이면 키 글자가 붉다(대상형 기술)
	bool far = false;
	if (!locked && !basic && row->target == SkillTarget::Enemy)
		if (Entity* t = Find(m_aim.enemy); t && IsEnemy(*t) && !t->dead)
		{
			const int range = def && def->range > 0 ? def->range : row->range;
			far = std::max(std::abs(t->tx - m_predX), std::abs(t->ty - m_predY)) > range;
		}
	KeyTag(r, key, queued, keySize, far);
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
		m_previewSkill = row->id;   // 월드에 사거리 · 범위를 보여 준다
	}
}

void Game::DrawSkillBar()
{
	const Hud L = Layout(m_uiW, m_uiH);
	wm::HudScope hs("hud.combat", "전투 판", CombatNatural(L));
	m_previewSkill = -1;
	for (int i = 0; i < 8; ++i)
		DrawHotSlot(L.skill[i], 1 + i, 1.f);
	DrawHotSlot(L.awaken, kHotAwaken, 1.15f);
	DrawHotSlot(L.mobility, kHotMobility, 1.f);
	DrawHotSlot(L.identity, kHotIdentity, 1.f);
	// 고유 기술 게이지(HP 막대 위 — 직업마다 칸 모양)
	DrawIdentityGauge(Rectangle{ std::round(L.hp.x + L.hp.width * 0.3f), std::round(L.panel.y - 15.f), std::round(L.hp.width * 0.4f), 9.f });
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == kDragSkill)
		m_pressKind = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 소모품 칸(전투 1~4 · 물약 F1 · 보조 5~8) — 소지품에서 끌어다 등록, 오른쪽 클릭 비우기
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuickBar()
{
	const Hud L = Layout(m_uiW, m_uiH);
	wm::HudScope hs("hud.combat", "전투 판", CombatNatural(L));
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
// 메뉴 막대(Phase 3-C) — 오른쪽 아래: 캐릭터 · 소지품 · 기술 · 퀘스트 · 지도 · 결사 · 경매장 · 설정
// 아이콘은 선으로 그린다(「새김」 — 금테 · 광택 없음). 칸 왼쪽 위 = 지금 키, 새벽빛 점 = 볼 것이 있다
// (능력치 포인트 · 기술 점수 · 보고할 퀘스트 · 정산함(Phase 5)).
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	enum class MenuIcon { Character, Bag, Skill, Quest, Map, Guild, Auction, Settings };

	void DrawMenuIcon(MenuIcon ic, Vector2 c, float k, Color col)
	{
		const float t = 1.6f;
		switch (ic)
		{
		case MenuIcon::Character:   // 머리 + 어깨
			DrawCircleV(Vector2{ c.x, c.y - 4.f * k }, 3.6f * k, col);
			DrawRing(Vector2{ c.x, c.y + 7.5f * k }, 5.2f * k, 5.2f * k + t, 180.f, 360.f, 16, col);
			DrawLineEx(Vector2{ c.x - 6.8f * k, c.y + 7.5f * k }, Vector2{ c.x + 6.8f * k, c.y + 7.5f * k }, t, col);
			break;
		case MenuIcon::Bag:         // 주머니 + 끈
			DrawRectangleLinesEx(Rectangle{ c.x - 6.f * k, c.y - 3.f * k, 12.f * k, 10.f * k }, t, col);
			DrawRing(Vector2{ c.x, c.y - 3.f * k }, 3.f * k, 3.f * k + t, 180.f, 360.f, 12, col);
			DrawLineEx(Vector2{ c.x - 6.f * k, c.y + 1.f * k }, Vector2{ c.x + 6.f * k, c.y + 1.f * k }, 1.f, col);
			break;
		case MenuIcon::Skill:       // 네 갈래 별
		{
			const float r1 = 7.5f * k, r2 = 2.2f * k;
			for (int i = 0; i < 4; ++i)
			{
				const float a = util::kPi * 0.5f * i;
				const Vector2 tip{ c.x + std::cos(a) * r1, c.y + std::sin(a) * r1 };
				const Vector2 l{ c.x + std::cos(a + util::kPi * 0.25f) * r2, c.y + std::sin(a + util::kPi * 0.25f) * r2 };
				const Vector2 rr{ c.x + std::cos(a - util::kPi * 0.25f) * r2, c.y + std::sin(a - util::kPi * 0.25f) * r2 };
				DrawTriangle(tip, rr, l, col);
				DrawTriangle(tip, l, rr, col);
			}
			break;
		}
		case MenuIcon::Quest:       // 두루마리(책장) + 줄
			DrawRectangleLinesEx(Rectangle{ c.x - 5.5f * k, c.y - 7.f * k, 11.f * k, 14.f * k }, t, col);
			for (int i = 0; i < 3; ++i)
				DrawLineEx(Vector2{ c.x - 3.f * k, c.y + (-3.f + 3.2f * i) * k }, Vector2{ c.x + 3.f * k, c.y + (-3.f + 3.2f * i) * k }, 1.f, col);
			break;
		case MenuIcon::Map:         // 접힌 지도(세 쪽)
		{
			const Vector2 p[4] = { { c.x - 7.f * k, c.y - 5.f * k }, { c.x - 2.3f * k, c.y - 7.f * k }, { c.x + 2.3f * k, c.y - 5.f * k }, { c.x + 7.f * k, c.y - 7.f * k } };
			for (int i = 0; i < 4; ++i)
				DrawLineEx(p[i], Vector2{ p[i].x, p[i].y + 12.f * k }, t, col);
			for (int i = 0; i < 3; ++i)
			{
				DrawLineEx(p[i], p[i + 1], t, col);
				DrawLineEx(Vector2{ p[i].x, p[i].y + 12.f * k }, Vector2{ p[i + 1].x, p[i + 1].y + 12.f * k }, t, col);
			}
			break;
		}
		case MenuIcon::Guild:       // 깃대 + 갈라진 깃발
			DrawLineEx(Vector2{ c.x - 5.f * k, c.y - 8.f * k }, Vector2{ c.x - 5.f * k, c.y + 8.f * k }, t, col);
			DrawTriangle(Vector2{ c.x - 5.f * k, c.y - 7.f * k }, Vector2{ c.x - 5.f * k, c.y + 1.f * k }, Vector2{ c.x + 7.f * k, c.y - 7.f * k }, col);
			DrawTriangle(Vector2{ c.x - 5.f * k, c.y + 1.f * k }, Vector2{ c.x + 7.f * k, c.y + 1.f * k }, Vector2{ c.x + 1.f * k, c.y - 3.f * k }, col);
			break;
		case MenuIcon::Auction:     // 저울
			DrawLineEx(Vector2{ c.x, c.y - 7.f * k }, Vector2{ c.x, c.y + 6.f * k }, t, col);
			DrawLineEx(Vector2{ c.x - 7.f * k, c.y - 4.f * k }, Vector2{ c.x + 7.f * k, c.y - 4.f * k }, t, col);
			DrawLineEx(Vector2{ c.x - 4.f * k, c.y + 7.f * k }, Vector2{ c.x + 4.f * k, c.y + 7.f * k }, t, col);
			DrawRing(Vector2{ c.x - 7.f * k, c.y + 1.f * k }, 2.4f * k, 2.4f * k + 1.2f, 0.f, 180.f, 10, col);
			DrawRing(Vector2{ c.x + 7.f * k, c.y + 1.f * k }, 2.4f * k, 2.4f * k + 1.2f, 0.f, 180.f, 10, col);
			break;
		case MenuIcon::Settings:    // 톱니
			for (int i = 0; i < 8; ++i)
			{
				const float a = i * util::kPi / 4.f;
				DrawLineEx(Vector2{ c.x + std::cos(a) * 4.f * k, c.y + std::sin(a) * 4.f * k }, Vector2{ c.x + std::cos(a) * 7.8f * k, c.y + std::sin(a) * 7.8f * k }, 2.6f, col);
			}
			DrawCircleV(c, 5.4f * k, col);
			DrawCircleV(c, 2.4f * k, Color{ 14, 16, 20, 255 });
			break;
		}
	}
}

void Game::DrawMenuBar()
{
	const float W = m_uiW, H = m_uiH;
	struct Item { MenuIcon icon; const char* name; kb::Act act; bool dot; const char* dotText; };
	bool questReady = false;
	for (const auto& v : m_quests) if (v.readyAt > 0.0) { questReady = true; break; }
	const Item items[] = {
		{ MenuIcon::Character, "캐릭터", kb::Act::Character, m_freePoints > 0, "올릴 능력치 포인트가 있다" },
		{ MenuIcon::Bag, "소지품", kb::Act::Inventory, false, "" },
		{ MenuIcon::Skill, "기술", kb::Act::SkillWindow, m_skillPoints > 0, "쓸 기술 점수가 있다" },
		{ MenuIcon::Quest, "퀘스트 일지", kb::Act::QuestLog, questReady, "보고할 퀘스트가 있다" },
		{ MenuIcon::Map, "월드 지도", kb::Act::WorldMap, false, "" },
		{ MenuIcon::Guild, "결사", kb::Act::Guild, m_guild.inviteGuild != 0, "결사 초대를 받았다" },
		{ MenuIcon::Auction, "경매장", kb::Act::Auction, m_auction.settleCount > 0, "정산함에 받을 것이 있다" },
		{ MenuIcon::Settings, "설정", kb::Act::Count, false, "" },
	};
	const float S = 34.f, gap = 4.f, pad = 7.f;
	const int n = static_cast<int>(std::size(items));
	const float w = n * S + (n - 1) * gap + pad * 2.f, h = S + pad * 2.f;
	const Rectangle natural{ std::round(W - 14.f - w), std::round(H - 12.f - h), w, h };
	wm::HudScope hs("hud.menu", "메뉴 막대", natural);
	ui::Panel(natural, 0.9f, false);
	for (int i = 0; i < n; ++i)
	{
		const Item& it = items[i];
		const Rectangle b{ natural.x + pad + i * (S + gap), natural.y + pad, S, S };
		const bool hover = ui::Hovered(b);
		bool open = false;
		switch (it.icon)
		{
		case MenuIcon::Character: open = m_charOpen; break;
		case MenuIcon::Bag: open = m_invOpen; break;
		case MenuIcon::Skill: open = m_skillWinOpen; break;
		case MenuIcon::Quest: open = m_questLogOpen; break;
		case MenuIcon::Map: open = m_mapOpen; break;
		case MenuIcon::Guild: open = m_guildOpen; break;
		case MenuIcon::Auction: open = m_auctionOpen; break;
		case MenuIcon::Settings: open = m_menuOpen; break;
		default: break;
		}
		const bool down = hover && ui::BtnDown(MOUSE_BUTTON_LEFT);
		ui::Plate(b, down ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : Color{ 14, 17, 21, 235 }), open ? ui::theme::kDawn : (hover ? ui::theme::kTextDim : ui::theme::kLine), 3.f, open ? 1.5f : 1.f);
		const Color ic = open ? ui::theme::kDawnHi : (hover ? ui::theme::kTextHi : ui::theme::kText);
		DrawMenuIcon(it.icon, Vector2{ b.x + S * 0.5f, b.y + S * 0.5f + (down ? 1.f : 0.f) }, 1.f, ic);
		const std::string key = it.act == kb::Act::Count ? std::string("Esc") : kb::Label(it.act);
		// 칸 안에는 짧은 키만(P · I · F9) — "Alt+U" · "Esc"처럼 긴 키는 아이콘을 덮으므로 툴팁에만(Phase 3 자기 검토)
		if (!key.empty() && key.size() <= 2)
		{
			ui::TextStyle ks; ks.size = ui::type::kMicro - 1.f; ks.color = ui::theme::kTextDim; ks.face = ui::Face::Strong; ks.outline = 1.f; ks.outlineColor = Color{ 0, 0, 0, 200 };
			ui::Fonts().Draw(key, Vector2{ b.x + 3.f, b.y + 1.f }, ks);
		}
		if (it.dot)
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 4.f);
			DrawCircleV(Vector2{ b.x + S - 5.f, b.y + 5.f }, 3.4f + pulse * 0.8f, ui::theme::kDawn);
			DrawCircleV(Vector2{ b.x + S - 5.f, b.y + 5.f }, 1.6f, ui::theme::kDawnHi);
		}
		ui::Block(b);
		if (hover)
		{
			std::vector<ui::TooltipLine> tip{ { std::string(it.name) + (key.empty() ? "" : "  [" + key + "]"), ui::theme::kTextHi, 14.f, ui::Face::Strong } };
			if (it.dot) tip.push_back({ it.dotText, ui::theme::kDawnHi, 12.5f });
			if (it.icon == MenuIcon::Guild && m_guild.id != 0) tip.push_back({ "「" + m_guild.name + "」 · 접속 " + std::to_string(m_guild.online) + "명", ui::theme::kChatGuild, 12.5f });
			ui::Tooltip(std::move(tip));
		}
		if (ui::Clicked(b))
		{
			ui::ConsumeMouse();
			auto toggle = [&](bool& flag) { flag = !flag; sfx::Play(flag ? sfx::Id::Open : sfx::Id::Close, 0.6f); };
			switch (it.icon)
			{
			case MenuIcon::Character: toggle(m_charOpen); break;
			case MenuIcon::Bag: toggle(m_invOpen); break;
			case MenuIcon::Skill: toggle(m_skillWinOpen); break;
			case MenuIcon::Quest: toggle(m_questLogOpen); m_abandonArm = 0; break;
			case MenuIcon::Map: toggle(m_mapOpen); break;
			case MenuIcon::Guild: if (m_guildOpen) toggle(m_guildOpen); else OpenGuildWindow(); break;
			case MenuIcon::Auction: if (m_auctionOpen) toggle(m_auctionOpen); else OpenAuction(m_auction.settleCount > 0 ? 3 : m_auction.tab); break;
			case MenuIcon::Settings: m_menuOpen = true; sfx::Play(sfx::Id::Open, 0.5f); break;
			}
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 시스템 메뉴(Esc — 열린 창이 없을 때)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawSystemMenu()
{
	const float W = m_uiW, H = m_uiH;
	DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, Color{ 0, 0, 0, 90 });
	const Rectangle r{ std::round(W * 0.5f - 150.f), std::round(H * 0.5f - 199.f), 300.f, 386.f };
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
	// Phase 3-A: HUD 덩어리 옮기기 · 배율(편집 모드) · 창 배치 되돌리기
	if (item("화면 배치", ButtonKind::Normal)) { m_menuOpen = false; m_uiEdit = true; sfx::Play(sfx::Id::Open, 0.5f); }
	if (item("창 배치 되돌리기", ButtonKind::Normal)) { wm::ResetWindows(); m_menuOpen = false; AddCenter("창 배치를 기본으로 되돌렸습니다.", ui::theme::kTextDim); sfx::Play(sfx::Id::Close, 0.5f); }
	if (item("도움말", ButtonKind::Normal)) { m_menuOpen = false; m_helpOpen = true; sfx::Play(sfx::Id::Open, 0.5f); }
	if (item("게임 종료", ButtonKind::Danger)) { SaveControls(); if (wm::Dirty()) SaveUiLayout(); m_quit = true; }
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
	const float W = m_uiW, H = m_uiH;
	DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, Color{ 0, 0, 0, 110 });
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
		// 전투 연출(Phase 2-A): 칸 여러 개 중 하나 고르기
		auto choice = [&](const char* name, const char* const* labels, const int* values, int n, int cur, auto&& set, const char* tip)
		{
			ui::Text(name, Vector2{ x + 4.f, y + 5.f }, ui::type::kBodyS, ui::theme::kText);
			const float bw = n >= 3 ? 44.f : 62.f;
			for (int i = 0; i < n; ++i)
			{
				const Rectangle b{ x + colW - bw * (n - i) - 3.f * (n - 1 - i), y + 1.f, bw, rowH - 4.f };
				if (ui::Button(b, labels[i], cur == values[i] ? ButtonKind::Primary : ButtonKind::Normal, true, 13.f)) { set(values[i]); kb::Save(m_playerName); }
			}
			const Rectangle hit{ x, y, colW - (bw + 3.f) * n, rowH };
			if (tip && ui::Hovered(hit))
				ui::Tooltip({ { name, ui::theme::kTextHi, 14.f, ui::Face::Strong }, { tip, ui::theme::kText, 13.f } });
			y += rowH;
		};
		{
			static const char* const kQ[] = { "낮음", "보통", "높음" };
			static const int kQv[] = { 0, 1, 2 };
			choice("그래픽 품질", kQ, kQv, 3, o.gfxQuality, [&](int v) { o.gfxQuality = static_cast<uint8_t>(v); },
				"낮음: 빛 번짐 · 화면 일그러짐을 끄고 입자를 절반으로. 보통: 빛 번짐을 가볍게.");
			static const char* const kO[] = { "25%", "50%", "100%" };
			static const int kOv[] = { 25, 50, 100 };
			choice("남의 기술 연출", kO, kOv, 3, o.otherFx, [&](int v) { o.otherFx = static_cast<uint8_t>(v); },
				"다른 모험가가 쓴 기술이 보이는 세기. 내 기술은 늘 100%.");
		}
		toggle("광과민 보호", o.photoSafe);
		toggle("화면 흔들림", o.shake);
		{
			// Phase 3-A: UI 배율 — 자동 = 1080p까지 100%, 1440p 125%, 4K 200%
			static const char* const kU[] = { "자동", "100", "125", "150", "200" };
			static const int kUv[] = { 0, 100, 125, 150, 200 };
			choice("창 · 글자 크기", kU, kUv, 5, o.uiScale, [&](int v) { o.uiScale = static_cast<uint8_t>(v); },
				"전투 판 · 창 · 글자를 함께 키운다(%). 자동은 화면 높이로 정한다 — 1080p까지 100%, 1440p 125%, 4K 200%.");
		}
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
// 확장 미니맵(Tab) — 화면 가운데 큰 반투명 지도. 창이 아니라서 클릭은 월드로 간다(달리면서 본다).
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawBigMap()
{
	const float W = m_uiW;
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
