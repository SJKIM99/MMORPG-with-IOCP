#include "game/Game.h"
#include <cstdlib>
#include <cstdio>
#include "render/Height.h"
#include "game/Input.h"
#include "game/KeyBinds.h"
#include "ui/Wm.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "audio/Sfx.h"
#include "platform/Ime.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

// ─────────────────────────────────────────────────────────────────────────────
// 조작(Phase 1-A) — 로스트아크식 배치 · 리니지식 자동 공격.
//
//   좌클릭(이동 버튼)   바닥: 그 칸까지 · 누르고 있으면 커서 쪽으로 계속
//                      적: 다가가 기본 공격 반복 · NPC: 대화 · 물건: 줍기 · 모험가: 따라가기(Ctrl = 공격)
//   우클릭 · C          기본 공격(커서에 가까운 적, 없으면 커서 쪽으로 휘두름). C를 누르고 있으면 가까운 적을 이어서
//   Q W E R · A S D F   기술 8칸 — 커서 방향 · 커서 지점 · 커서에 가까운 적으로
//   V · Space · Z       각성기 · 이동기 · 고유 기술
//   1~4 · F1 · 5~8      전투 칸 · 체력 물약 · 보조 칸
//   G                   가장 가까운 대상과 상호작용(대화 · 줍기 · 쓰다듬기)
// 키는 모두 game/KeyBinds로 다시 지정할 수 있고, 채팅 중(한글 조합 포함)에는 먹지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;

namespace
{
	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	// 직업별 기본 배치는 공유 표(SkillDefTable::Default — C Q W E R A S D F V Space Z)가 정한다.

