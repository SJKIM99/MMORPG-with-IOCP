#include "game/Game.h"
#include "game/KeyBinds.h"
#include "game/SkillView.h"
#include "fx/SkillFx.h"
#include "audio/Sfx.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 기술 성장(Phase 2-D) — 기술 창(K) · 레벨 · 점수 · 갈래.
//
//  · 서버 권위: 레벨 · 갈래 · 남은 점수는 SKILL_TREE_INF · SKILL_GROW_ACK가 기준이다. 누르면 요청만 보내고,
//    결과가 오면 바뀐다(버튼은 결과를 기다리는 동안 잠깐 잠긴다). 칸 배치(끌어서 놓기)는 클라 설정 그대로.
//  · 갈래는 SkillGrowth::ApplyDef가 기술 표 복사본을 바꾼다 — 예측 · 연출 · 범위 미리보기가 같은 복사본(skillview)을 쓴다.
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

	const char* GrowResultText(SkillGrowResult r)
	{
		switch (r)
		{
		case SkillGrowResult::Unknown: return "바꿀 수 없는 기술입니다.";
		case SkillGrowResult::Locked: return "아직 배우지 않은 기술입니다.";
		case SkillGrowResult::MaxLevel: return "이미 가장 높은 레벨입니다.";
		case SkillGrowResult::MinLevel: return "더 내릴 수 없습니다.";
		case SkillGrowResult::NoPoints: return "기술 점수가 모자랍니다.";
		case SkillGrowResult::TierLocked: return "기술 레벨이 모자라 고를 수 없습니다.";
		case SkillGrowResult::InCombat: return "싸우는 중에는 기술을 바꿀 수 없습니다.";
		case SkillGrowResult::Busy: return "지금은 바꿀 수 없습니다.";
		default: return "";
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 상태 · 요청 · 결과
// ─────────────────────────────────────────────────────────────────────────────
SkillGrowth::State Game::MyGrow(uint16_t skillId) const
{
	return skillId < SkillGrowth::kIdSpan ? SkillGrowth::Unpack(m_skillGrow[skillId]) : SkillGrowth::State{};
}

uint8_t Game::MyVarBits(uint16_t skillId) const
{
	return SkillGrowth::NfyBits(MyGrow(skillId));
}

const SkillDef* Game::MyDef(uint16_t skillId) const
{
	return skillview::Def(skillId, MyVarBits(skillId));
}

void Game::HandleSkillTree(const SKILL_TREE_INF_PACKET& p)
{
	const bool first = !m_skillTreeKnown;
	const uint16_t prevTotal = m_skillPointsTotal;
	m_skillGrow.fill(0);
	for (int i = 0; i < p.count && i < 8; ++i)
		if (p.skills[i].skillId < SkillGrowth::kIdSpan)
			m_skillGrow[p.skills[i].skillId] = p.skills[i].state;
	m_skillPoints = p.points;
	m_skillPointsTotal = p.total;
	m_skillTreeKnown = true;
	m_growBusyUntil = 0.0;
	if (!first && p.total > prevTotal)
		AddCenter(Fmt("기술 점수 +%d — 기술 창(%s)에서 올릴 수 있습니다", p.total - prevTotal, kb::Label(kb::Act::SkillWindow).c_str()), ui::theme::kDawnHi);
	if (SkillTrace())
		std::fprintf(stderr, "[skill] t=%.3f tree points=%u total=%u count=%u\n", m_now, p.points, p.total, p.count);
}

void Game::HandleSkillGrowAck(const SKILL_GROW_ACK_PACKET& p)
{
	m_growBusyUntil = 0.0;
	m_growAckSeq = p.seq;
	m_growAckResult = p.result;
	const auto r = static_cast<SkillGrowResult>(p.result);
	if (p.skill.skillId != 0 && p.skill.skillId < SkillGrowth::kIdSpan)
		m_skillGrow[p.skill.skillId] = p.skill.state;
	m_skillPoints = p.points;
	if (SkillTrace())
		std::fprintf(stderr, "[skill] t=%.3f grow seq=%u op=%u result=%u id=%u state=0x%02X points=%u\n", m_now, p.seq, p.op, p.result, p.skill.skillId, p.skill.state, p.points);
	if (r != SkillGrowResult::Ok)
	{
		std::string msg = GrowResultText(r);
		if (r == SkillGrowResult::NoPoints) msg = Fmt("기술 점수가 모자랍니다 (%u점 필요).", p.param);
		else if (r == SkillGrowResult::TierLocked) msg = Fmt("기술 레벨 %u에 고를 수 있습니다.", p.param);
		else if (r == SkillGrowResult::Locked) msg = Fmt("Lv.%u에 배울 수 있습니다.", p.param);
		else if (r == SkillGrowResult::InCombat && p.param > 0) msg = Fmt("싸우는 중에는 기술을 바꿀 수 없습니다 (%.1f초).", p.param / 10.f);
		AddCenter(msg, Rgb(0xC8C0B0));
		sfx::Play(sfx::Id::Error, 0.35f);
		return;
	}
	switch (static_cast<SkillGrowOp>(p.op))
	{
	case SkillGrowOp::LevelUp:
		m_growFlashAt = m_now;
		m_growFlashSkill = p.skill.skillId;
		sfx::Play(sfx::Id::SkillReady, 0.7f, 0.01f);
		{
			const SkillGrowth::State s = SkillGrowth::Unpack(p.skill.state);
			for (int tier = 0; tier < 2; ++tier)
				if (s.level == SkillGrowth::kTierLevel[tier] && s.pick[tier] == 0)
					if (const SkillRow* row = SkillTable::Find(p.skill.skillId))
						AddCenter(Fmt("%s — %s을 고를 수 있습니다", row->name, SkillGrowth::TierName(tier)), ui::theme::kDawnHi);
		}
		break;
	case SkillGrowOp::LevelDown:
		sfx::Play(sfx::Id::Close, 0.5f);
		break;
	case SkillGrowOp::Pick:
		m_growFlashAt = m_now;
		m_growFlashSkill = p.skill.skillId;
		sfx::Play(sfx::Id::Equip, 0.6f);
		break;
	case SkillGrowOp::ResetAll:
		AddCenter("기술 점수를 모두 돌려받았습니다.", ui::theme::kDawnHi);
		sfx::Play(sfx::Id::Equip, 0.6f);
		break;
	}
}

bool Game::ScriptGrowStep(uint8_t targetLevel, uint8_t pickA, uint8_t pickB)
{
	if (!m_skillTreeKnown || m_now < m_growBusyUntil) return false;
	for (int k = static_cast<int>(SkillKey::Q); k <= static_cast<int>(SkillKey::F); ++k)
	{
		const uint16_t id = SkillDefTable::Default(m_classId, static_cast<SkillKey>(k));
		const SkillRow* row = SkillTable::Find(id);
		if (!row || m_level < row->unlockLevel || !SkillGrowth::Growable(id)) continue;
		const SkillGrowth::State st = MyGrow(id);
		if (st.level < targetLevel && m_skillPoints >= SkillGrowth::StepCost(st.level)) { SendSkillGrow(SkillGrowOp::LevelUp, id); return false; }
		if (st.level >= SkillGrowth::kTierLevel[0] && st.pick[0] != pickA) { SendSkillGrow(SkillGrowOp::Pick, id, 0, pickA); return false; }
		if (st.level >= SkillGrowth::kTierLevel[1] && st.pick[1] != pickB) { SendSkillGrow(SkillGrowOp::Pick, id, 1, pickB); return false; }
	}
	return true;
}

void Game::SendSkillGrow(SkillGrowOp op, uint16_t skillId, uint8_t tier, uint8_t choice)
{
	if (m_now < m_growBusyUntil) return;
	m_growBusyUntil = m_now + 0.6;
	m_net.SendSkillGrow(op, skillId, tier, choice, ++m_growSeq);
	if (SkillTrace())
		std::fprintf(stderr, "[skill] t=%.3f growreq seq=%u op=%u id=%u tier=%u choice=%u\n", m_now, m_growSeq, static_cast<unsigned>(op), skillId, tier, choice);
}

// ─────────────────────────────────────────────────────────────────────────────
// 기술 창(K) — 왼쪽: 직업의 12칸(끌어서 칸 바꾸기) · 오른쪽: 고른 기술의 레벨 · 갈래 · 시연
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawSkillWindow()
{
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float wh = std::min(HudTop() - 20.f, 620.f);
	const float ww = std::min(W - 24.f, 1010.f);
	const Rectangle r{ std::round(std::max(12.f, W * 0.5f - ww * 0.5f)), std::round(std::max(12.f, std::min(H * 0.5f - wh * 0.5f - 30.f, HudTop() - 10.f - wh))), ww, wh };
	ui::Window(r, "기술", &m_skillWinOpen);
	const ClassRow& cls = ClassTable::Get(m_classId);
	ui::Text(Fmt("%s · %s", cls.name, cls.role), Vector2{ r.x + r.width - 48.f, r.y + 15.f }, ui::type::kCaption, util::Shade(Rgb(cls.color), 1.1f), Align::Right);

	// ── 위 줄: 기술 점수 · 초기화 ──
	const float topY = r.y + 46.f;
	{
		ui::TextStyle ps; ps.size = 22.f; ps.color = m_skillPoints > 0 ? ui::theme::kDawnHi : ui::theme::kTextDim; ps.face = ui::Face::NumberBold;
		ui::Text("기술 점수", Vector2{ r.x + 16.f, topY + 6.f }, ui::type::kCaption, ui::theme::kTextDim);
		const float nx = r.x + 16.f + ui::Fonts().Measure("기술 점수", ui::type::kCaption, 0.f, ui::Face::Body).x + 10.f;
		const float nw = ui::Fonts().Draw(std::to_string(m_skillPoints), Vector2{ nx, topY - 1.f }, ps);
		ui::Text(Fmt("/ %u", m_skillPointsTotal), Vector2{ nx + nw + 6.f, topY + 6.f }, ui::type::kCaption, ui::theme::kTextMute);
		ui::Text("레벨마다 2점 · 기술 레벨 4 · 7에서 갈래를 고릅니다", Vector2{ nx + nw + 60.f, topY + 6.f }, ui::type::kCaption, ui::theme::kTextDim);
		const bool busy = m_now < m_growBusyUntil;
		if (ui::Button(Rectangle{ r.x + r.width - 118.f, topY - 2.f, 102.f, 28.f }, "초기화", ButtonKind::Danger, m_skillTreeKnown && !busy && m_skillPointsTotal > m_skillPoints, 13.f))
			SendSkillGrow(SkillGrowOp::ResetAll, 0);
		if (ui::Button(Rectangle{ r.x + r.width - 238.f, topY - 2.f, 112.f, 28.f }, "기본 배치로", ButtonKind::Normal, true, 13.f))
		{
			InitHotbar(false);
			SaveHotbar();
			sfx::Play(sfx::Id::Equip, 0.5f);
		}
	}
	const float y0 = topY + 38.f;
	ui::Rule(r.x + 12.f, r.x + r.width - 12.f, y0 - 6.f, ui::theme::kLine);

	// ── 왼쪽: 12칸 목록 ──
	const float listW = std::round(std::min(360.f, r.width * 0.38f));
	const float listH = r.y + r.height - 16.f - y0;
	const float rowH = std::min(44.f, std::floor(listH / kSkillKeys));
	if (m_growSel == 0 || SkillTable::Find(m_growSel) == nullptr || SkillTable::Find(m_growSel)->cls != m_classId)
		m_growSel = SkillDefTable::Default(m_classId, SkillKey::Q);
	uint16_t hoverSkill = 0;   // 마우스를 올린 줄 — 오른쪽(설명 · 시연)이 잠시 그 기술을 보여 준다
	for (int i = 0; i < kSkillKeys; ++i)
	{
		const SkillRow* row = SkillTable::Find(SkillDefTable::Default(m_classId, static_cast<SkillKey>(i)));
		if (!row) continue;
		const SkillDef* def = SkillDefTable::Find(row->id);
		const bool basicRow = def && def->kind == SkillKind::Basic;
		const Rectangle rr{ r.x + 12.f, y0 + i * rowH, listW, rowH - 3.f };
		const bool hover = ui::Hovered(rr);
		const bool sel = m_growSel == row->id;
		const bool locked = m_level < row->unlockLevel;
		ui::Plate(rr, sel ? ui::theme::kSurface3 : (hover ? ui::theme::kSurface2 : ui::theme::kSurface1), sel ? ui::theme::kDawn : ui::theme::kLine, 3.f, 1.f);
		const float isz = rowH - 11.f;
		const Rectangle icon{ rr.x + 6.f, rr.y + (rr.height - isz) * 0.5f, isz, isz };
		ui::Plate(Rectangle{ icon.x - 1, icon.y - 1, icon.width + 2, icon.height + 2 }, Color{ 10, 12, 15, 245 }, ui::theme::kLineStrong, 3.f, 1.f);
		skillfx::DrawSkillIcon(row->id, Vector2{ icon.x + isz * 0.5f, icon.y + isz * 0.5f }, isz - 4.f, m_time, locked);
		// 레벨이 막 올랐으면 테가 반짝인다
		if (m_growFlashSkill == row->id && m_now - m_growFlashAt < 0.6)
		{
			const float u = static_cast<float>((m_now - m_growFlashAt) / 0.6);
			BeginBlendMode(BLEND_ADDITIVE);
			DrawRectangleLinesEx(Rectangle{ icon.x - 3, icon.y - 3, icon.width + 6, icon.height + 6 }, 2.f, util::WithA(ui::theme::kDawnHi, 1.f - u));
			EndBlendMode();
		}
		ui::TextStyle ns; ns.size = ui::type::kBody; ns.color = locked ? ui::theme::kTextDim : util::Mix(Rgb(row->color), ui::theme::kTextHi, 0.5f); ns.face = ui::Face::Strong;
		const float tx = icon.x + isz + 9.f;
		const float nw = ui::Fonts().Draw(row->name, Vector2{ tx, rr.y + rr.height * 0.5f - 9.f }, ns);
		// 놓인 칸
		std::string where;
		if (basicRow) where = kb::Label(kb::Act::BasicAttack);
		else if (const int h = HotOfSkill(row->id); h > 0) where = kb::Label(kb::HotAct(h));
		if (!where.empty())
		{
			const float kw = std::round(ui::Fonts().Measure(where, ui::type::kMicro, 0.f, ui::Face::Strong).x + 10.f);
			ui::Keycap(Rectangle{ tx + nw + 8.f, rr.y + rr.height * 0.5f - 8.f, kw, 16.f }, where, false);
		}
		// 오른쪽: 레벨 · 갈래 점 / 배움 레벨
		const float rx = rr.x + rr.width - 8.f;
		if (locked)
			ui::Text(Fmt("Lv.%d", row->unlockLevel), Vector2{ rx, rr.y + rr.height * 0.5f - 7.f }, ui::type::kCaption, ui::theme::kBad, Align::Right);
		else if (SkillGrowth::Growable(row->id))
		{
			const SkillGrowth::State s = MyGrow(row->id);
			for (int tier = 1; tier >= 0; --tier)
			{
				const float dx = rx - 8.f - (1 - tier) * 12.f;
				const Vector2 c{ dx, rr.y + rr.height * 0.5f };
				const uint8_t pick = SkillGrowth::Active(s, tier);
				if (pick) DrawCircleV(c, 4.f, pick == 1 ? ui::theme::kDawn : ui::theme::kOldway);
				else DrawCircleLines(static_cast<int>(c.x), static_cast<int>(c.y), 3.5f, s.level >= SkillGrowth::kTierLevel[tier] ? ui::theme::kDawnHi : ui::theme::kTextMute);
			}
			ui::TextStyle ls; ls.size = 17.f; ls.color = s.level > 1 ? ui::theme::kTextHi : ui::theme::kTextDim; ls.face = ui::Face::NumberBold;
			const std::string lv = std::to_string(s.level);
			const float lw = ui::Fonts().Measure(lv, ls.size, 0.f, ui::Face::NumberBold).x;
			ui::Fonts().Draw(lv, Vector2{ rx - 36.f - lw, rr.y + rr.height * 0.5f - 10.f }, ls);
			ui::Text("Lv", Vector2{ rx - 38.f - lw, rr.y + rr.height * 0.5f - 6.f }, ui::type::kMicro, ui::theme::kTextMute, Align::Right);
		}
		else
			ui::Text(def && def->kind == SkillKind::Awaken ? "각성기" : (def && def->kind == SkillKind::Move ? "이동기" : (def && def->kind == SkillKind::Identity ? "고유 기술" : "기본 공격")),
				Vector2{ rx, rr.y + rr.height * 0.5f - 7.f }, ui::type::kCaption, ui::theme::kTextMute, Align::Right);
		if (hover && !ui::Dragging()) hoverSkill = row->id;
		if (hover && ui::Pressed(rr) && !ui::Pressed(icon)) { m_growSel = row->id; sfx::Play(sfx::Id::Click, 0.35f); }
		// 끌기(기본 공격은 고정) — 아이콘을 잡고
		if (!basicRow && ui::Pressed(icon))
		{
			m_growSel = row->id;
			m_pressKind = kDragSkill; m_pressIndex = -1 - i; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == kDragSkill && m_pressIndex == -1 - i && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ kDragSkill, -1, row->id });
			m_pressKind = 0;
		}
	}
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && m_pressKind == kDragSkill)
		m_pressKind = 0;

	// ── 오른쪽: 고른 기술 ──
	const Rectangle pr{ r.x + 12.f + listW + 12.f, y0, r.width - listW - 36.f, r.y + r.height - 16.f - y0 };
	const uint16_t shown = hoverSkill != 0 ? hoverSkill : m_growSel;
	DrawSkillGrowPanel(pr, shown);
	m_previewSkill = shown;
}

