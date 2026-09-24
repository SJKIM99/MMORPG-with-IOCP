#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "fx/SkillFx.h"
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
	if (opts.classId >= 0)
		m_newClassId = static_cast<uint8_t>(opts.classId);
	m_terrain.Init();
	const Vector2 town = gfx::IsoPx(WorldData::kTownX0 + 22.f, WorldData::kTownY0 + 22.f);
	m_cam.SnapTo(town);

	ui::SetDragIconDrawer([this](const ui::DragPayload& p, Vector2 at)
	{
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
	if (m_traceFile) { std::fclose(m_traceFile); m_traceFile = nullptr; }
	SaveQuickSlots();
	m_net.Disconnect();
	m_terrain.Shutdown();
	m_light.Shutdown();
}

void Game::BeginConnect()
{
	m_net.SetSimulatedLag(m_opts.lagMs, m_opts.jitterMs);
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
	m_mover.Reset(0, 0);
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
	skillfx::Clear();
	m_delayedHits.clear();
	m_deferredDies.clear();
	m_chainHops.clear();
	m_queuedSkill = -1;
	m_bufferSlot = -1;
	m_dashWaitSeq = 0;
	m_actionLockUntil = 0.0;
	m_charOpen = false;
	InitNpcs();
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
			const auto& st = m_mover.GetStats();
			std::printf("[move-stats] steps=%d acks=%d rejects=%d corrections=%d maxCorr=%.2f maxPending=%d teleports=%d\n",
				st.steps, st.acks, st.rejects, st.corrections, st.maxCorrection, st.maxPending, st.teleports);
			std::fflush(stdout);
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
			m_net.SendLoginEx(m_playerName, m_fPass.text.empty() ? "1234" : m_fPass.text, m_newClassId);
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
	TickCombat(dt);
	UpdateNpcs(sdt);

	// ── 이동(내 캐릭터: 예측·보정·스무딩 / 다른 개체: 걸음 시간대로 보간) ──
	// 서버 걸음을 재생하는 시간은 프레임 시간 상한(0.05초)에 묶이면 안 된다 — FPS가 낮을 때
	// 다른 개체가 느려지고 밀린다. 실제 경과 시간(히트스톱만 반영)을 쓴다.
	const float netDt = std::min(GetFrameTime(), 0.25f) * fx::Fx().TimeScale();
	m_mover.SetSpeedPercent(m_baseSpeedPercent + (m_hasteUntil > m_now ? MoveRule::kHasteBonusPercent : 0u));
	m_mover.Update(m_now, sdt);
	SyncMover();
	if (!m_opts.traceMove.empty())
	{
		if (!m_traceFile)
		{
			m_traceFile = std::fopen(m_opts.traceMove.c_str(), "w");
			if (m_traceFile) std::fprintf(m_traceFile, "t,lx,ly,vx,vy,speed,pending,keyx,keyy,rx,ry\n");
		}
		if (m_traceFile)
		{
			const Vector2 l = m_mover.Logical(), v = m_mover.Visual();
			// 다른 플레이어 하나(원격 보간 확인용)
			Vector2 r{ -1, -1 };
			for (const auto& [oid, o] : m_ents)
				if (oid != m_selfId && o.kind == EntityKind::Player) { r = o.pos; break; }
			std::fprintf(m_traceFile, "%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%d,%d,%d,%.4f,%.4f\n", m_now, l.x, l.y, v.x, v.y, m_mover.Speed(), m_mover.PendingCount(), m_scriptKeyX, m_scriptKeyY, r.x, r.y);
		}
	}

	// ── 엔티티 애니메이션 ──
	std::vector<ObjID> dead;
	for (auto& [id, e] : m_ents)
	{
		if (id == m_selfId)
		{
			const Vector2 prev = e.pos;
			e.pos = m_mover.Visual();
			e.logPos = m_mover.Logical();
			motion::Animate(e, prev, sdt);
		}
		else
			motion::UpdateRemote(e, netDt, sdt);

		if (e.kind != EntityKind::Item && e.moving > 0.2f)
		{
			// 발소리/먼지 — 한 걸음(위상 π)마다
			if ((e.walkPhase - e.lastStepDust) > util::kPi)
			{
				e.lastStepDust = e.walkPhase;
				if (e.row == nullptr || e.row->archetype != MonsterArchetype::eGhost)
					fx::Fx().Dust(TileToIso(e.pos), (id == m_selfId ? 0.8f : 0.4f) * (1.f + e.run));
				if (id == m_selfId)
					sfx::Play(sfx::Id::Step, 0.42f + 0.15f * e.run, 0.15f);
			}
			if (id == m_selfId && m_hasteUntil > m_now)
				fx::Fx().HasteTrail(TileToIso(e.pos));
		}
		else
			e.lastStepDust = e.walkPhase - util::kPi * 0.5f;   // 다시 걸을 때 반 걸음 뒤 첫 발소리
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
		// 이동 방향으로 살짝 앞을 보여 준다(속도에 비례, 멈추면 천천히 돌아옴)
		const Vector2 v = m_mover.Velocity();
		const Vector2 isoV{ (v.x - v.y) * gfx::kHalfW, (v.x + v.y) * gfx::kHalfH };
		const float len = std::sqrt(isoV.x * isoV.x + isoV.y * isoV.y);
		Vector2 lead{ 0, 0 };
		if (len > 1.f)
			lead = util::Mul(util::Norm(isoV), 34.f * util::Clamp01(len / 160.f));
		m_cam.SetLead(lead);
		if (m_mover.Snapped())
			m_cam.SnapTo(TileToIso(me->pos, 0.6f));
	}
	if (!ui::TextFocused() && !ui::MouseOverUi())
	{
		const float wheel = GetMouseWheelMove();
		if (wheel != 0.f) m_cam.AdjustZoom(wheel * 0.12f);
	}
	m_cam.Update(dt);

	// ── 이펙트/분위기 ──
	fx::Fx().Update(dt);
	skillfx::Update(sdt);
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
		if (const Vector2 at = NpcTile(m_dialogNpc); Cheb(m_predX, m_predY, static_cast<int>(at.x), static_cast<int>(at.y)) > WorldData::kNpcInteractRange + 2)
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
		if (m_charOpen) { m_charOpen = false; return; }
		if (m_invOpen) { m_invOpen = false; return; }
		m_target = ObjID{};
		ClearGoal();
		return;
	}
	if (ui::TextFocused() || m_dead)
		return;

	if (IsKeyPressed(KEY_I)) { m_invOpen = !m_invOpen; sfx::Play(m_invOpen ? sfx::Id::Open : sfx::Id::Close, 0.6f); }
	if (IsKeyPressed(KEY_C)) { m_charOpen = !m_charOpen; sfx::Play(m_charOpen ? sfx::Id::Open : sfx::Id::Close, 0.6f); }
	if (IsKeyPressed(KEY_M)) { m_mapOpen = !m_mapOpen; sfx::Play(m_mapOpen ? sfx::Id::Open : sfx::Id::Close, 0.6f); }
	if (IsKeyPressed(KEY_F1) || IsKeyPressed(KEY_H)) { m_helpOpen = !m_helpOpen; sfx::Play(sfx::Id::Open, 0.5f); }
	if (IsKeyPressed(KEY_G)) m_showGrid = !m_showGrid;
	if (IsKeyPressed(KEY_Z)) PickupHere();
	if (IsKeyPressed(KEY_TAB)) CycleTarget();
	if (IsKeyPressed(KEY_F)) Interact();
	// Space: 기본 공격 — 대상(없으면 가까운 적)에게 다가가 계속 친다. 누르고 있으면
	// 쓰러뜨린 뒤 다음 적으로 이어 간다. Ctrl을 같이 누르면 가까운 모험가(PK).
	if (IsKeyPressed(KEY_SPACE) || (IsKeyDown(KEY_SPACE) && m_goal.kind == GoalKind::None && m_now >= m_actionLockUntil))
	{
		const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
		const SkillRow* basic = SkillInSlot(0);
		Entity* t = basic ? PickSkillTarget(*basic) : nullptr;
		if (t)
		{
			m_target = t->id;
			SetGoal(Goal{ GoalKind::Attack, t->id, 0, 0, 0, t->kind == EntityKind::Player });
		}
		else if (!ctrl)
			TargetNearestMonster();
	}
	// 기술 1~5 · 물약/주문서 Q E R T(= F5~F8)
	for (int i = 0; i < 5; ++i)
		if (IsKeyPressed(KEY_ONE + i) || IsKeyPressed(KEY_KP_1 + i))
			TryCastSkill(i + 1);
	const int quickKeys[kQuickSlots] = { KEY_Q, KEY_E, KEY_R, KEY_T };
	for (int i = 0; i < kQuickSlots; ++i)
		if (IsKeyPressed(quickKeys[i]) || IsKeyPressed(KEY_F5 + i))
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
		float bestDepth = -1e9f;
		for (const auto& [nid, v] : m_npcView)
		{
			if (!v.row) continue;
			const Vector2 base = TileToIso(v.pos);
			const float h = actors::NpcHeight(*v.row);
			const float w = WorldData::IsAnimal(v.row->kind) ? 20.f : 22.f;
			const Rectangle r{ base.x - w, base.y - h, w * 2.f, h + 8 };
			const float depth = v.pos.x + v.pos.y;
			if (CheckCollisionPointRec(mouseIso, r) && depth > bestDepth) { bestDepth = depth; m_hoverNpc = nid; }
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
		// 마우스는 이동·대화·줍기 같은 "전투 밖" 상호작용만 — 몬스터/사람을 눌러도 공격하지 않는다.
		Entity* e = Find(m_hover);
		if (e && e->kind == EntityKind::Item)
		{
			SetGoal(Goal{ GoalKind::Pickup, e->id, e->tx, e->ty });
			fx::Fx().ClickMarker(TileToIso(e->pos), Rgb(0xFFD84A));
		}
		else if (m_hoverNpc != 0)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, m_hoverNpc });
		}
		else if (m_hoverX >= 0 && !(e && e->kind == EntityKind::Player && ctrl))
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

	// 1) 키보드 8방향(화면 기준 WASD/방향키) — 누르는 동안은 경로/자동 목표보다 우선
	if (!ui::TextFocused())
	{
		int sx = (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT) ? 1 : 0) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT) ? 1 : 0);
		int sy = (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN) ? 1 : 0) - (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? 1 : 0);
		if (sx == 0 && sy == 0) { sx = m_scriptKeyX; sy = m_scriptKeyY; }
		if (sx != 0 || sy != 0)
		{
			if (m_goal.kind != GoalKind::None)
				ClearGoal();
			m_mouseHeld = false;
			if (!m_mover.CanStep(m_now))
				return;
			// 화면 오른쪽 = 타일(+1,-1), 화면 아래 = 타일(+1,+1)
			const int dx = std::clamp(sx + sy, -1, 1), dy = std::clamp(sy - sx, -1, 1);
			const int dir = PickMoveDir(dx, dy);
			if (dir >= 0)
				SendStep(dir);
			return;
		}
	}

	if (!m_mover.CanStep(m_now))
		return;

	// 2) 목표를 향한 경로
	switch (m_goal.kind)
	{
	case GoalKind::Move:
		if (!StepToward(m_goal.x, m_goal.y, 0) && !m_mouseHeld && m_path.empty())
		{
			if (m_predX == m_goal.x && m_predY == m_goal.y)
				break;
			// 경로가 막혔으면 한 번 더 찾아보고 그래도 없으면 포기
			if (m_now > m_repathAt)
				ClearGoal();
		}
		break;
	case GoalKind::Attack:
		if (Entity* t = Find(m_goal.target))
			StepToward(t->tx, t->ty, AttackRange());
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
		if (arrived && m_predX == m_goal.x && m_predY == m_goal.y && !m_mouseHeld)
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
	TryCastSkill(0, true, t.id);   // 기본 공격도 기술 표(검격·룬 탄환·연속 찌르기)로
}