	// 칸 좌표 방향 → 8방향 중 가장 가까운 것
	void Snap8(Vector2 d, int& dx, int& dy)
	{
		if (std::fabs(d.x) < 1e-4f && std::fabs(d.y) < 1e-4f) { dx = dy = 0; return; }
		const float a = std::atan2(d.y, d.x);
		const int k = static_cast<int>(std::lround(a / (util::kPi * 0.25f)));
		dx = static_cast<int>(std::lround(std::cos(k * util::kPi * 0.25f)));
		dy = static_cast<int>(std::lround(std::sin(k * util::kPi * 0.25f)));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 기술 칸
// ─────────────────────────────────────────────────────────────────────────────
void Game::InitHotbar(bool loadSaved)
{
	m_hot = {};
	m_hotClass = m_classId;
	for (int i = 0; i < kHotSlots && i < kSkillKeys; ++i)
		m_hot[static_cast<size_t>(i)] = SkillDefTable::Default(m_classId, static_cast<SkillKey>(i));
	// 캐릭터별로 바꿔 둔 배치(기술 창에서 끌어다 놓은 것) — 이 직업 기술만 받는다
	if (!loadSaved || m_playerName.empty()) return;
	std::ifstream in("hotbar_" + m_playerName + ".txt");
	if (!in) return;
	std::array<uint16_t, kHotSlots> saved{};
	for (int i = 0; i < kHotSlots; ++i)
	{
		int id = 0;
		if (!(in >> id)) return;
		const SkillRow* row = SkillTable::Find(static_cast<uint16_t>(id));
		const SkillDef* def = SkillDefTable::Find(static_cast<uint16_t>(id));
		if (id != 0 && (!row || !def || row->cls != m_classId || (i == kHotBasic) != (def->kind == SkillKind::Basic))) return;
		saved[static_cast<size_t>(i)] = static_cast<uint16_t>(id);
	}
	if (saved[kHotBasic] == 0) return;
	// 예전(6기술) 배치를 읽었으면: 빈 칸에 아직 없는 기술을 기본 자리대로 채운다(Phase 2-B 새 기술)
	for (int i = 0; i < kHotSlots && i < kSkillKeys; ++i)
	{
		const uint16_t def = SkillDefTable::Default(m_classId, static_cast<SkillKey>(i));
		if (saved[static_cast<size_t>(i)] != 0 || def == 0) continue;
		if (std::find(saved.begin(), saved.end(), def) == saved.end()) saved[static_cast<size_t>(i)] = def;
	}
	m_hot = saved;
}

void Game::SaveHotbar()
{
	if (m_playerName.empty() || m_hotClass == ClassId::Count) return;
	std::ofstream out("hotbar_" + m_playerName + ".txt");
	for (const uint16_t id : m_hot)
		out << id << '\n';
}

const SkillRow* Game::HotSkill(int hot) const
{
	if (hot < 0 || hot >= kHotSlots) return nullptr;
	return SkillTable::Find(m_hot[static_cast<size_t>(hot)]);
}

int Game::HotOfSkill(uint16_t skillId) const
{
	for (int i = 0; i < kHotSlots; ++i)
		if (m_hot[static_cast<size_t>(i)] == skillId) return i;
	return -1;
}

int Game::MoveButton() const { return kb::Opt().swapMouse ? MOUSE_BUTTON_RIGHT : MOUSE_BUTTON_LEFT; }
int Game::AttackButton() const { return kb::Opt().swapMouse ? MOUSE_BUTTON_LEFT : MOUSE_BUTTON_RIGHT; }

// ─────────────────────────────────────────────────────────────────────────────
// 커서 조준 — 매 프레임 커서의 월드 칸 · 방향 · 커서에 가장 가까운 적
// ─────────────────────────────────────────────────────────────────────────────
bool Game::IsEnemy(const Entity& e) const
{
	if (e.dead || e.removing || e.id == m_selfId) return false;
	if (e.kind == EntityKind::Monster) return true;
	if (e.kind == EntityKind::Player)
	{
		// 다른 모험가는 강제 공격(Ctrl)일 때만, 마을 밖에서만(서버 PK 규칙과 같다)
		const bool force = kb::Down(kb::Act::ForceAttack) || (m_goal.kind == GoalKind::Attack && m_goal.force && m_goal.target == e.id);
		return force && !WorldData::IsSafeZone(e.tx, e.ty) && !InTown();
	}
	return false;
}

void Game::SetScriptAim(int tx, int ty, const ObjID& enemy)
{
	m_aimScripted = true;
	m_aim.tx = tx;
	m_aim.ty = ty;
	m_aim.world = Vector2{ static_cast<float>(tx), static_cast<float>(ty) };
	m_aim.enemy = enemy;
	m_aim.valid = true;
	const Vector2 d{ static_cast<float>(tx - m_predX), static_cast<float>(ty - m_predY) };
	if (util::Len(d) > 0.01f) m_aim.dir = util::Norm(d);
}

void Game::UpdateAim()
{
	Entity* me = Self();
	const Vector2 origin = me ? me->pos : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	if (!m_aimScripted)
	{
		const Vector2 mouseIso = GetScreenToWorld2D(in::MousePos(), m_cam2d);
		m_aim.world = hgt::Pick(mouseIso);   // 높은 지형이 앞을 가리면 그 면의 칸(Phase 4-A)
		m_aim.tx = static_cast<int>(std::floor(m_aim.world.x + 0.5f));
		m_aim.ty = static_cast<int>(std::floor(m_aim.world.y + 0.5f));
		m_aim.valid = true;
		const Vector2 d = util::Sub(m_aim.world, origin);
		if (util::Len(d) > 0.05f) m_aim.dir = util::Norm(d);
		// 커서에 가장 가까운 적: 몸통 위에 커서가 있으면 그것, 아니면 커서에서 2.5칸 안의 가장 가까운 것
		// (커서가 창 · HUD 위면 없음 — 방향만 쓴다)
		m_aim.enemy = ObjID{};
		if (ui::MouseOverUi()) {}
		else if (Entity* h = Find(m_hover); h && IsEnemy(*h))
			m_aim.enemy = h->id;
		else
		{
			float best = 2.5f;
			for (auto& [id, e] : m_ents)
			{
				if (!IsEnemy(e)) continue;
				const float d2 = util::Len(util::Sub(e.pos, m_aim.world));
				if (d2 < best) { best = d2; m_aim.enemy = id; }
			}
		}
	}
	m_aimScripted = false;
	// 화면 기준 각도(0~255): 칸 → 화면(sx = x − y, sy = (x + y) / 2)
	const float sx = m_aim.dir.x - m_aim.dir.y, sy = (m_aim.dir.x + m_aim.dir.y) * 0.5f;
	float a = std::atan2(sy, sx);
	if (a < 0.f) a += 2.f * util::kPi;
	m_aim.dir256 = static_cast<uint8_t>(static_cast<int>(a / (2.f * util::kPi) * 256.f) & 255);
}

Entity* Game::AimEnemy(const SkillRow* row)
{
	Entity* t = Find(m_aim.enemy);
	if (t && IsEnemy(*t))
	{
		// 너무 멀면(사거리 + 다가갈 여유 밖) 대상으로 치지 않는다
		const int reach = row ? row->range + 8 : 12;
		if (Cheb(m_predX, m_predY, t->tx, t->ty) <= reach) return t;
	}
	return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// 입력 — 키보드(동작 표) · 마우스
// ─────────────────────────────────────────────────────────────────────────────
bool Game::CloseTopWindow()
{
	auto closed = [&](bool& flag) { flag = false; sfx::Play(sfx::Id::Close, 0.5f); return true; };
	if (m_keysOpen) { m_keysCapture = -1; kb::SetCapturing(false); return closed(m_keysOpen); }
	if (m_menuOpen) return closed(m_menuOpen);
	if (m_uiEdit) { m_uiEdit = false; sfx::Play(sfx::Id::Close, 0.5f); return true; }
	if (m_enchant.stage == 1 || m_enchant.stage == 2) { m_enchant = EnchantFlow{}; return true; }
	if (m_discard.open) { m_discard.open = false; return true; }
	// 창 관리자(Phase 3-A): 겹침 순서의 맨 위 열린 창부터(마지막에 누르거나 연 창)
	if (const char* top = wm::TopOpen())
	{
		CloseWindowById(top);
		return true;
	}
	if (m_dialogQuest != 0 && m_dialogNpc != 0) { m_dialogQuest = 0; m_dialogTextAt = m_now; return true; }   // 이야기 → NPC 첫 화면
	if (m_dialogNpc || m_shopMode != ShopMode::None) { CloseNpcWindows(); return true; }
	if (m_questLogOpen) return closed(m_questLogOpen);
	if (m_mapOpen) return closed(m_mapOpen);
	if (m_bigMapOpen) return closed(m_bigMapOpen);
	if (m_helpOpen) return closed(m_helpOpen);
	if (m_skillWinOpen) return closed(m_skillWinOpen);
	if (m_charOpen) return closed(m_charOpen);
	if (m_invOpen) return closed(m_invOpen);
	return false;
}

void Game::HandleHotkeys()
{
	if (kb::Pressed(kb::Act::DebugInfo))
		m_debugOverlay = !m_debugOverlay;

	// Esc: 채팅 · 표시 후 시전 · 키 지정 취소 → 맨 위 창 → 대상 · 목표 → 시스템 메뉴 (Esc는 다시 지정하지 않는다)
	if (IsKeyPressed(KEY_ESCAPE))
	{
		if (ui::TextFocused()) { ui::Focus(nullptr); m_chatOpen = false; return; }
		if (m_keysCapture >= 0) { m_keysCapture = -1; kb::SetCapturing(false); return; }
		if (m_aimingHot >= 0) { m_aimingHot = -1; return; }
		if (CloseTopWindow()) return;
		if (m_goal.kind != GoalKind::None || m_target != ObjID{})
		{
			m_target = ObjID{};
			ClearGoal();
			return;
		}
		m_menuOpen = true;
		sfx::Play(sfx::Id::Open, 0.5f);
		return;
	}
	if (kb::Blocked() || m_menuOpen || m_keysOpen)
		return;   // 채팅 · 한글 조합 · 키 지정 중, 메뉴 · 조작 설정(모달)이 떠 있으면 게임 키는 먹지 않는다

	// Enter: 채팅 · /: 명령어(입력칸이 / 로 시작)
	if (kb::Pressed(kb::Act::Chat) || kb::Pressed(kb::Act::Command))
	{
		const bool command = kb::Pressed(kb::Act::Command);
		m_chatOpen = true;
		m_chatOpenedAt = m_now;
		// 명령어: 입력칸이 / 로 시작한다(/ 키로 열었으면 그 글자가 이미 들어온다)
		if (command && m_chatField.text.empty() && !ui::TypedThisFrame('/')) m_chatField.text = "/";
		ui::Focus(&m_chatField);
		return;
	}
	if (m_dead)
		return;

	auto toggle = [&](bool& flag) { flag = !flag; sfx::Play(flag ? sfx::Id::Open : sfx::Id::Close, 0.6f); };
	if (kb::Pressed(kb::Act::Inventory)) toggle(m_invOpen);
	if (kb::Pressed(kb::Act::Character)) toggle(m_charOpen);
	if (kb::Pressed(kb::Act::SkillWindow)) toggle(m_skillWinOpen);
	if (kb::Pressed(kb::Act::QuestLog)) { toggle(m_questLogOpen); m_abandonArm = 0; }
	if (kb::Pressed(kb::Act::WorldMap)) toggle(m_mapOpen);
	if (kb::Pressed(kb::Act::BigMap)) toggle(m_bigMapOpen);
	if (kb::Pressed(kb::Act::Help)) toggle(m_helpOpen);
	if (kb::Pressed(kb::Act::Guild)) AddCenter("결사는 아직 문을 열지 않았습니다.", ui::theme::kTextDim);
	if (kb::Pressed(kb::Act::Auction)) AddCenter("경매장은 아직 문을 열지 않았습니다.", ui::theme::kTextDim);
	if (kb::Pressed(kb::Act::Interact)) Interact();
	if (kb::Pressed(kb::Act::ZoomIn)) m_cam.AdjustZoom(0.15f);
	if (kb::Pressed(kb::Act::ZoomOut)) m_cam.AdjustZoom(-0.15f);

	// 소모품 칸
	for (int i = 0; i < kItemSlots; ++i)
		if (kb::Pressed(kb::ItemAct(i)))
			UseItemSlot(i);

	// 기본 공격(C): 누르면 커서 쪽, 누르고 있으면 가까운 적을 이어서
	if (kb::Pressed(kb::Act::BasicAttack))
		BasicAttackAtAim(false);
	else if (kb::Down(kb::Act::BasicAttack) && m_goal.kind == GoalKind::None && m_now >= m_actionLockUntil)
		BasicAttackAtAim(true);

	// 기술 칸 — 빠른 시전: 누르는 순간 커서로 / 표시 후 시전: 누르는 동안 범위, 떼면 시전(우클릭 취소)
	const bool quick = kb::Opt().castMode == kb::CastMode::Quick;
	for (int hot = 1; hot < kHotSlots; ++hot)
	{
		const kb::Act act = kb::HotAct(hot);
		if (kb::Pressed(act))
		{
			if (quick || !HotSkill(hot)) CastHot(hot);
			else { m_aimingHot = hot; sfx::Play(sfx::Id::Hover, 0.35f); }
		}
		if (!quick && m_aimingHot == hot && !kb::Down(act))
		{
			m_aimingHot = -1;
			CastHot(hot);
		}
	}
}

void Game::PickUnderMouse()
{
	m_hover = ObjID{};
	m_hoverNpc = 0;
	m_hoverX = m_hoverY = -1;
	if (ui::MouseOverUi())
		return;
	const Vector2 mouseIso = GetScreenToWorld2D(in::MousePos(), m_cam2d);
	const Vector2 t = hgt::Pick(mouseIso);
	m_hoverX = static_cast<int>(std::floor(t.x + 0.5f));
	m_hoverY = static_cast<int>(std::floor(t.y + 0.5f));

	// 화면에서 몸통 사각형으로 고른다(앞쪽 = X+Y 큰 것 우선)
	float bestDepth = -1e9f;
	for (auto& [id, e] : m_ents)
	{
		if (id == m_selfId || e.removing || (e.dead && e.kind != EntityKind::Item) || e.flyT >= 0.f)
			continue;
		const Vector2 base = SurfaceIso(e.pos);
		float h = 60.f, w = 26.f;
		if (e.kind == EntityKind::Monster && e.row) { h = actors::MonsterHeight(*e.row); w = std::max(22.f, actors::MonsterShadow(*e.row) * 1.4f); }
		if (e.kind == EntityKind::Item) { h = 26.f; w = 20.f; }
		const Rectangle r{ base.x - w, base.y - h, w * 2.f, h + 8.f };
		if (CheckCollisionPointRec(mouseIso, r))
		{
			const float depth = e.pos.x + e.pos.y + (e.kind == EntityKind::Item ? -0.3f : 0.f);
			if (depth > bestDepth) { bestDepth = depth; m_hover = id; }
		}
	}
	if (m_hover == ObjID{})
	{
		float bestNpc = -1e9f;
		for (const auto& [nid, v] : m_npcView)
		{
			if (!v.row) continue;
			const Vector2 base = SurfaceIso(v.pos);
			const float h = actors::NpcHeight(*v.row);
			const float w = WorldData::IsAnimal(v.row->kind) ? 20.f : 22.f;
			const Rectangle r{ base.x - w, base.y - h, w * 2.f, h + 8 };
			const float depth = v.pos.x + v.pos.y;
			if (CheckCollisionPointRec(mouseIso, r) && depth > bestNpc) { bestNpc = depth; m_hoverNpc = nid; }
		}
	}
}

void Game::HandleWorldInput()
{
	PickUnderMouse();
	UpdateAim();
	if (m_dead || m_menuOpen || m_keysOpen || m_uiEdit)
		return;   // 메뉴 · 조작 설정 · UI 편집은 모달 — 어두운 바깥을 눌러도 월드로 가지 않는다

	const bool overUi = ui::MouseOverUi() || ui::MouseConsumed() || wm::Busy();
	const bool force = kb::Down(kb::Act::ForceAttack);
	const int moveBtn = MoveButton(), atkBtn = AttackButton();

	// 인벤토리(또는 장비창)에서 끌어다 월드에 놓으면 버리기 — 여러 개이거나 희귀 이상·착용 중이면 한 번 묻는다
	if (ui::Dragging() && ui::DragReleased() && !overUi && (ui::Drag().kind == 1 || ui::Drag().kind == 3))
	{
		const int slot = ui::Drag().index;
		const InvSlot& s = m_inv.slots[slot];
		if (!s.Empty())
		{
			const bool precious = s.equipped || SlotRarity(s) >= Rarity::Rare || s.enchant > 0;
			if (s.count > 1 || precious)
				m_discard = Discard{ true, slot, s.count };
			else
				m_net.SendDiscard(static_cast<uint16_t>(slot), 1);
		}
		ui::EndDrag();
		return;
	}

	// 강화 대상 고르기 · 키 지정 중에는 월드 클릭 무시
	if (m_enchant.stage == 1 || m_keysCapture >= 0)
		return;

	// 표시 후 시전 중 공격 버튼 = 취소
	if (m_aimingHot >= 0 && in::MousePressed(atkBtn))
	{
		m_aimingHot = -1;
		sfx::Play(sfx::Id::Close, 0.4f);
		return;
	}

	// ── 이동 버튼 ──
	if (in::MousePressed(moveBtn) && !overUi)
	{
		m_moveHeld = true;
		m_moveDirMode = false;
		m_movePressAt = m_now;
		m_movePressEnt = ObjID{};
		m_movePressNpc = 0;
		Entity* e = Find(m_hover);
		if (e && e->kind == EntityKind::Monster && !e->dead)
		{
			// 리니지식: 적을 누르면 다가가 기본 공격을 되풀이한다
			m_target = e->id;
			SetGoal(Goal{ GoalKind::Attack, e->id, 0, 0, 0, false });
			m_moveHeld = false;
		}
		else if (e && e->kind == EntityKind::Player && !e->dead)
		{
			if (force)
			{
				m_target = e->id;
				SetGoal(Goal{ GoalKind::Attack, e->id, 0, 0, 0, true });
			}
			else
			{
				SetGoal(Goal{ GoalKind::Follow, e->id });
				m_movePressEnt = e->id;   // 누른 채 커서가 그 사람을 벗어나면 달리기로
			}
			if (force) m_moveHeld = false;
		}
		else if (e && e->kind == EntityKind::Item)
		{
			SetGoal(Goal{ GoalKind::Pickup, e->id, e->tx, e->ty });
			fx::Fx().ClickMarker(TileToIso(e->pos), ui::theme::kDawnHi);
			m_movePressEnt = e->id;
		}
		else if (m_hoverNpc != 0)
		{
			// NPC를 누르면 다가가 대화 — 단, 누른 채 커서를 NPC 밖으로 끌면 그쪽으로 달린다
			// (달리다가 지나가는 NPC 위에서 눌러도 대화로 멈춰 서지 않게)
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, m_hoverNpc });
			m_movePressNpc = m_hoverNpc;
		}
		else if (m_hoverX >= 0)
		{
			// 바닥: 그 칸까지(못 가는 칸이면 가장 가까운 갈 수 있는 칸)
			int gx = m_hoverX, gy = m_hoverY;
			if (WorldData::IsBlocked(gx, gy))
			{
				int best = 1 << 30;
				for (int r = 1; r <= 4 && best == (1 << 30); ++r)
					for (int dy = -r; dy <= r; ++dy)
						for (int dx = -r; dx <= r; ++dx)
						{
							if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
							const int x = m_hoverX + dx, y = m_hoverY + dy;
							if (WorldData::IsBlocked(x, y)) continue;
							const int d = dx * dx + dy * dy + Cheb(x, y, m_predX, m_predY);
							if (d < best) { best = d; gx = x; gy = y; }
						}
			}
			SetGoal(Goal{ GoalKind::Move, ObjID{}, gx, gy });
			fx::Fx().ClickMarker(gfx::IsoPx(static_cast<float>(gx), static_cast<float>(gy)), ui::theme::kOldway);
		}
	}
	if (m_moveHeld)
	{
		if (!in::MouseDown(moveBtn))
		{
			// 손을 떼면: 누른 채 달리던 중이면 지금 걸음에서 멈춘다(클릭은 목적지까지 간다)
			if (m_moveDirMode && m_goal.kind == GoalKind::Direction)
				ClearGoal();
			m_moveHeld = false;
			m_moveDirMode = false;
		}
		else if (m_aim.valid && (m_moveDirMode || m_now - m_movePressAt > 0.16) &&
			(m_moveDirMode || !((m_movePressNpc != 0 && m_hoverNpc == m_movePressNpc) || (m_movePressEnt != ObjID{} && m_hover == m_movePressEnt))))
		{
			// 누르고 있으면 커서 쪽으로 계속(목적지 클릭 → 방향 이동으로).
			// NPC · 물건 · 모험가를 눌렀으면 커서가 그 위에 있는 동안은 대화 · 줍기 · 따라가기를 지킨다.
			if (!m_moveDirMode || m_goal.kind != GoalKind::Direction)
			{
				m_goal = Goal{ GoalKind::Direction };
				m_path.clear();
				m_queuedSkill = -1;
				m_lineValid = false;
			}
			m_moveDirMode = true;
			m_dirTarget = m_aim.world;
		}
	}

	// ── 공격 버튼(우클릭): 기본 공격 ──
	if (!overUi)
	{
		if (in::MousePressed(atkBtn))
			BasicAttackAtAim(false);
		else if (in::MouseDown(atkBtn) && m_goal.kind == GoalKind::None && m_now >= m_actionLockUntil)
			BasicAttackAtAim(true);
	}
	// 휠 확대 · 축소는 UpdateGame(카메라 갱신 직전)에서 — 제한 범위는 GameCamera
}

// ─────────────────────────────────────────────────────────────────────────────
// 기본 공격 · 기술
// ─────────────────────────────────────────────────────────────────────────────
void Game::BasicAttackAtAim(bool hold)
{
	if (m_dead || CombatBlocked())
	{
		if (!hold && CombatBlocked()) AddCenter("마을 안에서는 싸울 수 없습니다.", Rgb(0xFFD070));
		return;
	}
	const SkillRow* basic = SkillInSlot(0);
	Entity* t = AimEnemy(basic);
	if (!t && hold)
	{
		// C를 누르고 있으면: 커서 근처에 없으면 가장 가까운 적(예전 Space 편의)
		float best = 1e9f;
		for (auto& [id, e] : m_ents)
		{
			if (!IsEnemy(e) || e.kind != EntityKind::Monster) continue;
			const float d = static_cast<float>(Cheb(m_predX, m_predY, e.tx, e.ty)) + (id == m_target ? -0.5f : 0.f);
			if (d < best && d <= 9.f) { best = d; t = &e; }
		}
	}
	if (t)
	{
		m_target = t->id;
		if (m_goal.kind != GoalKind::Attack || m_goal.target != t->id)
			SetGoal(Goal{ GoalKind::Attack, t->id, 0, 0, 0, t->kind == EntityKind::Player });
		return;
	}
	if (hold)
		return;
	// 대상이 없으면 커서 쪽으로 휘두른다 — 콤보도 이어지고, 서버가 부채꼴 안의 적을 판정한다
	Entity* me = Self();
	if (!me || m_now < m_actionLockUntil) return;
	m_whiffAt = m_now;
	TryCastSkill(SkillDefTable::Default(m_classId, SkillKey::C), true);
}

void Game::CastHot(int hot)
{
	if (hot >= 0 && hot < kHotSlots) m_hotPressAt[static_cast<size_t>(hot)] = m_now;   // 늘 눌림 반응(Phase 3-C)
	const SkillRow* row = HotSkill(hot);
	if (!row)
	{
		sfx::Play(sfx::Id::Error, 0.25f);
		return;
	}
	if (const SkillDef* def = SkillDefTable::Find(row->id); def && def->kind == SkillKind::Basic)
	{
		BasicAttackAtAim(false);
		return;
	}
	TryCastSkill(row->id);
}

// ─────────────────────────────────────────────────────────────────────────────
// 소모품 칸(전투 1~4 · 물약 F1 · 보조 5~8) — 캐릭터별 저장
// ─────────────────────────────────────────────────────────────────────────────
void Game::UseItemSlot(int index)
{
	if (index < 0 || index >= kItemSlots) return;
	const QuickSlot& q = m_quick[static_cast<size_t>(index)];
	if (q.kind != 1) return;
	const int slot = m_inv.FirstSlotOf(q.itemId);
	if (slot < 0)
	{
		AddCenter(texts::Josa(texts::ItemName(q.itemId), "이", "가") + " 없습니다.", Rgb(0xC8C8C8));
		sfx::Play(sfx::Id::Error, 0.4f);
		return;
	}
	UseSlot(slot);
}

void Game::LoadQuickSlots()
{
	// 기본: 전투 1 진한 약초 물약 · 2 수액 영약 · 3 바람걸음 물약 · 물약(F1) 약초 물약 · 보조 5 귀환 · 6 옛길 두루마리
	m_quick = {};
	m_quick[0] = { 1, ItemTableData::Id::StrongHerbPotion };
	m_quick[1] = { 1, ItemTableData::Id::SapElixir };
	m_quick[2] = { 1, ItemTableData::Id::WindstepPotion };
	m_quick[kPotionSlot] = { 1, ItemTableData::Id::HerbPotion };
	m_quick[5] = { 1, ItemTableData::Id::ReturnScroll };
	m_quick[6] = { 1, ItemTableData::Id::OldwayScroll };
	auto readInto = [&](std::ifstream& in, int count)
	{
		for (int i = 0; i < count; ++i)
		{
			int kind = 0, item = 0;
			if (!(in >> kind >> item)) break;
			QuickSlot& q = m_quick[static_cast<size_t>(i)];
			q.kind = kind == 1 ? 1 : 0;
			q.itemId = q.kind ? static_cast<uint16_t>(item) : 0;
		}
	};
	if (std::ifstream in("quickslots3_" + m_playerName + ".txt"); in)
	{
		readInto(in, kItemSlots);
		return;
	}
	// 마이그레이션: 예전 Q E R T 네 칸(quickslots2) → 전투 칸 1~4. 예전 파일은 지우지 않는다.
	if (std::ifstream old("quickslots2_" + m_playerName + ".txt"); old)
	{
		for (int i = 0; i < 4; ++i) m_quick[static_cast<size_t>(i)] = QuickSlot{};
		readInto(old, 4);
		SaveQuickSlots();
	}
}

void Game::SaveQuickSlots()
{
	if (m_playerName.empty() || !m_hasSelf) return;
	std::ofstream out("quickslots3_" + m_playerName + ".txt");
	for (const auto& q : m_quick)
		out << q.kind << ' ' << q.itemId << '\n';
}

void Game::LoadControls()
{
	kb::Load(m_playerName);
	LoadQuickSlots();
	LoadUiLayout();
}

void Game::SaveControls()
{
	if (m_playerName.empty() || !m_hasSelf) return;
	kb::Save(m_playerName);
	SaveQuickSlots();
	SaveHotbar();
}

// ─────────────────────────────────────────────────────────────────────────────
// 이동 예측 · 목표
// ─────────────────────────────────────────────────────────────────────────────
void Game::SetGoal(const Goal& g)
{
	m_goal = g;
	m_path.clear();
	m_repathAt = 0.0;
	m_wayValid = false;
	m_lineValid = false;
	m_wantAt = 0.0;
}

void Game::ClearGoal()
{
	m_goal = Goal{};
	m_path.clear();
	m_queuedSkill = -1;
	m_wayValid = false;
	m_lineValid = false;
}

void Game::SyncMover()
{
	m_srvX = m_mover.SrvX();
	m_srvY = m_mover.SrvY();
	m_predX = m_mover.PredX();
	m_predY = m_mover.PredY();
	if (Entity* me = Self())
	{
		me->tx = m_srvX;
		me->ty = m_srvY;
	}
}

bool Game::SendStep(int dir)
{
	uint16_t seq = 0;
	if (!m_mover.Step(m_now, dir, seq))
	{
		m_path.clear();
		return false;
	}
	m_net.SendMoveEx(dir, seq);
	SyncMover();
	return true;
}

int Game::PickMoveDir(int dx, int dy) const
{
	const int want = MoveRule::DirOf(dx, dy);
	if (want < 0)
		return -1;
	if (m_mover.WouldMove(want))
	{
		// 대각선인데 한 축만 움직인다(벽을 따라 미끄러짐) → 그 축 방향 걸음으로 보낸다.
		// 대각선 비용(√2)을 내고 한 칸만 가면 벽 옆에서 느려지기 때문.
		int x = m_predX, y = m_predY;
		MoveRule::Apply(x, y, want);
		const int straight = MoveRule::DirOf(x - m_predX, y - m_predY);
		// 두 축 다 움직이지만 한 옆이 막혔다 — 서버는 열린 옆 칸(Y 먼저)을 거쳐 가도 화면은 대각으로 그어 모서리
		// (절벽 가장자리 난간 · 담 끝)를 스친다. 서버와 같은 Y 걸음을 먼저 보내고 다음 걸음에서 X로 돈다(Phase 4-A).
		if (straight == want && dx != 0 && dy != 0 && !path::CanStep(m_predX, m_predY, dx, dy))
			return MoveRule::DirOf(0, dy);
		return straight >= 0 ? straight : want;
	}
	// 막혔다 — 원하는 방향과 가장 가까운(45° 안쪽) 열린 방향으로 미끄러진다.
	const float wl = std::sqrt(static_cast<float>(dx * dx + dy * dy));
	int best = -1;
	float bestDot = 0.55f;
	for (int d = 0; d < 8; ++d)
	{
		if (d == want)
			continue;
		int x = m_predX, y = m_predY;
		MoveRule::Apply(x, y, d);
		const int mx = x - m_predX, my = y - m_predY;
		if (mx == 0 && my == 0)
			continue;
		const float ml = std::sqrt(static_cast<float>(mx * mx + my * my));
		const float dot = (mx * dx + my * dy) / (ml * wl);
		if (dot > bestDot)
		{
			bestDot = dot;
			best = d;
		}
	}
	return best;
}

// ─────────────────────────────────────────────────────────────────────────────
// 걸음 계획(Phase 1-C) — 연속 경로(커서 광선 · 목적지로 가는 선)를 8방향 걸음으로(DDA).
// 대각선은 두 직교 이웃이 모두 비었을 때만(path::CanStep — 서버의 축별 미끄러짐과 결과가 같다).
// ─────────────────────────────────────────────────────────────────────────────
int Game::PlanDir(int x, int y, Vector2 origin, Vector2 u) const
{
	// 앞으로 나아가는 걸음(선 방향과 67° 안) 중 새 칸 중심이 선에 가장 가까운 것 — 같으면 더 앞으로
	int best = -1;
	float bestScore = 1e9f;
	for (int d = 0; d < 8; ++d)
	{
		int dx = 0, dy = 0;
		MoveRule::Delta(d, dx, dy);
		const float len = (dx != 0 && dy != 0) ? 1.41421356f : 1.f;
		const float fwd = (dx * u.x + dy * u.y) / len;
		if (fwd < 0.38f || !path::CanStep(x, y, dx, dy)) continue;
		const float rx = static_cast<float>(x + dx) - origin.x, ry = static_cast<float>(y + dy) - origin.y;
		const float score = std::fabs(rx * u.y - ry * u.x) - fwd * 0.05f;
		if (score < bestScore) { bestScore = score; best = d; }
	}
	return best;
}

bool Game::RunLine(Vector2 target, Vector2& origin, Vector2& u)
{
	// 곧은 선 하나를 걸음 계획(DDA)과 화면 안내선이 함께 쓴다. 한 번 그으면 가려는 방향이 5° 넘게 바뀌거나
	// (커서 · 경유점이 움직임) 칸 · 화면 위치가 선에서 벗어날 때만 지금 화면 위치에서 다시 긋는다.
	// 매 걸음 화면 위치를 원점으로 새로 그으면 화면 ↔ 논리 차이가 선을 흔들어 걸음이 좌우로 흔들린다(1-C 검증에서 찾음).
	const Vector2 P = m_mover.Visual();
	const Vector2 d = util::Sub(target, P);
	const float l = util::Len(d);
	if (l < 0.3f)
	{
		if (!m_lineValid) return false;
		origin = m_lineO;
		u = m_lineU;
		return true;
	}
	const Vector2 want = util::Mul(d, 1.f / l);
	// 가려는 방향이 도는 속도(원을 그리며 달리기 · 움직이는 대상) — 걸음 계획이 한 걸음 앞을 내다보는 데 쓴다
	if (m_now > m_wantAt + 1e-6)
	{
		const double gap = m_now - m_wantAt;
		if (m_wantAt > 0.0 && gap < 0.25)
		{
			const float da = std::atan2(m_wantPrev.x * want.y - m_wantPrev.y * want.x, util::Dot(m_wantPrev, want));
			m_wantTurn += (da / static_cast<float>(gap) - m_wantTurn) * (1.f - std::exp(-static_cast<float>(gap) / 0.15f));
		}
		else
			m_wantTurn = 0.f;
		m_wantPrev = want;
		m_wantAt = m_now;
	}
	bool keep = m_lineValid && util::Dot(want, m_lineU) > 0.9962f;   // cos 5°
	if (keep)
	{
		const Vector2 n{ -m_lineU.y, m_lineU.x };
		const Vector2 L{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
		keep = std::fabs(util::Dot(util::Sub(L, m_lineO), n)) < 0.75f && std::fabs(util::Dot(util::Sub(P, m_lineO), n)) < 0.45f;
	}
	if (!keep)
	{
		m_lineO = P;
		m_lineU = want;
		m_lineValid = true;
	}
	origin = m_lineO;
	u = m_lineU;
	return true;
}

Vector2 Game::PlanU(Vector2 u) const
{
	// 누른 채 달리기(커서 쪽)에서만 쓴다 — 경유점 · 움직이는 대상은 방향이 걸음마다 뛰어서 회전 속도가 의미 없다.
	// 선의 방향은 5°마다만 다시 긋지만 가려는 쪽은 계속 돈다(원 그리기: 초당 약 40°). 0.1초 뒤의 방향(최대 ±20°)으로
	// 고른다 — 지금 방향만 보면 가끔 "위 → 오른쪽" ㄱ자 두 걸음을 고르고(대각 한 걸음 대신) 그만큼 논리가 늦어져 화면이
	// 선을 따라가다 끌려간다(1-C 검증: 부활 칸 10곳에서 원 그리기 — 멈칫 1~4프레임이 나던 2곳이 0으로). 0.2초는 원을 너무 안쪽으로 깎는다.
	const float a = std::clamp(m_wantTurn * 0.1f, -0.35f, 0.35f);
	return std::fabs(a) < 1e-4f ? u : util::Rot(u, a);
}

bool Game::LineClear(int x, int y, int gx, int gy, int stopWithin) const
{
	// (x,y) → (gx,gy) 곧은 선을 DDA 걸음으로 막힘 없이 따라갈 수 있나(도착 = 목표에서 stopWithin 안)
	const Vector2 origin{ static_cast<float>(x), static_cast<float>(y) };
	const Vector2 d{ static_cast<float>(gx - x), static_cast<float>(gy - y) };
	const float l = util::Len(d);
	if (l < 0.5f) return true;
	const Vector2 u = util::Mul(d, 1.f / l);
	const int maxSteps = Cheb(x, y, gx, gy) + 2;
	for (int i = 0; i < maxSteps; ++i)
	{
		if (Cheb(x, y, gx, gy) <= stopWithin) return true;
		int dir = -1;
		if (Cheb(x, y, gx, gy) <= 1)
		{
			const int ddx = gx - x, ddy = gy - y;
			if (path::CanStep(x, y, ddx, ddy)) dir = MoveRule::DirOf(ddx, ddy);
		}
		if (dir < 0) dir = PlanDir(x, y, origin, u);
		if (dir < 0) return false;
		int dx = 0, dy = 0;
		MoveRule::Delta(dir, dx, dy);
		x += dx;
		y += dy;
	}
	return Cheb(x, y, gx, gy) <= stopWithin;
}

bool Game::Waypoint(int gx, int gy, int stopWithin, int& wx, int& wy)
{
	// 곧게 갈 수 있으면 목적지 그대로(대부분)
	if (LineClear(m_predX, m_predY, gx, gy, stopWithin))
	{
		wx = gx;
		wy = gy;
		m_path.clear();
		return true;
	}
	// 막혔으면 A* 경로(1초마다 · 목적지가 바뀌면 다시)를 시야선으로 줄여 곧게 보이는 가장 먼 칸으로
	if (m_path.empty() || m_now > m_repathAt || m_pathGoalX != gx || m_pathGoalY != gy)
	{
		std::vector<path::Step> np = path::Find(m_predX, m_predY, gx, gy, stopWithin);
		// 길 고수: 움직이는 대상이 장애물 건너편에 있으면 위로 도는 길과 아래로 도는 길이 번갈아 조금씩 짧아져
		// 제자리에서 오르내린다(1-C 따라가기 녹화에서 찾음). 지금 길(남은 길 + 그 끝에서 새 목표까지)이 새 길보다
		// 3칸 넘게 길지 않으면 지금 길을 그대로 간다.
		auto cost = [&](const std::vector<path::Step>& p, size_t from)
		{
			float c = 0.f;
			int x = m_predX, y = m_predY;
			for (size_t i = from; i < p.size(); ++i)
			{
				c += (p[i].x != x && p[i].y != y) ? 1.41421356f : 1.f;
				x = p[i].x;
				y = p[i].y;
			}
			return c;
		};
		size_t at = 0;   // 지금 길에서 이미 지난 칸 다음
		for (size_t i = 0; i < m_path.size(); ++i)
			if (m_path[i].x == m_predX && m_path[i].y == m_predY) { at = i + 1; break; }
		bool keep = false;
		if (!m_path.empty() && !np.empty() && at < m_path.size() && m_goal.kind != GoalKind::Move)
		{
			const path::Step e = m_path.back();
			const int ex = std::abs(e.x - gx), ey = std::abs(e.y - gy);
			const float tail = static_cast<float>(std::max(ex, ey) - std::min(ex, ey)) + 1.41421356f * static_cast<float>(std::min(ex, ey));
			keep = cost(m_path, at) + tail <= cost(np, 0) + 3.f;
		}
		if (!keep) m_path = std::move(np);
		m_pathGoalX = gx;   // 고수했어도 이 목표로는 다시 찾지 않는다(1초 뒤 · 목표가 또 바뀌면 다시)
		m_pathGoalY = gy;
		m_repathAt = m_now + 1.0;
		if (m_path.empty()) return false;
	}
	// 이미 지난 칸은 버린다
	for (size_t i = 0; i < m_path.size(); ++i)
		if (m_path[i].x == m_predX && m_path[i].y == m_predY)
		{
			m_path.erase(m_path.begin(), m_path.begin() + static_cast<std::ptrdiff_t>(i) + 1);
			break;
		}
	if (m_path.empty()) return false;
	for (int i = static_cast<int>(m_path.size()) - 1; i >= 0; --i)
	{
		const path::Step& n = m_path[static_cast<size_t>(i)];
		if (i == 0 || LineClear(m_predX, m_predY, n.x, n.y, 0))
		{
			wx = n.x;
			wy = n.y;
			return true;
		}
	}
	return false;
}

bool Game::StepToward(int gx, int gy, int stopWithin)
{
	if (Cheb(m_predX, m_predY, gx, gy) <= stopWithin && !(stopWithin == 0 && (m_predX != gx || m_predY != gy)))
		return false;
	int wx = gx, wy = gy;
	if (!Waypoint(gx, gy, stopWithin, wx, wy))
		return false;
	m_wayX = wx;
	m_wayY = wy;
	m_wayValid = true;
	int dir = -1;
	if (Cheb(m_predX, m_predY, wx, wy) <= 1)
	{
		// 마지막 한 칸은 정확히 그 칸으로
		const int ddx = wx - m_predX, ddy = wy - m_predY;
		if ((ddx != 0 || ddy != 0) && path::CanStep(m_predX, m_predY, ddx, ddy)) dir = MoveRule::DirOf(ddx, ddy);
	}
	if (dir < 0)
	{
		// 경유점으로 가는 곧은 선을 따라(DDA) — 격자 칸이 선에서 0.5칸 안에 머문다
		Vector2 o{}, u{};
		if (RunLine(Vector2{ static_cast<float>(wx), static_cast<float>(wy) }, o, u)) dir = PlanDir(m_predX, m_predY, o, u);
		if (dir < 0)
		{
			const int sx = (wx > m_predX) - (wx < m_predX), sy = (wy > m_predY) - (wy < m_predY);
			dir = PickMoveDir(sx, sy);
		}
	}
	if (dir < 0)
	{
		m_path.clear();
		m_repathAt = 0.0;
		return false;
	}
	// 경로를 따라가는 중이면 계획한 걸음이 막다른 칸으로 가지 않는지 본다: 새 칸이 경로 위에 있거나 새 칸에서 경유점까지
	// 곧게 갈 수 있어야 한다. 아니면 경로의 다음 칸으로. 곧은 선의 원점은 화면 위치라 논리 칸과 어긋나면 선이 막다른 쪽으로
	// 기울 수 있다 — 계단으로 돌아가는 길(산울타리 · 가로등 옆)에서 두 칸을 오가며 멈춰 섰다(Phase 4-A terraincheck에서 찾음).
	if (!m_path.empty())
	{
		int nx = m_predX, ny = m_predY;
		MoveRule::Apply(nx, ny, dir);
		bool onPath = false;
		for (const auto& st : m_path)
			if (st.x == nx && st.y == ny) { onPath = true; break; }
		if (!onPath && !LineClear(nx, ny, wx, wy, 0))
		{
			const path::Step n0 = m_path.front();
			const int ddx = n0.x - m_predX, ddy = n0.y - m_predY;
			if (Cheb(m_predX, m_predY, n0.x, n0.y) == 1 && path::CanStep(m_predX, m_predY, ddx, ddy))
				dir = MoveRule::DirOf(ddx, ddy);
			else
				m_repathAt = 0.0;   // 경로에서 벗어났다 — 다음 걸음에서 지금 칸부터 다시 찾는다
		}
	}
	if (static const bool trace = std::getenv("HARVEN_STEP_TRACE") != nullptr; trace)
	{
		const Vector2 P = m_mover.Visual();
		std::printf("[step] pred %d,%d vis %.2f,%.2f goal %d,%d way %d,%d dir %d path %d\n", m_predX - WorldData::kTownX0, m_predY - WorldData::kTownY0,
			P.x - WorldData::kTownX0, P.y - WorldData::kTownY0, gx - WorldData::kTownX0, gy - WorldData::kTownY0, wx - WorldData::kTownX0, wy - WorldData::kTownY0, dir, static_cast<int>(m_path.size()));
	}
	return SendStep(dir);
}

bool Game::StepAlong(Vector2 origin, Vector2 dir)
{
	// 누른 채 이동: 커서 쪽 곧은 선을 따라 DDA로(임의 각도), 막히면 가장 가까운 열린 방향으로 미끄러진다
	int d = PlanDir(m_predX, m_predY, origin, PlanU(dir));
	if (d < 0)
	{
		int dx = 0, dy = 0;
		Snap8(dir, dx, dy);
		if (dx == 0 && dy == 0) return false;
		d = PickMoveDir(dx, dy);
	}
	return d >= 0 && SendStep(d);
}

void Game::UpdateGuide()
{
	// 화면 추종자의 안내선: 지금 화면 위치에서 가려는 곳으로 곧게(격자 지그재그를 지운다)
	motion::Guide g;
	const Vector2 P = m_mover.Visual();
	auto toward = [&](Vector2 target, float minDist)
	{
		if (util::Len(util::Sub(target, P)) < minDist) return;
		Vector2 o{}, u{};
		if (!RunLine(target, o, u)) return;
		g.active = true;
		g.origin = o;   // 걸음 계획과 같은 선
		g.dir = u;
	};
	switch (m_goal.kind)
	{
	case GoalKind::Direction:
		toward(m_dirTarget, 0.75f);
		break;
	case GoalKind::Move:
	case GoalKind::Pickup:
	case GoalKind::Talk:
		if (m_wayValid) toward(Vector2{ static_cast<float>(m_wayX), static_cast<float>(m_wayY) }, 0.3f);
		break;
	// 움직이는 대상(따라가기 · 쫓아가 공격)은 곧은 선이 계속 돌아가서 화면이 흔들린다 — 논리 궤적을 앞보기로 따라간다
	case GoalKind::Attack:
	case GoalKind::Follow:
	default:
		break;
	}
	if (m_scriptKeyX != 0 || m_scriptKeyY != 0) g.active = false;   // 예전 WASD 시험: 칸 걸음 그대로
	m_mover.SetGuide(g);
}

void Game::TickMovement()
{
	Entity* me = Self();
	if (!me || m_dead)
		return;
	// 기술 동작 중(짧은 무게감) · 돌진 착지 대기 · 기절/빙결 중에는 걸음을 보내지 않는다
	if (m_now < m_actionLockUntil || (m_dashWaitSeq != 0 && m_now < m_dashWaitUntil))
		return;
	const float nowf = static_cast<float>(m_now);
	if (me->Status(static_cast<uint8_t>(StatusId::Stun), nowf) || me->Status(static_cast<uint8_t>(StatusId::Freeze), nowf))
		return;

	// 자동 시나리오의 화면 방향(예전 WASD 검증 — 화면 오른쪽 = 칸(+1,-1), 화면 아래 = 칸(+1,+1))
	if (m_scriptKeyX != 0 || m_scriptKeyY != 0)
	{
		if (m_goal.kind != GoalKind::None) ClearGoal();
		if (!m_mover.CanStep(m_now)) return;
		const int dx = std::clamp(m_scriptKeyX + m_scriptKeyY, -1, 1), dy = std::clamp(m_scriptKeyY - m_scriptKeyX, -1, 1);
		const int dir = PickMoveDir(dx, dy);
		if (dir >= 0) SendStep(dir);
		return;
	}

	// 선확정: 지금 걸음이 0.11초 남으면 다음 걸음을 정해 보낸다(칸 경계에서 멈추지 않게)
	if (!m_mover.CanStep(m_now))
		return;

	switch (m_goal.kind)
	{
	case GoalKind::Move:
		if (!StepToward(m_goal.x, m_goal.y, 0))
		{
			if (m_predX == m_goal.x && m_predY == m_goal.y)
				break;
			// 경로가 막혔으면 한 번 더 찾아보고 그래도 없으면 포기
			if (m_now > m_repathAt)
				ClearGoal();
		}
		break;
	case GoalKind::Direction:
	{
		const Vector2 d = util::Sub(m_dirTarget, Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) });
		Vector2 o{}, u{};
		if (util::Len(d) > 0.75f && RunLine(m_dirTarget, o, u))
			StepAlong(o, u);
		break;
	}
	case GoalKind::Attack:
		if (Entity* t = Find(m_goal.target))
			StepToward(t->tx, t->ty, AttackRange());
		break;
	case GoalKind::Follow:
		if (Entity* t = Find(m_goal.target))
		{
			// 상대가 걷는 중이면 그 칸까지 쫓아가며 멈추지 않는다(한 칸 앞에서 섰다 떠나기를 되풀이하지 않게),
			// 서 있으면 한 칸 옆에서 멈춘다
			const bool moving = t->track.qn > 0 || t->track.t < 1.f || (t->track.lastSpeed > 0.3f && t->track.dryT < 0.5f);
			StepToward(t->tx, t->ty, moving ? 0 : 1);
		}
		break;
	case GoalKind::Pickup:
		StepToward(m_goal.x, m_goal.y, 0);
		break;
	case GoalKind::Talk:
	{
		const Vector2 at = NpcTile(m_goal.npc);
		StepToward(static_cast<int>(at.x), static_cast<int>(at.y), 2);
		break;
	}
	default:
		break;
	}
}

