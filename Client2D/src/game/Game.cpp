#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Props.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "platform/Ime.h"
#include "core/Util.h"

#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstdlib>

using util::Rgb;

namespace
{
	constexpr float kMoveInterval = 0.54f;       // 서버 이동 쿨타임 500ms + 여유
	constexpr float kHasteMoveInterval = 0.36f;  // 가속 330ms + 여유
	constexpr float kAttackInterval = 0.62f;     // 서버 공격 쿨타임 500ms + 여유
	constexpr float kPendingTimeout = 1.0f;      // 이동 응답이 이만큼 안 오면 거절된 것으로 보고 되돌린다

	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

}

// ─────────────────────────────────────────────────────────────────────────────
bool Game::Init(const Options& opts)
{
	m_opts = opts;
	m_fHost.text = opts.host;
	m_fHost.asciiOnly = true;
	m_fHost.maxBytes = 60;
	m_fPort.text = std::to_string(opts.port);
	m_fPort.asciiOnly = true;
	m_fPort.maxBytes = 5;
	m_fName.asciiOnly = true;
	m_fName.maxBytes = 12;
	m_fPass.password = true;
	m_fPass.asciiOnly = true;
	m_fPass.maxBytes = PASSWORD_SIZE - 1;
	m_chatField.maxBytes = CHAT_SIZE - 1;

	m_net.SetHandler([this](const char* data, uint16_t size) { OnPacket(data, size); });
	m_terrain.Init();
	const Vector2 town = gfx::IsoPx(WorldData::kTownX0 + 22.f, WorldData::kTownY0 + 22.f);
	m_cam.SnapTo(town);

	ui::SetDragIconDrawer([this](const ui::DragPayload& p, Vector2 at)
	{
		if (p.kind == 2 && m_quick[p.index].kind == 2)
		{
			DrawCircleV(at, 20, Color{ 60, 120, 200, 200 });
			return;
		}
		icons::Draw(p.itemId, Rectangle{ at.x - 22, at.y - 22, 44, 44 }, Color{ 255, 255, 255, 220 });
	});

	if (!opts.autoName.empty())
	{
		m_fName.text = opts.autoName;
		m_fPass.text = "1234";
		BeginConnect();
	}
	return true;
}

void Game::Shutdown()
{
	SaveQuickSlots();
	m_net.Disconnect();
	m_terrain.Shutdown();
	m_light.Shutdown();
}

void Game::BeginConnect()
{
	std::string name = m_fName.text;
	if (name.size() < 2)
	{
		m_loginError = "캐릭터 이름은 영문/숫자 2~12자로 입력하세요.";
		sfx::Play(sfx::Id::Error);
		return;
	}
	const int port = std::atoi(m_fPort.text.c_str());
	if (port <= 0 || port > 65535)
	{
		m_loginError = "포트 번호가 올바르지 않습니다.";
		sfx::Play(sfx::Id::Error);
		return;
	}
	m_loginError.clear();
	m_playerName = name;
	ResetWorld();
	if (!m_net.Connect(m_fHost.text, static_cast<uint16_t>(port)))
	{
		m_loginError = m_net.LastError();
		sfx::Play(sfx::Id::Error);
		return;
	}
	m_scene = Scene::Connecting;
	m_loginSent = false;
	m_connectStart = GetTime();
	ui::Focus(nullptr);
}

void Game::ResetWorld()
{
	m_ents.clear();
	m_inv = ClientInventory{};
	m_hasSelf = false;
	m_selfId = ObjID{};
	m_dead = false;
	m_pending = false;
	m_path.clear();
	m_goal = Goal{};
	m_target = ObjID{};
	m_hover = ObjID{};
	m_dialogNpc = 0;
	m_shopMode = ShopMode::None;
	m_enchant = EnchantFlow{};
	m_discard = Discard{};
	m_banners.clear();
	m_toasts.clear();
	m_center.clear();
	fx::Fx().Clear();
	fx::Particles().Clear();
}

// ─────────────────────────────────────────────────────────────────────────────
void Game::Frame()
{
	m_screenW = GetScreenWidth();
	m_screenH = GetScreenHeight();
	const float dt = std::min(GetFrameTime(), 0.05f);
	m_now = GetTime();
	m_runTime += dt;

	m_net.Update();
	ui::BeginFrame(dt);
	m_light.UpdateClock(m_opts.hour);
	props::SetNight(m_light.Night());

	switch (m_scene)
	{
	case Scene::Login:
	case Scene::Connecting: UpdateLogin(dt); break;
	case Scene::Game: UpdateGame(dt); break;
	case Scene::Disconnected: m_time += dt; fx::Fx().Update(dt); break;
	}

	BeginDrawing();
	ClearBackground(Color{ 8, 8, 12, 255 });
	switch (m_scene)
	{
	case Scene::Login:
	case Scene::Connecting: DrawLogin(); break;
	case Scene::Game: DrawGame(); break;
	case Scene::Disconnected: DrawGame(); DrawDisconnected(); break;
	}
	ui::EndFrame();
	DrawCursor();

	if (!m_opts.screenshot.empty() && !m_shotTaken && m_runTime >= m_opts.screenshotAfter + m_shotIndex * m_opts.screenshotEvery)
	{
		rlDrawRenderBatchActive();   // 배치에 남은 UI까지 그린 뒤 캡처
		std::string path = m_opts.screenshot;
		if (m_opts.screenshotCount > 1)
		{
			const size_t dot = path.rfind('.');
			path = path.substr(0, dot) + "_" + std::to_string(m_shotIndex + 1) + (dot == std::string::npos ? ".png" : path.substr(dot));
		}
		TakeScreenshot(path.c_str());
		if (++m_shotIndex >= m_opts.screenshotCount)
		{
			m_shotTaken = true;
			m_quit = true;
		}
	}
	EndDrawing();
}