// ─────────────────────────────────────────────────────────────────────────────
// 행동
// ─────────────────────────────────────────────────────────────────────────────
void Game::CycleTarget()
{
	// 가까운 순서로 다음 몬스터(화면 안 10칸)
	std::vector<Entity*> list;
	for (auto& [id, e] : m_ents)
		if (e.kind == EntityKind::Monster && !e.dead && !e.removing && Cheb(m_predX, m_predY, e.tx, e.ty) <= 10)
			list.push_back(&e);
	if (list.empty())
	{
		AddCenter("주변에 대상이 없습니다.", Rgb(0xC8C8C8));
		return;
	}
	std::sort(list.begin(), list.end(), [&](const Entity* a, const Entity* b)
	{
		const int da = Cheb(m_predX, m_predY, a->tx, a->ty), db = Cheb(m_predX, m_predY, b->tx, b->ty);
		return da != db ? da < db : a->id.GetSerial() < b->id.GetSerial();
	});
	size_t next = 0;
	for (size_t i = 0; i < list.size(); ++i)
		if (list[i]->id == m_target) { next = (i + 1) % list.size(); break; }
	m_target = list[next]->id;
	fx::Fx().ClickMarker(TileToIso(list[next]->pos), Rgb(0xFF5A4A));
	sfx::Play(sfx::Id::Hover, 0.5f);
}