void Game::DrawSkillGrowPanel(Rectangle pr, uint16_t skillId)
{
	const SkillRow* row = SkillTable::Find(skillId);
	const SkillDef* base = SkillDefTable::Find(skillId);
	if (!row || !base) return;
	ui::Plate(pr, ui::theme::kSurface1, ui::theme::kLine, 4.f, 1.f);
	const SkillGrowth::State st = MyGrow(skillId);
	const SkillDef* def = MyDef(skillId);
	const SkillRow* erow = skillview::Row(skillId, MyVarBits(skillId));
	const bool growable = SkillGrowth::Growable(skillId);
	const bool locked = m_level < row->unlockLevel;
	const Color cc = Rgb(row->color);
	const float x = pr.x + 14.f;
	float y = pr.y + 12.f;

	// 시연(오른쪽 위): 갈래 칸에 마우스를 올리면 그 갈래를 입힌 모습 — 고르기 전에 본다
	const float stW = std::round(std::clamp(pr.width * 0.46f, 220.f, 300.f));
	const float stH = std::round(stW * 232.f / 432.f);
	const Rectangle stageR{ pr.x + pr.width - 12.f - stW, pr.y + 12.f, stW, stH };
	const float colW = stageR.x - 12.f - x;   // 시연 옆 글 칸 너비
	uint8_t stageBits = MyVarBits(skillId);
	std::string stageCap = "시연";

	// 머리: 아이콘 · 이름 · 유형
	const float isz = 54.f;
	ui::Plate(Rectangle{ x - 2, y - 2, isz + 4, isz + 4 }, Color{ 10, 12, 15, 245 }, util::WithA(cc, 0.7f), 3.f, 1.f);
	skillfx::DrawSkillIcon(skillId, Vector2{ x + isz * 0.5f, y + isz * 0.5f }, isz - 4.f, m_time, locked);
	{
		ui::TextStyle ns; ns.size = ui::type::kDisplayS; ns.color = util::Mix(cc, ui::theme::kTextHi, 0.45f); ns.face = ui::Face::Display;
		ui::Fonts().Draw(row->name, Vector2{ x + isz + 12.f, y + 2.f }, ns);
		ui::TextWrapped(def->kindText[0] ? def->kindText : row->desc, Rectangle{ x + isz + 12.f, y + 30.f, colW - isz - 12.f, 30.f }, ui::type::kCaption, util::Mix(cc, ui::theme::kText, 0.55f), 2.f);
	}
	y += isz + 14.f;

	// 레벨 줄
	const bool busy = m_now < m_growBusyUntil;
	if (growable)
	{
		ui::TextStyle ls; ls.size = 30.f; ls.color = ui::theme::kTextHi; ls.face = ui::Face::NumberBold;
		ui::Text("기술 레벨", Vector2{ x, y + 10.f }, ui::type::kCaption, ui::theme::kTextDim);
		const float lx = x + 64.f;
		const float lw = ui::Fonts().Draw(std::to_string(st.level), Vector2{ lx, y - 2.f }, ls);
		ui::Text(Fmt("/ %d", SkillGrowth::kMaxLevel), Vector2{ lx + lw + 6.f, y + 10.f }, ui::type::kCaption, ui::theme::kTextMute);
		const float bx = lx + lw + 44.f;
		const bool canDown = !locked && !busy && st.level > 1;
		const uint8_t cost = SkillGrowth::StepCost(st.level);
		const bool canUp = !locked && !busy && st.level < SkillGrowth::kMaxLevel && m_skillPoints >= cost;
		if (ui::Button(Rectangle{ bx, y + 2.f, 30.f, 28.f }, "−", ButtonKind::Normal, canDown, 18.f))
			SendSkillGrow(SkillGrowOp::LevelDown, skillId);
		if (ui::Button(Rectangle{ bx + 36.f, y + 2.f, 30.f, 28.f }, "+", ButtonKind::Primary, canUp, 18.f))
			SendSkillGrow(SkillGrowOp::LevelUp, skillId);
		std::string next;
		if (locked) next = Fmt("Lv.%d에 배웁니다", row->unlockLevel);
		else if (st.level >= SkillGrowth::kMaxLevel) next = "가장 높은 레벨";
		else next = Fmt("다음 레벨 %d점", cost);
		ui::Text(next, Vector2{ bx + 76.f, y + 3.f }, ui::type::kCaption, !locked && st.level < SkillGrowth::kMaxLevel && m_skillPoints < cost ? ui::theme::kBad : ui::theme::kTextDim);
		ui::Text(Fmt("피해 +%u%%", SkillGrowth::DamagePercent(st.level) - 100u), Vector2{ bx + 76.f, y + 18.f }, ui::type::kCaption, st.level > 1 ? ui::theme::kDawnHi : ui::theme::kTextMute);
		y += 42.f;
	}
	else
	{
		y += ui::TextWrapped("기본 공격 · 각성기 · 이동기 · 고유 기술은 레벨이 없다. 장비의 기술 강화로 세진다.",
			Rectangle{ x, y, colW, 40.f }, ui::type::kCaption, ui::theme::kTextDim, 2.f) + 6.f;
	}

	// 수치 한 줄(갈래를 입힌 값)
	{
		std::string meta;
		if (row->mp > 0) meta = Fmt("MP %d", row->mp);
		if (def->kind != SkillKind::Basic)
			meta += (meta.empty() ? "" : " · ") + Fmt("재사용 %.1f초", SkillDefTable::CooldownMs(*def, *erow, m_stats.cooldownReduction) / 1000.f);
		const int range = def->range > 0 ? def->range : row->range;
		if (range > 0) meta += (meta.empty() ? "" : " · ") + Fmt("사거리 %d칸", range);
		if (def->kind == SkillKind::Cast && def->castMs > 0) meta += Fmt(" · 준비 %.2f초", def->castMs / 1000.f);
		const bool magic = row->kind == DamageKind::Magic;
		const int power = magic ? m_stats.magicAttack : m_stats.attack;
		uint32_t coeff = erow->coeff;
		for (int i = 0; i < def->hitCount; ++i) coeff = std::max<uint32_t>(coeff, def->hits[i].coeff);
		if (coeff > 0)
			meta += Fmt(" · 한 타 약 %d", static_cast<int>(power * coeff / 100u * SkillGrowth::DamagePercent(st.level) / 100u));
		y += ui::TextWrapped(meta, Rectangle{ x, y, colW, 34.f }, ui::type::kCaption, ui::theme::kInfo, 2.f) + 4.f;
		// 여기부터는 시연 아래 — 넓게
		y = std::max(y, stageR.y + stageR.height + 10.f);
		y += ui::TextWrapped(row->desc, Rectangle{ x, y, pr.width - 28.f, 40.f }, ui::type::kCaption, ui::theme::kText, 2.f) + 4.f;
		if (row->combo && row->combo[0])
			y += ui::TextWrapped(std::string("연계 · ") + row->combo, Rectangle{ x, y, pr.width - 28.f, 34.f }, ui::type::kCaption, ui::theme::kDawn, 2.f) + 6.f;
	}

	// 갈래 Ⅰ · Ⅱ
	if (growable)
	{
		const SkillGrowth::Tree* tree = SkillGrowth::FindTree(skillId);
		const float cw = std::floor((pr.width - 28.f - 10.f) * 0.5f);
		for (int tier = 0; tier < 2 && tree; ++tier)
		{
			const bool open = st.level >= SkillGrowth::kTierLevel[tier] && !locked;
			ui::Rule(x, pr.x + pr.width - 14.f, y + 2.f, ui::theme::kLine);
			ui::TextStyle hs; hs.size = ui::type::kHeading; hs.color = open ? ui::theme::kTitle : ui::theme::kTextMute; hs.face = ui::Face::Strong;
			const float hw = ui::Fonts().Draw(SkillGrowth::TierName(tier), Vector2{ x, y + 10.f }, hs);
			ui::Text(open ? "하나를 고른다 · 다시 누르면 끈다" : Fmt("기술 레벨 %d에 열린다", SkillGrowth::kTierLevel[tier]),
				Vector2{ x + hw + 10.f, y + 12.f }, ui::type::kCaption, open ? ui::theme::kTextDim : ui::theme::kTextMute);
			y += 34.f;
			const float ch = 58.f;
			for (int k = 0; k < 2; ++k)
			{
				const SkillGrowth::Variant& v = tree->v[tier][k];
				const Rectangle cr{ x + k * (cw + 10.f), y, cw, ch };
				m_growCardRect[tier][k] = cr;
				const bool chosen = st.pick[tier] == k + 1 && open;
				const bool hover = open && ui::Hovered(cr);
				const Color accent = v.color != 0 ? Rgb(v.color) : (k == 0 ? ui::theme::kDawn : ui::theme::kOldway);
				ui::Plate(cr, chosen ? util::Mix(ui::theme::kSurface2, accent, 0.16f) : (hover ? ui::theme::kSurface2 : ui::theme::kSurface0),
					chosen ? accent : (hover ? ui::theme::kLineStrong : ui::theme::kLine), 3.f, chosen ? 2.f : 1.f);
				ui::TextStyle vs; vs.size = ui::type::kBody; vs.face = ui::Face::Strong;
				vs.color = !open ? ui::theme::kTextMute : (chosen ? util::Mix(accent, ui::theme::kTextHi, 0.35f) : ui::theme::kTextHi);
				ui::Fonts().Draw(v.name, Vector2{ cr.x + 10.f, cr.y + 7.f }, vs);
				if (chosen)
				{
					// 고른 표시(작은 새김 마름모)
					const Vector2 c{ cr.x + cr.width - 14.f, cr.y + 14.f };
					DrawPoly(c, 4, 6.f, 45.f, accent);
				}
				ui::TextWrapped(v.desc, Rectangle{ cr.x + 10.f, cr.y + 27.f, cr.width - 18.f, 28.f }, ui::type::kCaption, open ? ui::theme::kText : ui::theme::kTextMute, 1.f);
				// 마우스를 올리면 시연이 이 갈래로(잠긴 갈래도 미리 본다)
				if (ui::Hovered(cr) && !chosen)
				{
					SkillGrowth::State pv = st;
					pv.level = std::max<uint8_t>(pv.level, SkillGrowth::kTierLevel[tier]);
					pv.pick[tier] = static_cast<uint8_t>(k + 1);
					stageBits = SkillGrowth::NfyBits(pv);
					stageCap = std::string("미리 보기 · ") + v.name;
				}
				if (open && !busy && ui::Clicked(cr))
					SendSkillGrow(SkillGrowOp::Pick, skillId, static_cast<uint8_t>(tier), static_cast<uint8_t>(chosen ? 0 : k + 1));
			}
			y += ch + 10.f;
		}
	}
	DrawSkillStage(stageR, skillId, stageBits, stageCap.c_str());
	(void)base;
}