void Game::TickGoal()
{
	if (m_dead)
		return;
	// 목표 행동(공격·줍기·대화)은 예측 칸에 **도착한 뒤**에 한다. 서버는 이동과 행동을
	// 받은 순서대로 처리하므로 ACK를 기다릴 필요는 없다.
	const bool arrived = !m_mover.IsWalking(m_now);
	switch (m_goal.kind)
	{
	case GoalKind::Attack:
	{
		Entity* t = Find(m_goal.target);
		if (!t || t->dead || t->removing)
		{
			ClearGoal();
			return;
		}
		// 대상이 움직이면 경로는 Waypoint가 다시 찾는다(지금 길이 크게 나쁘지 않으면 고수 — 제자리 오르내림 방지)
		const int range = AttackRange();
		if (arrived && Cheb(m_predX, m_predY, t->tx, t->ty) <= range)
		{
			m_path.clear();
			if (m_queuedSkill > 0 && m_queuedTarget == t->id)
			{
				// 사거리 밖에서 눌러 둔 기술 — 닿았으니 쓴다(실패해도 4초 뒤엔 잊는다)
				const uint16_t skill = static_cast<uint16_t>(m_queuedSkill);
				if (TryCastSkill(skill, true, t->id) || m_now - m_queuedAt > 4.0)
					m_queuedSkill = -1;
			}
			else if (m_heldSkill == 0)
				TryAttackTarget(*t);   // 모으거나 도는 동안에는 자동 공격이 기술을 끊지 않는다(다른 기술 누름 = 끊김 규칙)
		}
		break;
	}
	case GoalKind::Follow:
	{
		Entity* t = Find(m_goal.target);
		if (!t || t->dead || t->removing)
			ClearGoal();
		break;
	}
	case GoalKind::Pickup:
	{
		Entity* item = Find(m_goal.target);
		if (!item || item->removing || item->flyT >= 0.f)
		{
			ClearGoal();
			return;
		}
		if (arrived && m_predX == item->tx && m_predY == item->ty)
		{
			PickupHere();
			ClearGoal();
		}
		break;
	}
	case GoalKind::Talk:
	{
		const auto* npc = WorldData::FindNpc(m_goal.npc);
		if (!npc) { ClearGoal(); return; }
		const Vector2 at = NpcTile(npc->id);
		if (arrived && Cheb(m_predX, m_predY, static_cast<int>(at.x), static_cast<int>(at.y)) <= 2)
		{
			OpenNpc(npc->id);
			ClearGoal();
		}
		break;
	}
	case GoalKind::Move:
		if (arrived && m_predX == m_goal.x && m_predY == m_goal.y)
			ClearGoal();
		break;
	default:
		break;
	}
}

