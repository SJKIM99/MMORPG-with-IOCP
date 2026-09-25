#include "game/Game.h"
#include "game/Input.h"
#include "game/KeyBinds.h"
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
//   V · Space · Z       각성기 · 이동기 · 아이덴티티
//   1~4 · F1 · 5~8      배틀 칸 · 체력 물약 · 보조 칸
//   G                   가장 가까운 대상과 상호작용(대화 · 줍기 · 쓰다듬기)
// 키는 모두 game/KeyBinds로 다시 지정할 수 있고, 채팅 중(한글 조합 포함)에는 먹지 않는다.
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;

namespace
{
	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	// 직업별 기본 배치: 칸 → ClassRow.skills의 몇 번째(-1 비움). Phase 2 기술표의 자리.
	//                      C  Q  W  E  R  A  S  D  F  V  Spc Z
	constexpr int kLayout[3][12] = {
		{ 0, 1, 2, -1, -1, 3, 4, -1, -1, 5, -1, -1 },   // 서약기사: 방패 강타 · 회전 베기 · 돌진 · 철벽 태세 · 단죄
		{ 0, 1, 2, 4, -1, -1, -1, -1, -1, 5, 3, -1 },   // 각인술사: 서리 각인 · 화염 고리 · 벼락 사슬 · 낙성 · 옛길 도약
		{ 0, 2, 1, 4, -1, -1, -1, -1, -1, 5, 3, -1 },   // 밤사냥꾼: 독 칼날 · 그늘 걸음 · 후방 습격 · 그믐 베기 · 잔상 회피
	};

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
	const int cls = static_cast<int>(m_classId) < 3 ? static_cast<int>(m_classId) : 0;
	const ClassRow& c = ClassTable::Get(m_classId);
	for (int i = 0; i < kHotSlots; ++i)
		if (kLayout[cls][i] >= 0)
			m_hot[static_cast<size_t>(i)] = c.skills[kLayout[cls][i]];
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
		if (id != 0 && (!row || row->cls != m_classId || (i == kHotBasic) != (row->slot == 0))) return;
		saved[static_cast<size_t>(i)] = static_cast<uint16_t>(id);
	}
	if (saved[kHotBasic] == 0) return;
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
		m_aim.world = gfx::IsoToTile(mouseIso);
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
	if (m_enchant.stage == 1 || m_enchant.stage == 2) { m_enchant = EnchantFlow{}; return true; }
	if (m_discard.open) { m_discard.open = false; return true; }
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
	const Vector2 t = gfx::IsoToTile(mouseIso);
	m_hoverX = static_cast<int>(std::floor(t.x + 0.5f));
	m_hoverY = static_cast<int>(std::floor(t.y + 0.5f));

	// 화면에서 몸통 사각형으로 고른다(앞쪽 = X+Y 큰 것 우선)
	float bestDepth = -1e9f;
	for (auto& [id, e] : m_ents)
	{
		if (id == m_selfId || e.removing || (e.dead && e.kind != EntityKind::Item) || e.flyT >= 0.f)
			continue;
		const Vector2 base = TileToIso(e.pos);
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
			const Vector2 base = TileToIso(v.pos);
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
	if (m_dead || m_menuOpen || m_keysOpen)
		return;   // 메뉴 · 조작 설정은 모달 — 어두운 바깥을 눌러도 월드로 가지 않는다

	const bool overUi = ui::MouseOverUi() || ui::MouseConsumed();
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
				SetGoal(Goal{ GoalKind::Follow, e->id });
			m_moveHeld = false;
		}
		else if (e && e->kind == EntityKind::Item)
		{
			SetGoal(Goal{ GoalKind::Pickup, e->id, e->tx, e->ty });
			fx::Fx().ClickMarker(TileToIso(e->pos), ui::theme::kDawnHi);
			m_moveHeld = false;
		}
		else if (m_hoverNpc != 0)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, m_hoverNpc });
			m_moveHeld = false;
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
		else if (m_aim.valid && (m_moveDirMode || m_now - m_movePressAt > 0.16))
		{
			// 누르고 있으면 커서 쪽으로 계속(목적지 클릭 → 방향 이동으로)
			if (!m_moveDirMode || m_goal.kind != GoalKind::Direction)
			{
				m_goal = Goal{ GoalKind::Direction };
				m_path.clear();
				m_queuedSkill = -1;
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
	if (m_dead || InTown())
	{
		if (!hold && InTown()) AddCenter("마을 안에서는 싸울 수 없습니다.", Rgb(0xFFD070));
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
	// 대상이 없으면 커서 쪽으로 한 번 휘두른다(보이기만 — 서버에 보내지 않는다)
	Entity* me = Self();
	if (!me || m_now - m_whiffAt < 0.35 || m_now < m_actionLockUntil) return;
	m_whiffAt = m_now;
	int dx = 0, dy = 0;
	Snap8(m_aim.dir, dx, dy);
	const int sx = dx - dy, sy = dx + dy;
	if (sx != 0) me->faceRight = sx > 0;
	if (sy != 0) me->faceBack = sy < 0;
	me->attackT = 0.f;
	me->animT = -1.f;
	sfx::Play(sfx::Id::Swing, 0.45f, 0.08f);
}

void Game::CastHot(int hot)
{
	const SkillRow* row = HotSkill(hot);
	if (!row)
	{
		sfx::Play(sfx::Id::Error, 0.25f);
		return;
	}
	if (row->slot == 0)
	{
		BasicAttackAtAim(false);
		return;
	}
	TryCastSkill(row->slot);
}

// ─────────────────────────────────────────────────────────────────────────────
// 소모품 칸(배틀 1~4 · 물약 F1 · 보조 5~8) — 캐릭터별 저장
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
	// 기본: 배틀 1 진한 약초 물약 · 2 수액 영약 · 3 바람걸음 물약 · 물약(F1) 약초 물약 · 보조 5 귀환 · 6 옛길 두루마리
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
	// 마이그레이션: 예전 Q E R T 네 칸(quickslots2) → 배틀 칸 1~4. 예전 파일은 지우지 않는다.
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
}

void Game::ClearGoal()
{
	m_goal = Goal{};
	m_path.clear();
	m_queuedSkill = -1;
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

bool Game::StepToward(int gx, int gy, int stopWithin)
{
	if (Cheb(m_predX, m_predY, gx, gy) <= stopWithin && !(stopWithin == 0 && (m_predX != gx || m_predY != gy)))
		return false;
	if (m_path.empty() || m_now > m_repathAt)
	{
		m_path = path::Find(m_predX, m_predY, gx, gy, stopWithin);
		m_repathAt = m_now + 1.2;
		if (m_path.empty())
			return false;
	}
	const path::Step next = m_path.front();
	const int dir = MoveRule::DirOf(next.x - m_predX, next.y - m_predY);
	if (dir < 0 || std::abs(next.x - m_predX) > 1 || std::abs(next.y - m_predY) > 1)
	{
		m_path.clear();
		return false;
	}
	// 경로의 다음 칸이 서버 규칙으로도 정확히 그 칸이어야 한다(대각선 모서리 등)
	int x = m_predX, y = m_predY;
	MoveRule::Apply(x, y, dir);
	if (x != next.x || y != next.y)
	{
		m_path.clear();
		m_repathAt = 0.0;
		return false;
	}
	if (!SendStep(dir))
		return false;
	m_path.erase(m_path.begin());
	return true;
}

bool Game::StepAlong(Vector2 dir)
{
	int dx = 0, dy = 0;
	Snap8(dir, dx, dy);
	if (dx == 0 && dy == 0) return false;
	const int d = PickMoveDir(dx, dy);
	return d >= 0 && SendStep(d);
}

void Game::TickMovement()
{
	Entity* me = Self();
	if (!me || m_dead)
		return;
	// 기술 동작 중(짧은 무게감)·돌진 착지 대기·기절/빙결 중에는 걸음을 보내지 않는다
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

	if (!m_mover.CanStep(m_now))
		return;

	switch (m_goal.kind)
	{
	case GoalKind::Move:
		if (!StepToward(m_goal.x, m_goal.y, 0) && m_path.empty())
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
		if (util::Len(d) > 0.75f)
			StepAlong(d);
		break;
	}
	case GoalKind::Attack:
		if (Entity* t = Find(m_goal.target))
			StepToward(t->tx, t->ty, AttackRange());
		break;
	case GoalKind::Follow:
		if (Entity* t = Find(m_goal.target))
			StepToward(t->tx, t->ty, 1);
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
		// 대상이 움직이면 경로를 다시 찾는다
		const int range = AttackRange();
		if (!m_path.empty())
		{
			const path::Step last = m_path.back();
			if (Cheb(last.x, last.y, t->tx, t->ty) > range)
				m_path.clear();
		}
		if (arrived && Cheb(m_predX, m_predY, t->tx, t->ty) <= range)
		{
			m_path.clear();
			if (m_queuedSkill >= 0 && m_queuedTarget == t->id)
			{
				// 사거리 밖에서 눌러 둔 기술 — 닿았으니 쓴다(실패해도 4초 뒤엔 잊는다)
				const int slot = m_queuedSkill;
				if (TryCastSkill(slot, true, t->id) || m_now - m_queuedAt > 4.0)
					m_queuedSkill = -1;
			}
			else
				TryAttackTarget(*t);
		}
		break;
	}
	case GoalKind::Follow:
	{
		Entity* t = Find(m_goal.target);
		if (!t || t->dead || t->removing)
			ClearGoal();
		else if (!m_path.empty())
		{
			const path::Step last = m_path.back();
			if (Cheb(last.x, last.y, t->tx, t->ty) > 1)
				m_path.clear();
		}
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
	TryCastSkill(0, true, t.id);   // 기본 공격도 기술 표(검격·각인탄·연속 찌르기)로
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