void Game::Interact()
{
	if (m_dead) return;
	// 가까운 NPC가 있으면 그쪽으로 가서 대화, 없으면 줍기
	const WorldData::NpcRow* best = nullptr;
	int bestD = WorldData::kNpcInteractRange + 1;   // 화면의 [F] 표시와 같은 기준
	for (const auto& npc : WorldData::kNpcs)
	{
		const Vector2 at = NpcTile(npc.id);
		const int d = Cheb(m_predX, m_predY, static_cast<int>(std::lround(at.x)), static_cast<int>(std::lround(at.y)));
		if (d < bestD) { bestD = d; best = &npc; }
	}
	if (best)
	{
		if (m_dialogNpc == best->id) { CloseNpcWindows(); return; }
		SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, best->id });
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
	m_net.SendNpcTalk(npcId);   // 서버가 거리를 확인하고 NPC를 멈춰 세워 나를 보게 한다
	NpcView* v = NpcViewOf(npcId);
	if (WorldData::IsAnimal(npc->kind))
	{
		// 동물은 쓰다듬는다 — 하트와 반가운 소리(서버가 모두에게 짖는 소리를 알린다)
		if (v)
		{
			v->heartT = 0.f;
			v->hopT = 0.f;
			const Vector2 iso = TileToIso(v->pos);
			for (int k = 0; k < 3; ++k)
				fx::Fx().FloatText(Vector2{ iso.x + util::RandF(-12.f, 12.f), iso.y }, actors::NpcHeight(*npc) + 6.f + k * 8.f, "♥", Rgb(0xFF7A9A), 16.f + k * 3.f, 1.1f + k * 0.2f);
		}
		sfx::Play(sfx::Id::Pet, 0.6f, 0.05f);
		return;
	}
	m_dialogNpc = npcId;
	m_dialogLine = -1;
	m_dialogTextAt = m_now;
	m_shopMode = ShopMode::None;
	m_buyItem = 0;
	m_sellSlot = -1;
	m_teleportSel = -1;
	sfx::Play(sfx::Id::Open, 0.7f);
	if (m_now - m_lastVoiceAt > 0.4)
	{
		m_lastVoiceAt = m_now;
		sfx::PlayPitched(sfx::Id::Voice, 0.55f, npc->kind == WorldData::NpcKind::eChild ? 1.55f : (npc->kind == WorldData::NpcKind::eElder || npc->kind == WorldData::NpcKind::eVillager ? 0.8f : 1.05f));
	}
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
	// 기본: Q 체력 물약 · E 주황 물약 · R 가속 물약 · F 귀환 주문서
	m_quick = {};
	m_quick[0] = { 1, ItemTableData::Id::HealthPotion };
	m_quick[1] = { 1, ItemTableData::Id::OrangePotion };
	m_quick[2] = { 1, ItemTableData::Id::HastePotion };
	m_quick[3] = { 1, ItemTableData::Id::ReturnScroll };
	std::ifstream in("quickslots2_" + m_playerName + ".txt");
	if (!in) return;
	for (auto& q : m_quick)
	{
		int kind = 0, item = 0;
		if (!(in >> kind >> item)) break;
		q.kind = kind == 1 ? 1 : 0;
		q.itemId = q.kind ? static_cast<uint16_t>(item) : 0;
	}
}