void Game::UpdateLogin(float dt)
{
	m_time += dt;
	// 로그인 배경: 마을 위를 천천히 도는 카메라
	const float a = m_time * 0.05f;
	const Vector2 c{ WorldData::kTownX0 + 22.f + std::cos(a) * 9.f, WorldData::kTownY0 + 22.f + std::sin(a) * 9.f };
	m_cam.Follow(gfx::IsoPx(c.x, c.y));
	m_cam.Update(dt);
	fx::Fx().Update(dt);
	fx::Fx().Ambient(WorldData::Biome::eMeadow, m_cam.Target(), m_screenW / m_cam.Zoom(), m_screenH / m_cam.Zoom(), dt, m_light.Night(), true);

	if (m_scene == Scene::Connecting)
	{
		const auto state = m_net.GetState();
		if (state == net::TcpSocket::State::Connected && !m_loginSent)
		{
			m_net.SendLoginEx(m_playerName, m_fPass.text.empty() ? "1234" : m_fPass.text);
			m_loginSent = true;
		}
		else if (state == net::TcpSocket::State::Failed || state == net::TcpSocket::State::Closed)
		{
			m_loginError = m_net.LastError().empty() ? "서버에 연결할 수 없습니다." : m_net.LastError();
			m_scene = Scene::Login;
			sfx::Play(sfx::Id::Error);
		}
		else if (GetTime() - m_connectStart > 10.0)
		{
			m_net.Disconnect();
			m_loginError = "서버 응답이 없습니다. 주소/포트를 확인하세요.";
			m_scene = Scene::Login;
			sfx::Play(sfx::Id::Error);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
Entity* Game::Self()
{
	if (!m_hasSelf) return nullptr;
	auto it = m_ents.find(m_selfId);
	return it == m_ents.end() ? nullptr : &it->second;
}

Entity* Game::Find(const ObjID& id)
{
	auto it = m_ents.find(id);
	return it == m_ents.end() ? nullptr : &it->second;
}

bool Game::InTown() const { return WorldData::IsSafeZone(m_predX, m_predY); }
int Game::Region() const { return WorldData::RegionIdAt(m_predX, m_predY); }

Vector2 Game::TileToIso(Vector2 tile, float z) const { return gfx::IsoPx(tile.x, tile.y, z); }

void Game::AddChat(const std::string& text, Color color)
{
	m_chat.push_back({ text, color, m_now });
	while (m_chat.size() > 80)
		m_chat.pop_front();
}

void Game::AddCenter(const std::string& text, Color color)
{
	for (auto& c : m_center)
		if (c.text == text) { c.start = m_now; return; }
	m_center.push_back({ text, color, m_now });
	while (m_center.size() > 3)
		m_center.pop_front();
}

void Game::AddBanner(const std::string& text, Color color, float life)
{
	m_banners.push_back({ text, color, m_now, life });
	while (m_banners.size() > 3)
		m_banners.pop_front();
}

void Game::AddToast(uint16_t itemId, const std::string& text, Color color)
{
	m_toasts.push_back({ itemId, text, color, m_now });
	while (m_toasts.size() > 5)
		m_toasts.pop_front();
}

void Game::PlayAt(int sfxId, Vector2 tile, float volume, float pitchVar)
{
	// 화면 기준 좌우 위치로 팬, 거리로 음량
	const Vector2 me{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	const float dx = (tile.x - tile.y) - (me.x - me.y);
	const float dist = std::max(std::fabs(tile.x - me.x), std::fabs(tile.y - me.y));
	const float vol = volume * util::Clamp01(1.2f - dist / 9.f);
	if (vol <= 0.02f) return;
	sfx::Play(static_cast<sfx::Id>(sfxId), vol, pitchVar, util::Clamp01(0.5f + dx * 0.06f));
}

Color Game::NameColor(const Entity& e) const
{
	if (e.kind == EntityKind::Player)
	{
		const bool purple = (e.flags & SUBJECT_EX_PURPLE) != 0;
		switch (AlignmentRule::ColorOf(e.alignment, purple))
		{
		case AlignmentRule::NameColor::ePurple: return Rgb(0xD890FF);
		case AlignmentRule::NameColor::eChaotic: return Rgb(0xFF5A50);
		case AlignmentRule::NameColor::eLawful: return Rgb(0x7AB8FF);
		default: return Rgb(0xF2F0E8);
		}
	}
	if (e.kind == EntityKind::Monster && e.row)
	{
		if (e.row->boss) return Rgb(0xFF8A3A);
		const int diff = e.row->level - m_level;
		if (diff >= 8) return Rgb(0xFF4A4A);
		if (diff >= 3) return Rgb(0xFFA040);
		if (diff <= -8) return Rgb(0x9A9A9A);
		return Rgb(0xF2EEDC);
	}
	return icons::ItemNameColor(e.itemId);
}

// ─────────────────────────────────────────────────────────────────────────────
void Game::UpdateGame(float dt)
{
	if (!m_net.IsConnected())
	{
		m_scene = Scene::Disconnected;
		sfx::Play(sfx::Id::Error);
		return;
	}
	const float sdt = dt * fx::Fx().TimeScale();
	m_time += sdt;

	HandleHotkeys();
	HandleWorldInput();
	TickGoal();
	TickMovement();
	RunScript(dt);

	// ── 엔티티 애니메이션 ──
	std::vector<ObjID> dead;
	for (auto& [id, e] : m_ents)
	{
		if (e.moveT < 1.f)
		{
			e.moveT = std::min(1.f, e.moveT + sdt / std::max(0.05f, e.moveDur));
			e.pos = util::LerpV(e.from, e.to, e.moveT);
			e.walkPhase += sdt * util::kPi / std::max(0.2f, e.moveDur);
			e.moving = util::Damp(e.moving, 1.f, 14.f, sdt);
			// 발소리/먼지(가끔)
			if (e.kind != EntityKind::Item && (e.walkPhase - e.lastStepDust) > util::kPi)
			{
				e.lastStepDust = e.walkPhase;
				if (e.row == nullptr || e.row->archetype != MonsterArchetype::eGhost)
					fx::Fx().Dust(TileToIso(e.pos), id == m_selfId ? 1.f : 0.5f);
				if (id == m_selfId)
					sfx::Play(sfx::Id::Step, 0.5f, 0.15f);
			}
			if (id == m_selfId && m_hasteUntil > m_now)
				fx::Fx().HasteTrail(TileToIso(e.pos));
		}
		else
		{
			e.pos = e.to;
			e.moving = util::Damp(e.moving, 0.f, 8.f, sdt);
		}
		if (e.attackT >= 0.f) { e.attackT += sdt; if (e.attackT > 0.6f) e.attackT = -1.f; }
		if (e.castT >= 0.f) { e.castT += sdt; if (e.castT > 0.7f) e.castT = -1.f; }
		e.hurtT = std::max(0.f, e.hurtT - sdt * 5.f);
		e.flashT = std::max(0.f, e.flashT - sdt * 7.f);
		e.spawnT = std::min(1.f, e.spawnT + sdt * 3.5f);
		e.chatT = std::max(0.f, e.chatT - dt);
		if (e.dropT < 1.f) e.dropT = std::min(1.f, e.dropT + sdt * 2.2f);
		if (e.flyT >= 0.f)
		{
			e.flyT += sdt * 3.f;
			if (Entity* me = Self()) e.flyTo = me->pos;
		}
		const float hpTarget = e.HpRatio();
		e.hpShown = util::Damp(e.hpShown, hpTarget, 18.f, dt);
		if (e.hpLag < hpTarget) e.hpLag = hpTarget;
		else if (m_time - e.lastHitAt > 0.35f) e.hpLag = util::Damp(e.hpLag, hpTarget, 5.f, dt);

		if (e.dead) e.deadT += sdt;
		if (e.removing) e.removeT += sdt * 4.f;

		// 필드 아이템 반짝임
		if (e.kind == EntityKind::Item && e.flyT < 0.f)
		{
			const ItemTableRow* row = ItemTableData::Find(e.itemId);
			if (row && row->grade >= ItemGrade::eRare && util::RandF() < dt * 6.f)
				fx::Fx().LootSparkle(TileToIso(e.pos), icons::GradeColor(row->grade));
		}

		const bool fadeDone = e.removing && e.removeT >= 1.f;
		const bool corpseDone = e.dead && id != m_selfId && e.deadT > (e.kind == EntityKind::Player ? 6.f : 1.6f);
		const bool flyDone = e.flyT >= 1.f;
		if (fadeDone || corpseDone || flyDone)
			dead.push_back(id);
	}
	for (const ObjID& id : dead)
	{
		if (id == m_target) m_target = ObjID{};
		m_ents.erase(id);
	}

	// 내 HP/EXP 표시값(부드럽게)
	const float hpRatio = m_maxhp > 0 ? static_cast<float>(m_hp) / m_maxhp : 0.f;
	m_hpShown = util::Damp(m_hpShown, hpRatio, 16.f, dt);
	if (m_hpLag < hpRatio) m_hpLag = hpRatio; else m_hpLag = util::Damp(m_hpLag, hpRatio, 3.f, dt);
	const float expRatio = m_expToNext > 0 ? util::Clamp01(static_cast<float>(m_exp) / m_expToNext) : 1.f;
	m_expShown = (expRatio < m_expShown - 0.3f) ? expRatio : util::Damp(m_expShown, expRatio, 6.f, dt);

	// ── 카메라 ──
	if (Entity* me = Self())
	{
		m_cam.Follow(TileToIso(me->pos, 0.6f));
		// 이동 방향으로 살짝 앞을 보여 준다
		Vector2 lead{ 0, 0 };
		if (me->moveT < 1.f)
		{
			const Vector2 d = util::Sub(TileToIso(me->to), TileToIso(me->from));
			lead = util::Mul(util::Norm(d), 26.f);
		}
		m_cam.SetLead(lead);
	}
	if (!ui::TextFocused() && !ui::MouseOverUi())
	{
		const float wheel = GetMouseWheelMove();
		if (wheel != 0.f) m_cam.AdjustZoom(wheel * 0.12f);
	}
	m_cam.Update(dt);

	// ── 이펙트/분위기 ──
	fx::Fx().Update(dt);
	const auto* region = WorldData::FindRegion(Region());
	const WorldData::Biome biome = region ? region->biome : WorldData::Biome::eMeadow;
	fx::Fx().Ambient(biome, m_cam.Target(), m_screenW / m_cam.Zoom(), m_screenH / m_cam.Zoom(), dt, m_light.Night(), InTown());
	if (m_now > m_ambNext)
	{
		m_ambNext = m_now + util::RandF(4.f, 9.f);
		using WorldData::Biome;
		const bool night = m_light.Night() > 0.5f;
		sfx::Id amb = sfx::Id::AmbBird;
		switch (biome)
		{
		case Biome::eSnow: case Biome::eDesert: case Biome::eCanyon: case Biome::eWasteland: amb = sfx::Id::AmbWind; break;
		case Biome::eVolcanic: case Biome::eHell: amb = sfx::Id::AmbCrackle; break;
		case Biome::eSwamp: amb = sfx::Id::AmbBubble; break;
		case Biome::eBeach: amb = sfx::Id::AmbWave; break;
		case Biome::eGraveyard: case Biome::eRuins: amb = sfx::Id::AmbCrow; break;
		default: amb = night ? sfx::Id::AmbCricket : sfx::Id::AmbBird; break;
		}
		sfx::Play(amb, 0.6f, 0.1f, util::RandF(0.25f, 0.75f));
	}

	// 알림 만료
	while (!m_banners.empty() && m_now - m_banners.front().start > m_banners.front().life) m_banners.pop_front();
	while (!m_toasts.empty() && m_now - m_toasts.front().start > 3.5) m_toasts.pop_front();
	while (!m_center.empty() && m_now - m_center.front().start > 2.6) m_center.pop_front();

	// NPC 창: 멀어지면 닫기
	if (m_dialogNpc != 0)
		if (const auto* npc = WorldData::FindNpc(m_dialogNpc); npc && Cheb(m_predX, m_predY, npc->x, npc->y) > WorldData::kNpcInteractRange + 1)
			CloseNpcWindows();

	// 강화 연출: 충전이 끝나고 결과가 있으면 공개
	if (m_enchant.stage == 3 && m_enchant.resultKnown && m_now - m_enchant.start > 1.25)
	{
		m_enchant.stage = 4;
		m_enchant.start = m_now;
		const bool ok = m_enchant.result == EnchantResult::Success;
		sfx::Play(ok ? sfx::Id::EnchantSuccess : sfx::Id::EnchantFail);
		if (Entity* me = Self())
			fx::Fx().EnchantResult(TileToIso(me->pos), ok);
		fx::Fx().Flash(ok ? Rgb(0xD8F0FF) : Rgb(0x6A3A8A), ok ? 0.5f : 0.35f, 0.35f);
		m_cam.AddTrauma(ok ? 0.25f : 0.4f);
		const std::string name = texts::ItemNameEnchanted(m_enchant.itemId, ok ? m_enchant.after : m_enchant.before);
		if (ok) AddChat(name + " 강화에 성공했습니다!", Rgb(0x9AD8FF));
		else AddChat(texts::Josa(name, "이", "가") + " 강렬한 빛과 함께 증발했습니다...", Rgb(0xC08AFF));
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 입력
// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleHotkeys()
{
	// Enter: 채팅 열기/보내기(입력칸이 처리) · Esc: 창/대상 해제
	if (!ui::TextFocused())
	{
		if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
		{
			m_chatOpen = true;
			ui::Focus(&m_chatField);
			return;
		}
	}
	if (IsKeyPressed(KEY_ESCAPE))
	{
		if (ui::TextFocused()) { ui::Focus(nullptr); m_chatOpen = false; return; }
		if (m_enchant.stage == 1 || m_enchant.stage == 2) { m_enchant = EnchantFlow{}; return; }
		if (m_discard.open) { m_discard.open = false; return; }
		if (m_dialogNpc || m_shopMode != ShopMode::None) { CloseNpcWindows(); return; }
		if (m_mapOpen) { m_mapOpen = false; return; }
		if (m_helpOpen) { m_helpOpen = false; return; }
		if (m_invOpen) { m_invOpen = false; return; }
		m_target = ObjID{};
		ClearGoal();
		return;
	}
	if (ui::TextFocused() || m_dead)
		return;

	if (IsKeyPressed(KEY_I) || IsKeyPressed(KEY_TAB)) { m_invOpen = !m_invOpen; sfx::Play(m_invOpen ? sfx::Id::Open : sfx::Id::Close, 0.6f); }
	if (IsKeyPressed(KEY_M)) { m_mapOpen = !m_mapOpen; sfx::Play(m_mapOpen ? sfx::Id::Open : sfx::Id::Close, 0.6f); }
	if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_H)) { m_helpOpen = !m_helpOpen; sfx::Play(sfx::Id::Open, 0.5f); }
	if (IsKeyPressed(KEY_G)) m_showGrid = !m_showGrid;
	if (IsKeyPressed(KEY_Q)) CastSkill();
	if (IsKeyPressed(KEY_Z)) PickupHere();
	if (IsKeyPressed(KEY_SPACE)) TargetNearestMonster();
	for (int i = 0; i < 8; ++i)
		if (IsKeyPressed(KEY_ONE + i) || IsKeyPressed(KEY_F5 + i))
			UseQuickSlot(i);
	if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) m_cam.AdjustZoom(0.15f);
	if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) m_cam.AdjustZoom(-0.15f);
}

void Game::PickUnderMouse()
{
	m_hover = ObjID{};
	m_hoverNpc = 0;
	m_hoverX = m_hoverY = -1;
	if (ui::MouseOverUi())
		return;
	const Vector2 mouseIso = GetScreenToWorld2D(GetMousePosition(), m_cam2d);
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
		for (const auto& npc : WorldData::kNpcs)
		{
			const Vector2 base = gfx::IsoPx(npc.x, npc.y);
			const Rectangle r{ base.x - 22, base.y - actors::NpcHeight(npc), 44, actors::NpcHeight(npc) + 8 };
			if (CheckCollisionPointRec(mouseIso, r)) { m_hoverNpc = npc.id; break; }
		}
	}
}

void Game::HandleWorldInput()
{
	PickUnderMouse();
	if (m_dead)
		return;

	const bool overUi = ui::MouseOverUi() || ui::MouseConsumed();
	const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

	// 인벤토리에서 끌어다 월드에 놓으면 버리기
	if (ui::Dragging() && ui::DragReleased() && !overUi && ui::Drag().kind == 1)
	{
		const int slot = ui::Drag().index;
		const InvSlot& s = m_inv.slots[slot];
		if (!s.Empty())
		{
			if (s.count > 1)
				m_discard = Discard{ true, slot, s.count };
			else
				m_net.SendDiscard(static_cast<uint16_t>(slot), 1);
		}
		ui::EndDrag();
		return;
	}

	// 강화 대상 고르기 중에는 월드 클릭 무시
	if (m_enchant.stage == 1)
		return;

	if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !overUi)
	{
		m_mouseHeld = false;
		if (m_hover != ObjID{})
		{
			Entity* e = Find(m_hover);
			if (e && e->kind == EntityKind::Monster)
			{
				m_target = e->id;
				SetGoal(Goal{ GoalKind::Attack, e->id });
				fx::Fx().ClickMarker(TileToIso(e->pos), Rgb(0xFF5A4A));
			}
			else if (e && e->kind == EntityKind::Player)
			{
				m_target = e->id;
				if (ctrl)
				{
					SetGoal(Goal{ GoalKind::Attack, e->id, 0, 0, 0, true });
					fx::Fx().ClickMarker(TileToIso(e->pos), Rgb(0xFF3A3A));
				}
			}
			else if (e && e->kind == EntityKind::Item)
			{
				SetGoal(Goal{ GoalKind::Pickup, e->id, e->tx, e->ty });
				fx::Fx().ClickMarker(TileToIso(e->pos), Rgb(0xFFD84A));
			}
		}
		else if (m_hoverNpc != 0)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, m_hoverNpc });
		}
		else if (m_hoverX >= 0)
		{
			m_mouseHeld = true;
			SetGoal(Goal{ GoalKind::Move, ObjID{}, m_hoverX, m_hoverY });
			fx::Fx().ClickMarker(gfx::IsoPx(static_cast<float>(m_hoverX), static_cast<float>(m_hoverY)), Rgb(0x8AF0A0));
		}
	}
	// 누르고 있으면 계속 따라 걷기(리니지식)
	if (m_mouseHeld)
	{
		if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
			m_mouseHeld = false;
		else if (m_now > m_holdRepathAt && m_hoverX >= 0 && !overUi)
		{
			m_holdRepathAt = m_now + 0.2;
			if (m_goal.kind == GoalKind::Move && (m_goal.x != m_hoverX || m_goal.y != m_hoverY))
			{
				m_goal.x = m_hoverX;
				m_goal.y = m_hoverY;
				m_path.clear();
			}
		}
	}

	// 오른쪽 클릭: 대상 선택만
	if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !overUi && m_hover != ObjID{})
		m_target = m_hover;
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
}