void Game::TryAttackTarget(Entity& t)
{
	if (t.kind == EntityKind::Player && !m_goal.force)
	{
		ClearGoal();
		return;
	}
	// 기본 공격도 기술 표(검격 · 각인탄 · 연속 찌르기 — 콤보)로
	TryCastSkill(SkillDefTable::Default(m_classId, SkillKey::C), true, t.id);
}

// ─────────────────────────────────────────────────────────────────────────────
// 상호작용(G) — 가장 가까운 NPC · 물건 · 동물
// ─────────────────────────────────────────────────────────────────────────────
const WorldData::NpcRow* Game::InteractNpc() const
{
	const WorldData::NpcRow* best = nullptr;
	int bestD = WorldData::kNpcInteractRange + 1;
	for (const auto& npc : WorldData::kNpcs)
	{
		const Vector2 at = NpcTile(npc.id);
		const int d = Cheb(m_predX, m_predY, static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)));
		if (d < bestD) { bestD = d; best = &npc; }
	}
	return best;
}

Entity* Game::InteractItem()
{
	Entity* best = nullptr;
	int bestD = 3;
	for (auto& [id, e] : m_ents)
	{
		if (e.kind != EntityKind::Item || e.removing || e.flyT >= 0.f) continue;
		const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
		if (d < bestD) { bestD = d; best = &e; }
	}
	return best;
}

