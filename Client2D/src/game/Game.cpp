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
#include "audio/Voice.h"
#include "platform/Ime.h"
#include "core/Util.h"
#include "core/Clock.h"
#include "core/Perf.h"
#include "core/Recorder.h"
#include "game/Input.h"
#include "game/KeyBinds.h"

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
	if (clk::Shared())
		m_net.SetTimeSource(&clk::Now);   // 흉내 지연도 공유 시계로(샌드박스 녹화)
	m_debugOverlay = opts.debugOverlay;
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
	rec::Finish();
	perf::Print(stdout, "perf");
	{
		const auto& st = m_mover.GetStats();
		std::printf("[move-stats] steps=%d acks=%d rejects=%d corrections=%d maxCorr=%.2f maxPending=%d teleports=%d rtt=%.0fms\n",
			st.steps, st.acks, st.rejects, st.corrections, st.maxCorrection, st.maxPending, st.teleports, m_moveRttMs);
		std::fflush(stdout);
	}
	if (m_traceFile) { std::fclose(m_traceFile); m_traceFile = nullptr; }
	SaveControls();
	m_net.Disconnect();
	m_terrain.Shutdown();
	m_light.Shutdown();
}

void Game::BeginConnect()
{
	{
		net::NetClient::NetSim sim = m_opts.netsim;
		if (m_opts.lagMs > 0 && sim.latencyMs == 0) sim.latencyMs = m_opts.lagMs;
		if (m_opts.jitterMs > 0 && sim.jitterMs == 0) sim.jitterMs = m_opts.jitterMs;
		m_net.SetNetSim(sim);
	}
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
	// 퀘스트(로그인하면 QUEST_LIST_INF로 다시 온다)
	m_quests.clear();
	m_questDone = {};
	m_questCooldownUntil.clear();
	m_questsKnown = false;
	m_questLogOpen = false;
	m_dialogQuest = 0;
	m_trackedQuest = 0;
	m_questFlies.clear();
	m_questDoneFx.clear();
	m_discoveries.clear();
	m_chapterCard = 0;
	m_endingAt = -100.0;
	m_questDestValid = false;
	InitNpcs();
}