void Game::SendStep(int dir)
{
	Entity* me = Self();
	if (!me) return;
	int dx, dy;
	path::DeltaOf(dir, dx, dy);
	if (!path::CanStep(m_predX, m_predY, dx, dy))
	{
		m_path.clear();
		return;
	}
	m_net.SendMove(dir);
	m_pending = true;
	m_pendingAt = m_now;
	m_predX += dx;
	m_predY += dy;
	const float interval = (m_hasteUntil > m_now) ? kHasteMoveInterval : kMoveInterval;
	m_nextMoveAt = m_now + interval;
	me->from = me->pos;
	me->to = Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
	me->moveT = 0.f;
	me->moveDur = interval;
	const int sx = dx - dy, sy = dx + dy;
	if (sx != 0) me->faceRight = sx > 0;
	if (sy != 0) me->faceBack = sy < 0;
	m_lastDir = dir;
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
	const int dir = path::DirectionOf(next.x - m_predX, next.y - m_predY);
	if (dir < 0)
	{
		m_path.clear();
		return false;
	}
	m_path.erase(m_path.begin());
	SendStep(dir);
	return true;
}

void Game::TickMovement()
{
	Entity* me = Self();
	if (!me || m_dead)
		return;

	// 응답이 너무 늦으면(쿨타임/존 이동 중 거절) 서버 위치로 되돌린다
	if (m_pending && m_now - m_pendingAt > kPendingTimeout)
	{
		m_pending = false;
		m_predX = m_srvX;
		m_predY = m_srvY;
		me->from = me->pos;
		me->to = Vector2{ static_cast<float>(m_srvX), static_cast<float>(m_srvY) };
		me->moveT = 0.f;
		me->moveDur = 0.15f;
		m_path.clear();
	}
	if (m_pending || m_now < m_nextMoveAt)
		return;

	// 1) 키보드(화면 방향 기준 WASD/방향키)
	if (!ui::TextFocused())
	{
		const int sx = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT) ? 1 : 0) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT) ? 1 : 0);
		const int sy = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN) ? 1 : 0) - (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? 1 : 0);
		if (sx != 0 || sy != 0)
		{
			// 화면 오른쪽 = 타일(+1,-1), 화면 아래 = 타일(+1,+1)
			const int dx = std::clamp(sx + sy, -1, 1), dy = std::clamp(sy - sx, -1, 1);
			ClearGoal();
			m_mouseHeld = false;
			if (path::CanStep(m_predX, m_predY, dx, dy))
				SendStep(path::DirectionOf(dx, dy));
			else if (dx != 0 && dy != 0 && path::CanStep(m_predX, m_predY, dx, 0))
				SendStep(path::DirectionOf(dx, 0));
			else if (dx != 0 && dy != 0 && path::CanStep(m_predX, m_predY, 0, dy))
				SendStep(path::DirectionOf(0, dy));
			return;
		}
	}

	// 2) 목표를 향한 경로
	switch (m_goal.kind)
	{
	case GoalKind::Move:
		if (!StepToward(m_goal.x, m_goal.y, 0) && !m_mouseHeld)
			ClearGoal();
		break;
	case GoalKind::Attack:
		if (Entity* t = Find(m_goal.target))
			StepToward(t->tx, t->ty, 1);
		break;
	case GoalKind::Pickup:
		StepToward(m_goal.x, m_goal.y, 0);
		break;
	case GoalKind::Talk:
		if (const auto* npc = WorldData::FindNpc(m_goal.npc))
			StepToward(npc->x, npc->y, 2);
		break;
	default:
		break;
	}
}