void Game::InteractTarget(uint16_t& npc, ObjID& item)
{
	// 발밑 · 바로 옆 물건이 NPC보다 먼저(줍는 손이 더 가깝다), 아니면 가까운 NPC, 그것도 없으면 가까운 물건.
	// 머리 위 G 표시도 이 답을 쓴다.
	npc = 0;
	item = ObjID{};
	Entity* it = InteractItem();
	const WorldData::NpcRow* n = InteractNpc();
	if (it && Cheb(m_predX, m_predY, it->tx, it->ty) <= 1) { item = it->id; return; }
	if (n) { npc = n->id; return; }
	if (it) item = it->id;
}

void Game::Interact()
{
	if (m_dead) return;
	uint16_t npc = 0;
	ObjID itemId{};
	InteractTarget(npc, itemId);
	if (Entity* item = Find(itemId))
	{
		SetGoal(Goal{ GoalKind::Pickup, item->id, item->tx, item->ty });
		return;
	}
	if (npc != 0)
	{
		if (m_dialogNpc == npc) { CloseNpcWindows(); return; }
		SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, npc });
		return;
	}
	PickupHere();
}

void Game::PickupHere()
{
	if (m_dead) return;
	// 내 칸에 아이템이 있으면 줍고, 없으면 가까운 아이템으로 걸어간다
	Entity* best = nullptr;
	int bestD = 99;
	for (auto& [id, e] : m_ents)
	{
		if (e.kind != EntityKind::Item || e.removing || e.flyT >= 0.f) continue;
		const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
		if (d < bestD) { bestD = d; best = &e; }
	}
	if (best && bestD == 0 && !m_mover.IsWalking(m_now))
	{
		m_net.SendPickup();
		m_pickupSentAt = m_now;
		return;
	}
	if (best && bestD <= 5)
		SetGoal(Goal{ GoalKind::Pickup, best->id, best->tx, best->ty });
}