void Game::SaveQuickSlots()
{
	if (m_playerName.empty() || !m_hasSelf) return;
	std::ofstream out("quickslots2_" + m_playerName + ".txt");
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
			if (m_hp * 2 < m_maxhp) UseQuickSlot(0);
		}
	}
	else if (s == "skill" || s == "combat")
	{
		// 마을 밖으로 나가 가까운 몬스터에게 직업 기술을 돌려 가며 쓴다(연계 순서대로).
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 22, WorldData::kTownY0 + 50 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
			m_scriptStep = 2;
		else if (m_scriptStep == 2)
		{
			if (m_goal.kind == GoalKind::None)
			{
				bool any = false;
				for (auto& [id, e] : m_ents) if (e.kind == EntityKind::Monster && !e.dead && Cheb(m_predX, m_predY, e.tx, e.ty) <= 8) any = true;
				if (any) TargetNearestMonster();
				else SetGoal(Goal{ GoalKind::Move, ObjID{}, m_predX + util::RandI(-6, 6), m_predY + util::RandI(3, 8) });   // 사냥감을 찾아 더 나간다
			}
			Entity* t = Find(m_target);
			if (t && !t->dead && m_now >= m_actionLockUntil)
			{
				// 직업별 연계 순서(슬롯): 기사 방패 강타→처형→회전 베기→철벽→돌진 /
				// 룬술사 빙결 인장→혜성→번개→화염 고리 / 사냥꾼 독 칼날→그림자 돌진→후방 습격→월영 난무
				static const int order[3][5] = { { 1, 5, 2, 4, 3 }, { 1, 5, 4, 2, 0 }, { 2, 1, 4, 5, 0 } };
				const int cls = static_cast<int>(m_classId);
				const int d = Cheb(m_predX, m_predY, t->tx, t->ty);
				for (int k = 0; k < 5; ++k)
				{
					const int slot = order[cls][k];
					if (slot == 0) continue;
					const SkillRow* row = SkillInSlot(slot);
					if (!row || m_level < row->unlockLevel || m_now < m_skillReady[slot] || m_mp < static_cast<int>(row->mp)) continue;
					if (row->target == SkillTarget::Enemy && d > row->range) continue;
					if ((row->target == SkillTarget::Self || row->target == SkillTarget::Direction) && d > std::max<int>(1, row->radius)) continue;
					m_hoverX = t->tx; m_hoverY = t->ty;   // 지점 기술은 대상 칸으로
					if (TryCastSkill(slot, true, row->target == SkillTarget::Enemy ? t->id : ObjID{})) break;
				}
			}
			if (m_hp * 2 < m_maxhp) UseQuickSlot(0);
		}
	}
	else if (s == "charwin")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.5f) { m_charOpen = true; m_scriptStep = 1; }
	}
	else if (s == "plaza")
	{
		// 분수 앞에 서서 마을 사람들이 돌아다니는 것을 본다
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 21, WorldData::kTownY0 + 25 });
			m_scriptStep = 1;
		}
	}
	else if (s == "talk" || s == "priest" || s == "pet" || s == "soup")
	{
		// 촌장(6)과 이야기 / 사제(7)의 기도 / 누렁이(17) 쓰다듬기 / 여관 수프(8)
		const uint16_t npcId = s == "talk" ? 6 : (s == "priest" ? 7 : (s == "pet" ? 17 : 8));
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, npcId });
			m_scriptStep = 1;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 1 && (m_dialogNpc == npcId || (s == "pet" && m_goal.kind == GoalKind::None)))
		{
			m_scriptStep = 2;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 1 && m_goal.kind == GoalKind::None && m_scriptT > 2.f)
		{
			SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, npcId });   // NPC가 움직였다 — 다시 따라간다
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 2 && m_scriptT > 1.6f)
		{
			if (s == "talk") { m_dialogLine = 0; m_dialogTextAt = m_now; }
			else if (s == "priest") RequestNpcService(npcId, WorldData::NpcService::Heal);
			else if (s == "soup") RequestNpcService(npcId, WorldData::NpcService::Soup);
			else if (s == "pet") OpenNpc(npcId);
			m_scriptStep = 3;
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
	else if (s == "wasd")
	{
		// 8방향 키보드 이동 검증: 오른쪽 → 오른쪽 아래 → 위 → 왼쪽 위 → 왼쪽(마을 광장)
		struct Leg { float until; int kx, ky; };
		static const Leg legs[] = {
			{ 1.0f, 0, 0 }, { 2.6f, 1, 0 }, { 3.8f, 1, 1 }, { 5.2f, 0, -1 }, { 6.2f, -1, -1 }, { 7.4f, -1, 0 }, { 99.f, 0, 0 },
		};
		for (const Leg& leg : legs)
			if (m_scriptT < leg.until) { m_scriptKeyX = leg.kx; m_scriptKeyY = leg.ky; break; }
	}
	else if (s == "watch")
	{
		// 가만히 서서 다른 플레이어의 움직임을 본다(원격 보간 검증)
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