void Game::TickGoal()
{
	if (m_dead)
		return;
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
		if (!m_path.empty())
		{
			const path::Step last = m_path.back();
			if (Cheb(last.x, last.y, t->tx, t->ty) > 1)
				m_path.clear();
		}
		if (!m_pending && Cheb(m_srvX, m_srvY, t->tx, t->ty) <= 1)
		{
			m_path.clear();
			TryAttackTarget(*t);
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
		if (!m_pending && m_srvX == item->tx && m_srvY == item->ty)
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
		if (!m_pending && Cheb(m_srvX, m_srvY, npc->x, npc->y) <= 2)
		{
			OpenNpc(npc->id);
			ClearGoal();
		}
		break;
	}
	case GoalKind::Move:
		if (!m_pending && m_srvX == m_goal.x && m_srvY == m_goal.y && !m_mouseHeld)
			ClearGoal();
		break;
	default:
		break;
	}
}

void Game::TryAttackTarget(Entity& t)
{
	if (m_now < m_nextAttackAt)
		return;
	if (InTown())
	{
		AddCenter("마을 안에서는 싸울 수 없습니다.", Rgb(0xFFD070));
		ClearGoal();
		return;
	}
	if (t.kind == EntityKind::Player && !m_goal.force)
	{
		ClearGoal();
		return;
	}
	m_net.SendTargetAttack(t.id);
	m_nextAttackAt = m_now + kAttackInterval;
	if (Entity* me = Self())
	{
		me->attackT = 0.f;
		const int dx = t.tx - m_srvX, dy = t.ty - m_srvY;
		const int sx = dx - dy, sy = dx + dy;
		if (sx != 0) me->faceRight = sx > 0;
		if (sy != 0) me->faceBack = sy < 0;
		fx::Fx().Slash(TileToIso(me->pos), 36.f, me->faceRight ? 1.f : -1.f, me->faceBack, Rgb(0xFFFFFF, 220), 1.f);
	}
	sfx::Play(sfx::Id::Swing, 0.7f, 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────────
// 행동
// ─────────────────────────────────────────────────────────────────────────────
void Game::CastSkill()
{
	if (m_dead) return;
	if (InTown())
	{
		AddCenter("마을 안에서는 싸울 수 없습니다.", Rgb(0xFFD070));
		sfx::Play(sfx::Id::Error, 0.5f);
		return;
	}
	const double remain = m_lastSkillAt + CombatRule::kSkillCooldownMs / 1000.0 - m_now;
	if (remain > 0.0)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "십자베기 재사용 대기 중 (%.1f초)", remain);
		AddCenter(buf, Rgb(0xC8C8C8));
		sfx::Play(sfx::Id::Error, 0.4f);
		return;
	}
	m_lastSkillAt = m_now + 0.05;
	m_net.SendSkill();
	m_localSkillFxAt = m_now;
	if (Entity* me = Self())
	{
		me->castT = 0.f;
		fx::Fx().SkillCross(TileToIso(me->pos));
	}
	sfx::Play(sfx::Id::Skill);
	m_cam.AddTrauma(0.22f);
	m_cam.Kick(0.03f);
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
		const int d = Cheb(m_srvX, m_srvY, e.tx, e.ty);
		if (d < bestD) { bestD = d; best = &e; }
	}
	if (best && bestD == 0 && !m_pending)
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
	fx::Fx().ClickMarker(TileToIso(best->pos), Rgb(0xFF5A4A));
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