void Game::TargetNearestMonster()
{
	Entity* best = nullptr;
	float bestD = 1e9f;
	for (auto& [id, e] : m_ents)
	{
		if (e.kind != EntityKind::Monster || e.dead || e.removing) continue;
		const float d = static_cast<float>(Cheb(m_predX, m_predY, e.tx, e.ty)) + (id == m_target ? -0.5f : 0.f);
		if (d < bestD) { bestD = d; best = &e; }
	}
	if (!best)
	{
		AddCenter("주변에 공격할 대상이 없습니다.", Rgb(0xC8C8C8));
		return;
	}
	m_target = best->id;
	SetGoal(Goal{ GoalKind::Attack, best->id });
}

void Game::UseSlot(int slot)
{
	if (slot < 0 || slot >= MAX_INVENTORY_SLOTS || m_dead) return;
	const InvSlot& s = m_inv.slots[slot];
	if (s.Empty()) return;
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	if (!row) return;

	if (row->type == ItemType::eEquipment)
	{
		if (s.equipped) m_net.SendUnequip(static_cast<uint16_t>(slot));
		else m_net.SendEquip(static_cast<uint16_t>(slot));
		return;
	}
	if (row->type != ItemType::eConsumable)
		return;

	switch (row->effect)
	{
	case ItemEffect::eHeal:
	{
		const double remain = m_lastPotionAt + CombatRule::kPotionCooldownMs / 1000.0 - m_now;
		if (remain > 0.0)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "물약 재사용 대기 중 (%.1f초)", remain);
			AddCenter(buf, Rgb(0xC8C8C8));
			sfx::Play(sfx::Id::Error, 0.4f);
			return;
		}
		m_lastPotionAt = m_now + 0.05;
		m_net.SendUseItem(static_cast<uint16_t>(slot));
		break;
	}
	case ItemEffect::eReturnTown:
	case ItemEffect::eTeleportRandom:
	case ItemEffect::eHaste:
	{
		if (m_now - m_lastScrollAt < CombatRule::kScrollCooldownMs / 1000.0)
			return;
		m_lastScrollAt = m_now;
		ClearGoal();
		m_net.SendUseItem(static_cast<uint16_t>(slot));
		break;
	}
	case ItemEffect::eEnchantWeapon:
		StartEnchant(slot);
		break;
	default:
		break;
	}
}