// ─────────────────────────────────────────────────────────────────────────────
void Game::Frame()
{
	perf::BeginFrame();
	clk::BeginFrame();
	in::BeginFrame();
	m_screenW = GetScreenWidth();
	m_screenH = GetScreenHeight();
	const float dt = std::min(clk::FrameDt(), 0.05f);
	m_now = clk::Now();
	m_runTime += dt;

	m_net.Update();
	ui::BeginFrame(dt);
	kb::BeginFrame();   // 채팅 · 한글 조합 · 키 지정 중이면 이번 프레임의 동작 키를 모두 막는다
	m_light.UpdateClock(m_opts.hour);
	// 엔딩: 하르벤이 다시 뛰면 새벽이 온다(잠시 뒤 원래 시각으로 천천히 돌아간다)
	if (m_endingAt > 0.0 && m_now - m_endingAt < 70.0)
	{
		const float t = static_cast<float>(m_now - m_endingAt);
		if (t > 3.8f && t < 70.f)
		{
			const float real = m_light.Hour();
			float diff = 6.8f - real;
			while (diff > 12.f) diff -= 24.f;
			while (diff < -12.f) diff += 24.f;
			const float k = util::Clamp01((t - 3.8f) / 2.f) * (1.f - util::Clamp01((t - 55.f) / 15.f));
			float h = real + diff * k;
			if (h < 0.f) h += 24.f;
			if (h >= 24.f) h -= 24.f;
			m_light.UpdateClock(h);
		}
	}
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
	if (m_debugOverlay)
		DrawDebugOverlay();
	DrawCursor();
	perf::MarkWorkEnd();
	perf::NoteParticles(static_cast<int>(fx::Particles().Count()));

	// 녹화(--record): 게임 화면에 들어오면 시작 기준점을 잡고, 다 뜨면 끝낸다
	if (rec::Enabled() && m_scene == Scene::Game)
	{
		rec::Arm(m_now);
		rec::Capture(m_now);
		if (rec::Finished() && m_opts.screenshot.empty())
			m_quit = true;
	}

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
			m_quit = true;   // 이동 · 성능 통계는 Shutdown이 찍는다
		}
	}
	EndDrawing();
	perf::EndFrame(GetFrameTime());
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
		// 성향 색: 적대(먼저 공격함) · 악명 · 명예 · 중립 — docs/DESIGN_SYSTEM.md 1장
		case AlignmentRule::NameColor::ePurple: return Rgb(0xE08C5A);
		case AlignmentRule::NameColor::eChaotic: return Rgb(0xD85848);
		case AlignmentRule::NameColor::eLawful: return Rgb(0x8FB2DC);
		default: return Rgb(0xECE8DE);
		}
	}
	if (e.kind == EntityKind::Monster && e.row)
	{
		// 위험도 색: 보스 · 훨씬 강함 · 강함 · 약함 · 비슷함
		if (e.row->boss) return Rgb(0xE07848);
		const int diff = e.row->level - m_level;
		if (diff >= 8) return Rgb(0xD85046);
		if (diff >= 3) return Rgb(0xE0A05A);
		if (diff <= -8) return Rgb(0x8C8C88);
		return Rgb(0xECE6D6);
	}
	// 바닥 장비는 희귀도 색(서버가 알려 준 희귀도 — 옵션은 주워야 보인다)
	if (const ItemTableRow* row = ItemTableData::Find(e.itemId); row && row->type == ItemType::eEquipment)
	{
		const int floor = static_cast<int>(ItemRules::FloorRarity(*row));
		const int sent = std::min<int>(e.weaponGrade, static_cast<int>(Rarity::Count) - 1);
		return icons::RarityColor(static_cast<Rarity>(std::max(floor, sent)));
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
	TickLoot();
	UpdateNpcs(sdt);
	UpdateQuests(dt);

	// ── 이동(내 캐릭터: 예측·보정·스무딩 / 다른 개체: 걸음 시간대로 보간) ──
	// 서버 걸음을 재생하는 시간은 프레임 시간 상한(0.05초)에 묶이면 안 된다 — FPS가 낮을 때
	// 다른 개체가 느려지고 밀린다. 실제 경과 시간(히트스톱만 반영)을 쓴다.
	const float netDt = std::min(clk::FrameDt(), 0.25f) * fx::Fx().TimeScale();
	m_mover.SetSpeedPercent(m_baseSpeedPercent + (m_hasteUntil > m_now ? MoveRule::kHasteBonusPercent : 0u));
	m_mover.Update(m_now, sdt);
	SyncMover();
	if (!m_opts.traceMove.empty())
	{
		if (!m_traceFile)
		{
			m_traceFile = std::fopen(m_opts.traceMove.c_str(), "w");
			if (m_traceFile) std::fprintf(m_traceFile, "t,lx,ly,vx,vy,speed,pending,keyx,keyy,rx,ry,camx,camy,zoom,face,rface\n");
		}
		if (m_traceFile)
		{
			const Vector2 l = m_mover.Logical(), v = m_mover.Visual();
			// 다른 플레이어 하나(원격 보간 확인용)
			Vector2 r{ -1, -1 };
			int rface = -1;
			for (const auto& [oid, o] : m_ents)
				if (oid != m_selfId && o.kind == EntityKind::Player) { r = o.pos; rface = (o.faceRight ? 1 : 0) | (o.faceBack ? 2 : 0); break; }
			int face = -1;
			if (const Entity* me = Self()) face = (me->faceRight ? 1 : 0) | (me->faceBack ? 2 : 0);
			const Vector2 cam = m_cam.Target();
			std::fprintf(m_traceFile, "%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%d,%d,%d,%.4f,%.4f,%.3f,%.3f,%.4f,%d,%d\n", m_now, l.x, l.y, v.x, v.y, m_mover.Speed(), m_mover.PendingCount(),
				m_scriptKeyX, m_scriptKeyY, r.x, r.y, cam.x, cam.y, m_cam.Zoom(), face, rface);
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
// 강화 시작 · NPC 창 · 채팅
// ─────────────────────────────────────────────────────────────────────────────
void Game::StartEnchant(int scrollSlot)
{
	if (m_enchant.stage >= 3) return;
	m_enchant = EnchantFlow{};
	m_enchant.stage = 1;
	m_enchant.scrollSlot = scrollSlot;
	m_enchant.scrollId = m_inv.slots[scrollSlot].itemId;
	m_invOpen = true;
	AddCenter("강화할 무기를 소지품에서 고르세요.", ui::theme::kDawnHi);
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
	// 보고할 퀘스트 · 이 사람이 들려줄 메인 이야기가 있으면 바로 펼친다
	m_dialogQuest = AutoQuestForNpc(npcId);
	m_buyItem = 0;
	m_sellSlot = -1;
	m_teleportSel = -1;
	sfx::Play(sfx::Id::Open, 0.7f);
	if (m_now - m_lastVoiceAt > 0.4)
	{
		m_lastVoiceAt = m_now;
		// 목소리(Higgsfield 음성 대사)가 있으면 인사말을 실제로 말한다 — 없으면 예전 "웅얼거림" 효과음
		if (!voice::Play("npc_" + std::to_string(npcId), 0.9f, 1))
			sfx::PlayPitched(sfx::Id::Voice, 0.55f, npc->kind == WorldData::NpcKind::eChild ? 1.55f : (npc->kind == WorldData::NpcKind::eElder || npc->kind == WorldData::NpcKind::eVillager ? 0.8f : 1.05f));
	}
}

void Game::CloseNpcWindows()
{
	if (m_dialogNpc != 0 || m_shopMode != ShopMode::None)
		sfx::Play(sfx::Id::Close, 0.5f);
	m_dialogNpc = 0;
	m_dialogQuest = 0;
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
			const int scroll = m_inv.FirstSlotOf(ItemTableData::Id::EngravingScroll);
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
		// 길잡이에게 가서 벨고라 설원(0번 지역)으로 이동 → (boss) 모로즈에게 돌진
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
			if (m_hp * 2 < m_maxhp) UseItemSlot(kPotionSlot);
		}
	}
	else if (s == "skill" || s == "combat" || s == "loot")
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
			// loot: 떨어진 물건이 가까이 있으면 먼저 줍는다(희귀도 빛기둥 · 획득 연출 확인용)
			if (s == "loot" && (m_goal.kind == GoalKind::None || m_goal.kind == GoalKind::Move))
			{
				const Entity* best = nullptr;
				int bestD = 99;
				for (auto& [id, e] : m_ents)
				{
					if (e.kind != EntityKind::Item || e.removing || e.flyT >= 0.f) continue;
					const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
					if (d < bestD && d <= 7) { bestD = d; best = &e; }
				}
				if (best) SetGoal(Goal{ GoalKind::Pickup, best->id, best->tx, best->ty });
			}
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
				// 각인술사 서리 각인→낙성→번개→화염 고리 / 사냥꾼 독 칼날→그늘 걸음→후방 습격→그믐 베기
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
					SetScriptAim(t->tx, t->ty, t->id);   // 사람이 커서를 대상에 올린 것처럼(지점 기술은 그 칸으로)
					if (TryCastSkill(slot, true, row->target == SkillTarget::Enemy ? t->id : ObjID{})) break;
				}
			}
			if (m_hp * 2 < m_maxhp) UseItemSlot(kPotionSlot);
		}
	}
	else if (s == "gear" || s == "gearsort")
	{
		// 장비창·툴팁·비교를 차례로 비춘다(스크린샷 간격 2초에 맞춤)
		auto hoverGrid = [&](int idx)
		{
			ui::SetMouseOverride(true, Vector2{ m_invGridPos.x + (idx % 6) * 58.f + 27.f, m_invGridPos.y + (idx / 6) * 58.f + 27.f });
		};
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_invOpen = true; m_scriptStep = 1; }
		if (m_scriptStep >= 1)
		{
			const float t = m_scriptT;
			if (s == "gearsort")
			{
				if (m_scriptStep == 1 && t > 2.5f) { m_invSearch.text = "치명"; m_scriptStep = 2; }
				else if (m_scriptStep == 2 && t > 5.0f) { m_invSearch.text.clear(); m_net.SendItemSort(); m_sortSentAt = m_now; m_scriptStep = 3; }
				ui::SetMouseOverride(true, Vector2{ 20.f, static_cast<float>(m_screenH) - 20.f });
			}
			else
			{
				// 가방에서 첫 번째로 "입지 않은 장비"를 골라 비교 툴팁, 그다음 장비칸 무기
				int unequipped[3] = { -1, -1, -1 };
				int n = 0;
				for (int i = 0; i < MAX_INVENTORY_SLOTS && n < 3; ++i)
					if (!m_inv.slots[i].Empty() && !m_inv.slots[i].equipped && ItemTableData::SlotOf(m_inv.slots[i].itemId) != EquipSlot::None)
						unequipped[n++] = i;
				if (t < 2.f) ui::SetMouseOverride(true, Vector2{ 20.f, static_cast<float>(m_screenH) - 20.f });
				else if (t < 4.f && unequipped[0] >= 0) hoverGrid(unequipped[0]);
				else if (t < 6.f && unequipped[1] >= 0) hoverGrid(unequipped[1]);
				else if (t < 8.f && unequipped[2] >= 0) hoverGrid(unequipped[2]);
				else if (t < 10.f)
				{
					const Rectangle& r = m_equipRects[static_cast<size_t>(EquipSlot::Weapon)];
					ui::SetMouseOverride(true, Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f });
				}
				else ui::SetMouseOverride(true, Vector2{ 20.f, static_cast<float>(m_screenH) - 20.f });
			}
		}
	}
	else if (s == "lootshow")
	{
		// 가방의 장비를 한 칸씩 옮겨 가며 바닥에 내려놓는다 — 희귀도별 빛기둥 확인용
		const int bx = WorldData::kTownX0 + 16, by = WorldData::kTownY0 + 27;
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { SetGoal(Goal{ GoalKind::Move, ObjID{}, bx, by }); m_scriptStep = 1; }
		else if (m_scriptStep >= 1 && m_scriptStep < 7 && m_goal.kind == GoalKind::None && m_now >= m_actionLockUntil)
		{
			const int k = m_scriptStep - 1;
			if (m_predX == bx + k && m_predY == by)
			{
				// 가장 희귀한 "입지 않은 장비"부터
				int best = -1;
				for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
				{
					const InvSlot& it = m_inv.slots[i];
					if (it.Empty() || it.equipped || ItemTableData::SlotOf(it.itemId) == EquipSlot::None) continue;
					if (best < 0 || SlotRarity(it) > SlotRarity(m_inv.slots[best])) best = i;
				}
				if (best >= 0) m_net.SendDiscard(static_cast<uint16_t>(best), 1);
				m_scriptStep += 1;
				SetGoal(Goal{ GoalKind::Move, ObjID{}, bx + k + 1, by });
			}
			else
				SetGoal(Goal{ GoalKind::Move, ObjID{}, bx + k, by });
		}
		else if (m_scriptStep == 7 && m_goal.kind == GoalKind::None)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, bx + 2, by + 3 });
			m_scriptStep = 8;
		}
	}
	else if (s == "smith")
	{
		// 대장장이(2)의 구매 창(두 줄 목록)
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, 2 }); m_scriptStep = 1; }
		else if (m_scriptStep == 1 && m_dialogNpc != 0) { m_shopMode = ShopMode::Buy; m_buyItem = ItemTableData::Id::PaddedArmor; m_scriptStep = 2; }
	}
	else if (s == "charwin")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.5f) { m_charOpen = true; m_scriptStep = 1; }
	}
	else if (s == "stand")
	{
		// 이름마다 다른 칸에 서 있는다(여러 클라이언트로 외형 비교)
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			uint32_t h = 2166136261u;
			for (char c : m_playerName) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 17 + static_cast<int>(h % 6u), WorldData::kTownY0 + 25 + static_cast<int>((h / 6u) % 2u) * 2 });
			m_scriptStep = 1;
		}
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
	else if (s == "talk" || s == "priest" || s == "pet" || s == "soup" || s.rfind("talk:", 0) == 0)
	{
		// 촌장(6)과 이야기 / 사제(7)의 기도 / 누렁이(17) 쓰다듬기 / 여관 수프(8) / talk:N = N번 NPC와 이야기
		const uint16_t npcId = s.rfind("talk:", 0) == 0 ? static_cast<uint16_t>(std::atoi(s.c_str() + 5))
			: (s == "talk" ? 6 : (s == "priest" ? 7 : (s == "pet" ? 17 : 8)));
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
			if (s == "talk" || s.rfind("talk:", 0) == 0) { m_dialogLine = 0; m_dialogTextAt = m_now; }
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
	else if (s.rfind("runline", 0) == 0 || s == "runcircle" || s.rfind("runcircle:", 0) == 0 || s.rfind("follow:", 0) == 0)
	{
		// 녹화용 이동 시나리오(Phase 1 전/후 비교) — 사람처럼 **마우스로** 조작한다(in::SetScriptMouse).
		//   runline[:dx,dy[,초]]  화면 방향(dx,dy)으로 커서를 두고 좌클릭을 누른 채 달린다(기본 오른쪽 6초)
		//   runcircle[:초]         분수 둘레를 도는 점을 커서로 앞서 가리키며 누른 채 달린다(기본 30초)
		//   follow:이름            그 플레이어 위에 커서를 두고 누른 채 따라간다
		Entity* me = Self();
		if (!me) return;
		const Vector2 meScr = GetWorldToScreen2D(TileToIso(me->pos, 0.f), m_cam2d);
		const float start = 1.5f;
		if (m_scriptT < start) { in::SetScriptMouse(Vector2{ meScr.x, meScr.y - 200.f }, false); return; }
		if (s.rfind("runline", 0) == 0)
		{
			float dx = 1.f, dy = 0.f, secs = 6.f;
			if (s.size() > 8) std::sscanf(s.c_str() + 8, "%f,%f,%f", &dx, &dy, &secs);
			const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
			const bool hold = m_scriptT < start + secs;
			in::SetScriptMouse(Vector2{ meScr.x + dx / l * 240.f, meScr.y - 30.f + dy / l * 240.f }, hold);
		}
		else if (s.rfind("runcircle", 0) == 0)
		{
			float secs = 30.f;
			if (s.size() > 10) secs = static_cast<float>(std::atof(s.c_str() + 10));
			const Vector2 c{ WorldData::kTownX0 + 21.5f, WorldData::kTownY0 + 21.5f };   // 분수 가운데
			const float R = 5.f;
			const float a = std::atan2(me->pos.y - c.y, me->pos.x - c.x) + 0.9f;          // 도는 쪽으로 약 50° 앞
			const Vector2 aim{ c.x + std::cos(a) * R, c.y + std::sin(a) * R };
			const Vector2 scr = GetWorldToScreen2D(TileToIso(aim, 0.f), m_cam2d);
			in::SetScriptMouse(scr, m_scriptT < start + secs);
		}
		else
		{
			const std::string who = s.substr(7);
			Entity* t = nullptr;
			for (auto& [id, e] : m_ents)
				if (e.kind == EntityKind::Player && id != m_selfId && e.name == who) { t = &e; break; }
			if (t)
			{
				const Vector2 scr = GetWorldToScreen2D(TileToIso(t->pos, 0.f), m_cam2d);
				in::SetScriptMouse(Vector2{ scr.x, scr.y - 24.f }, true);
			}
			else
				in::SetScriptMouse(Vector2{ meScr.x, meScr.y - 200.f }, false);
		}
	}
	else if (s == "aimcombat")
	{
		// Phase 1-A 조작을 사람과 같은 경로로: 마을 밖으로 나가 커서를 가장 가까운 적에 올리고(외곽선)
		// 우클릭 기본 공격(누르고 떼기) · 기술 칸을 키로(kb::ScriptPress) · 체력이 낮으면 물약 칸
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 22, WorldData::kTownY0 + 50 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
			m_scriptStep = 2;
		else if (m_scriptStep == 2)
		{
			Entity* best = nullptr;
			int bestD = 99;
			for (auto& [id, e] : m_ents)
			{
				if (e.kind != EntityKind::Monster || e.dead || e.removing) continue;
				const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
				if (d < bestD) { bestD = d; best = &e; }
			}
			if (!best || bestD > 9)
			{
				in::ClearScriptMouse();
				if (m_goal.kind == GoalKind::None)
					SetGoal(Goal{ GoalKind::Move, ObjID{}, m_predX + util::RandI(-6, 6), m_predY + util::RandI(3, 8) });
				return;
			}
			const Vector2 scr = GetWorldToScreen2D(util::Add(TileToIso(best->pos, 0.f), Vector2{ 0.f, -(best->row ? actors::MonsterHeight(*best->row) : 40.f) * 0.45f }), m_cam2d);
			const bool rmb = std::fmod(m_scriptT, 1.4f) < 0.5f;   // 우클릭을 눌렀다 뗐다
			in::SetScriptMouse(scr, false, rmb);
			if (m_now >= m_scriptNextCast && m_now >= m_actionLockUntil)
			{
				for (int k = 0; k < 10; ++k)
				{
					const int hot = 1 + (m_scriptCastIdx + k) % 10;   // Q W E R A S D F V (Space는 빼고 도는 차례)
					const SkillRow* row = HotSkill(hot);
					if (!row || hot == kHotMobility || m_level < row->unlockLevel || m_now < m_skillReady[row->slot] || m_mp < static_cast<int>(row->mp)) continue;
					kb::ScriptPress(kb::HotAct(hot));
					m_scriptCastIdx = hot;
					break;
				}
				m_scriptNextCast = m_now + 1.6;
			}
			if (m_hp * 2 < m_maxhp) kb::ScriptPress(kb::Act::Potion);
		}
	}
	else if (s == "bigmap")
	{
		// Tab 확장 미니맵을 켠 채 좌클릭을 누르고 달린다(창이 아니라 달리면서 본다)
		Entity* me = Self();
		if (!me) return;
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { kb::ScriptPress(kb::Act::BigMap); m_scriptStep = 1; }
		const Vector2 meScr = GetWorldToScreen2D(TileToIso(me->pos, 0.f), m_cam2d);
		in::SetScriptMouse(Vector2{ meScr.x + 200.f, meScr.y + 60.f }, m_scriptT > 1.5f && m_scriptT < 5.5f);
	}
	else if (s == "keysui" || s == "keysconflict")
	{
		// 조작 설정 창(Esc 메뉴) — keysconflict: 기술 1에 W를 눌러 겹침 경고를 띄운다
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_keysOpen = true; m_scriptStep = 1; }
		if (s == "keysconflict" && m_scriptStep == 1 && m_scriptT > 1.6f) { ApplyKeyBind(static_cast<int>(kb::Act::Skill1), KEY_W, 0, false); m_scriptStep = 2; }
	}
	else if (s == "castpreview")
	{
		// 표시 후 시전: 기술 1을 누르고 있는 동안 커서 지점에 범위가 보이고, 떼면 나간다
		Entity* me = Self();
		if (!me) return;
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { kb::Opt().castMode = kb::CastMode::ShowThenCast; m_scriptStep = 1; }
		const Vector2 meScr = GetWorldToScreen2D(TileToIso(me->pos, 0.f), m_cam2d);
		in::SetScriptMouse(Vector2{ meScr.x + 150.f, meScr.y + 70.f }, false);
		kb::ScriptHold(kb::Act::Skill1, m_scriptT > 1.5f && m_scriptT < 4.5f);
	}
	else if (s == "menu")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_menuOpen = true; m_scriptStep = 1; }
	}
	else if (s == "skillwin")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { kb::ScriptPress(kb::Act::SkillWindow); m_scriptStep = 1; }
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
			m_net.SendChat("안녕하세요, 데니차입니다.");
			m_scriptStep = 1;
		}
	}
	else if (s == "quest" || s == "questlog")
	{
		// 새 캐릭터: 촌장에게 첫 이야기(장 제목) → 사제에게 보고(완료 연출) → 두 번째 이야기 → 남문 밖 사냥
		auto talkTo = [&](uint16_t npc) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, npc }); m_scriptT = 0.f; };
		switch (m_scriptStep)
		{
		case 0: if (m_scriptT > 2.0f && m_questsKnown) { talkTo(6); m_scriptStep = 1; } break;
		case 1:
			if (m_dialogNpc == 6 && m_dialogQuest == 1) { m_scriptStep = 2; m_scriptT = 0.f; }
			else if (m_goal.kind == GoalKind::None && m_scriptT > 2.5f) talkTo(6);
			break;
		case 2: if (m_scriptT > 3.5f) { SendQuest(QuestAction::Accept, 1); m_scriptStep = 3; m_scriptT = 0.f; } break;
		case 3:
			if (s == "questlog" && m_scriptT > 6.5f) { CloseNpcWindows(); m_questLogOpen = true; m_scriptStep = 99; }
			else if (s == "quest" && m_scriptT > 7.0f) { CloseNpcWindows(); talkTo(7); m_scriptStep = 4; }
			break;
		case 4:
			if (m_dialogNpc == 7 && m_dialogQuest == 1) { m_scriptStep = 5; m_scriptT = 0.f; }
			else if (m_goal.kind == GoalKind::None && m_scriptT > 2.5f) talkTo(7);
			break;
		case 5: if (m_scriptT > 3.0f) { SendQuest(QuestAction::Complete, 1); m_scriptStep = 6; m_scriptT = 0.f; } break;
		case 6: if (m_scriptT > 3.5f && m_dialogQuest == 2) { SendQuest(QuestAction::Accept, 2); m_scriptStep = 7; m_scriptT = 0.f; } break;
		case 7: if (m_scriptT > 2.5f) { CloseNpcWindows(); SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kTownX0 + 21, WorldData::kTownY0 + 50 }); m_scriptStep = 8; m_scriptT = 0.f; } break;
		case 8:
			if (m_goal.kind == GoalKind::None)
			{
				bool any = false;
				for (auto& [id, e] : m_ents) if (e.kind == EntityKind::Monster && !e.dead && IsQuestTarget(e) && Cheb(m_predX, m_predY, e.tx, e.ty) <= 9) any = true;
				if (any)
				{
					// 퀘스트 대상부터
					Entity* best = nullptr;
					int bestD = 99;
					for (auto& [id, e] : m_ents)
						if (e.kind == EntityKind::Monster && !e.dead && IsQuestTarget(e) && Cheb(m_predX, m_predY, e.tx, e.ty) < bestD) { best = &e; bestD = Cheb(m_predX, m_predY, e.tx, e.ty); }
					if (best) { m_target = best->id; SetGoal(Goal{ GoalKind::Attack, best->id }); }
				}
				else SetGoal(Goal{ GoalKind::Move, ObjID{}, m_predX + util::RandI(-6, 6), m_predY + util::RandI(2, 7) });
			}
			if (m_hp * 2 < m_maxhp) UseItemSlot(kPotionSlot);
			break;
		default: break;
		}
	}
	else if (s.rfind("chapter", 0) == 0)
	{
		// chapter = 제2장, chapterN = 제N장
		const int n = s.size() > 7 ? std::clamp(std::atoi(s.c_str() + 7), 1, static_cast<int>(QuestData::kChapterCount)) : 2;
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_chapterCard = n; m_chapterCardAt = m_now; m_chapterBellPlayed = false; m_scriptStep = 1; }
	}
	else if (s == "discover")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_discoveries.push_back(Discovery{ 3, m_now, false }); sfx::Play(sfx::Id::Discover); m_scriptStep = 1; }
	}
	else if (s == "questdone")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_questDoneFx.push_back(QuestDoneFx{ 5, m_now, 400, 600, ItemTableData::Id::TravelerCloak, 1, 2 }); sfx::Play(sfx::Id::QuestComplete); m_scriptStep = 1; }
	}
	else if (s == "ending")
	{
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { m_endingAt = m_now; m_endingStage = 0; m_scriptStep = 1; }
	}
}