void Game::UseQuickSlot(int index)
{
	if (index < 0 || index >= static_cast<int>(m_quick.size())) return;
	const QuickSlot& q = m_quick[index];
	if (q.kind == 2) { CastSkill(); return; }
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

void Game::StartEnchant(int scrollSlot)
{
	if (m_enchant.stage >= 3) return;
	m_enchant = EnchantFlow{};
	m_enchant.stage = 1;
	m_enchant.scrollSlot = scrollSlot;
	m_enchant.scrollId = m_inv.slots[scrollSlot].itemId;
	m_invOpen = true;
	AddCenter("강화할 무기를 인벤토리에서 선택하세요.", Rgb(0xFFE08A));
	sfx::Play(sfx::Id::Open, 0.6f);
}

void Game::OpenNpc(uint16_t npcId)
{
	const auto* npc = WorldData::FindNpc(npcId);
	if (!npc) return;
	m_dialogNpc = npcId;
	m_shopMode = ShopMode::None;
	m_buyItem = 0;
	m_sellSlot = -1;
	m_teleportSel = -1;
	sfx::Play(sfx::Id::Open, 0.7f);
	// NPC가 나를 바라보는 연출은 DrawNpcs에서(가까우면 돌아본다)
}

void Game::CloseNpcWindows()
{
	if (m_dialogNpc != 0 || m_shopMode != ShopMode::None)
		sfx::Play(sfx::Id::Close, 0.5f);
	m_dialogNpc = 0;
	m_shopMode = ShopMode::None;
}

void Game::SubmitChat()
{
	std::string text = m_chatField.text;
	m_chatField.text.clear();
	// 앞뒤 공백 제거
	while (!text.empty() && text.back() == ' ') text.pop_back();
	size_t start = 0;
	while (start < text.size() && text[start] == ' ') ++start;
	text = text.substr(start);
	if (text.empty())
		return;
	m_net.SendChat(text);
}

// ─────────────────────────────────────────────────────────────────────────────
// 퀵슬롯 저장(캐릭터별 파일)
// ─────────────────────────────────────────────────────────────────────────────
void Game::LoadQuickSlots()
{
	m_quick = {};
	m_quick[0] = { 2, 0 };
	m_quick[1] = { 1, ItemTableData::Id::HealthPotion };
	m_quick[2] = { 1, ItemTableData::Id::OrangePotion };
	m_quick[3] = { 1, ItemTableData::Id::ClearPotion };
	m_quick[4] = { 1, ItemTableData::Id::HastePotion };
	m_quick[5] = { 1, ItemTableData::Id::ReturnScroll };
	m_quick[6] = { 1, ItemTableData::Id::TeleportScroll };
	std::ifstream in("quickslots_" + m_playerName + ".txt");
	if (!in) return;
	for (auto& q : m_quick)
	{
		int kind = 0, item = 0;
		if (!(in >> kind >> item)) break;
		q.kind = kind;
		q.itemId = static_cast<uint16_t>(item);
	}
}

void Game::SaveQuickSlots()
{
	if (m_playerName.empty() || !m_hasSelf) return;
	std::ofstream out("quickslots_" + m_playerName + ".txt");
	for (const auto& q : m_quick)
		out << q.kind << ' ' << q.itemId << '\n';
}

// ─────────────────────────────────────────────────────────────────────────────
// 테스트 자동 조작(--script). 사람이 조작하는 것과 같은 경로(목표/입력 함수)를 탄다.
// ─────────────────────────────────────────────────────────────────────────────
void Game::RunScript(float dt)
{
	if (m_opts.script.empty() || !m_hasSelf)
		return;
	m_scriptT += dt;
	const std::string& s = m_opts.script;
	if (s == "hunt")
	{
		// 마을 밖으로 나가 가장 가까운 몬스터를 계속 사냥
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 22, WorldData::kTownY0 + 50 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
			m_scriptStep = 2;
		else if (m_scriptStep == 2 && m_goal.kind == GoalKind::None)
		{
			bool any = false;
			for (auto& [id, e] : m_ents) if (e.kind == EntityKind::Monster && !e.dead) any = true;
			if (any) TargetNearestMonster();
			else if (m_goal.kind == GoalKind::None)
				SetGoal(Goal{ GoalKind::Move, ObjID{}, m_predX + util::RandI(-6, 6), m_predY + util::RandI(2, 8) });
		}
	}
	else if (s == "town")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, 1 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && m_dialogNpc != 0)
		{
			m_shopMode = ShopMode::Buy;
			m_scriptStep = 2;
		}
	}
	else if (s == "inventory")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_invOpen = true; m_scriptStep = 1; }
	}
	else if (s == "map")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_mapOpen = true; m_scriptStep = 1; }
	}
	else if (s == "help")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_helpOpen = true; m_scriptStep = 1; }
	}
	else if (s == "enchant")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.5f)
		{
			const int scroll = m_inv.FirstSlotOf(ItemTableData::Id::EnchantScroll);
			if (scroll >= 0) { StartEnchant(scroll); m_scriptStep = 1; }
		}
		else if (m_scriptStep == 1 && m_scriptT > 2.5f)
		{
			const int weapon = m_inv.EquippedSlot();
			if (weapon >= 0)
			{
				m_enchant.targetSlot = weapon;
				m_enchant.itemId = m_inv.slots[weapon].itemId;
				m_enchant.before = m_inv.slots[weapon].enchant;
				m_enchant.stage = 2;
				m_scriptStep = 2;
				m_scriptT = 0.f;
			}
		}
		else if (m_scriptStep == 2 && m_scriptT > 1.0f && m_opts.script == "enchant")
		{
			m_net.SendEnchant(static_cast<uint16_t>(m_enchant.scrollSlot), static_cast<uint16_t>(m_enchant.targetSlot));
			m_enchant.stage = 3;
			m_enchant.start = m_now;
			m_enchant.resultKnown = false;
			m_scriptStep = 3;
		}
	}
	else if (s == "teleport" || s == "boss")
	{
		// 텔레포터에게 가서 서리 설원(0번 지역)으로 이동 → (boss) 서리 거인에게 돌진
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, 3 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && m_dialogNpc == 3)
		{
			m_shopMode = ShopMode::Teleport;
			m_teleportSel = 0;
			m_scriptStep = 2;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 2 && m_scriptT > 0.8f)
		{
			m_lastShopSeq = m_net.SendNpcTeleport(3, 0);
			m_scriptStep = 3;
		}
		else if (m_scriptStep == 3 && Region() == 0)
		{
			m_scriptStep = s == "boss" ? 4 : 99;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 4 && m_scriptT > 1.0f)
		{
			bool attacking = false;
			for (auto& [id, e] : m_ents)
				if (e.IsBoss() && !e.dead)
				{
					m_target = id;
					if (m_goal.kind != GoalKind::Attack) SetGoal(Goal{ GoalKind::Attack, id });
					attacking = true;
				}
			if (!attacking && m_goal.kind == GoalKind::None)
				SetGoal(Goal{ GoalKind::Move, ObjID{}, 263, 265 });
			// 체력이 낮으면 물약
			if (m_hp * 2 < m_maxhp) UseQuickSlot(1);
		}
	}
	else if (s == "skill")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 22, WorldData::kTownY0 + 50 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
			m_scriptStep = 2;
		else if (m_scriptStep == 2)
		{
			if (m_goal.kind == GoalKind::None) TargetNearestMonster();
			int adjacent = 0;
			for (auto& [id, e] : m_ents)
				if (e.kind == EntityKind::Monster && !e.dead && std::abs(e.tx - m_srvX) + std::abs(e.ty - m_srvY) == 1) ++adjacent;
			if (adjacent > 0) CastSkill();
			if (m_hp * 2 < m_maxhp) UseQuickSlot(1);
		}
	}
	else if (s == "npc")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, 3 });
			m_scriptStep = 1;
		}
	}
	else if (s == "teleportwin")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, 3 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && m_dialogNpc == 3)
		{
			m_shopMode = ShopMode::Teleport;
			m_teleportSel = 7;
			m_scriptStep = 2;
		}
	}
	else if (s == "pkvictim" || s == "pkattacker")
	{
		// 마을 남문 밖 같은 곳으로 모여서 공격자가 피해자를 Ctrl+공격(PK)
		const bool attacker = s == "pkattacker";
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 21 + (attacker ? 1 : 0), WorldData::kTownY0 + 58 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && attacker && m_goal.kind == GoalKind::None && m_scriptT > 3.f)
		{
			for (auto& [id, e] : m_ents)
				if (e.kind == EntityKind::Player && id != m_selfId && !e.dead)
				{
					m_target = id;
					SetGoal(Goal{ GoalKind::Attack, id, 0, 0, 0, true });
					m_scriptStep = 2;
					break;
				}
		}
		else if (m_scriptStep == 2 && attacker && m_goal.kind == GoalKind::None)
			m_scriptStep = 1;
	}
	else if (s == "behind")
	{
		// 잡화점 건물 바로 뒤(북쪽)에 서서 실루엣이 보이는지 확인
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 7, WorldData::kTownY0 + 4 });
			m_scriptStep = 1;
		}
	}
	else if (s == "chat")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			m_net.SendChat("안녕하세요! 여명의 마을입니다");
			m_scriptStep = 1;
		}
	}
}
