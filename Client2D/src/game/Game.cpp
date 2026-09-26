#include "game/Game.h"
#include "render/Height.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "fx/SkillFx.h"
#include "fx/Vfx.h"
#include "fx/Trail.h"
#include "render/Post.h"
#include "render/FlashLight.h"
#include "fx/FxScale.h"
#include "render/Draw.h"
#include "render/Props.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "render/Wind.h"
#include "audio/Ambience.h"
#include "audio/Voice.h"
#include "platform/Ime.h"
#include "core/Util.h"
#include "core/Clock.h"
#include "core/Perf.h"
#include "core/Recorder.h"
#include "game/Input.h"
#include "game/KeyBinds.h"
#include "ui/Wm.h"

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
	const Vector2 town = gfx::IsoPx(TownMap::kTownCenter.x, TownMap::kTownCenter.y);
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
	if (wm::Dirty()) SaveUiLayout();
	m_net.Disconnect();
	m_terrain.Shutdown();
	m_light.Shutdown();
	if (m_stage.ready) { UnloadRenderTexture(m_stage.rt); m_stage.ready = false; }
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
	++m_frameNo;
	m_screenW = GetScreenWidth();
	m_screenH = GetScreenHeight();
	const float dt = std::min(clk::FrameDt(), 0.05f);
	m_now = clk::Now();
	m_runTime += dt;

	m_net.Update();
	sfx::Update(dt);
	ApplyFxOptions();
	// UI 배율(Phase 3-A): 게임 화면만(로그인 화면은 1:1로 그린다)
	ui::SetUiScale(m_scene == Scene::Game || m_scene == Scene::Disconnected ? EffectiveUiScale() : 1.f);
	ui::BeginFrame(dt);
	{
		const Vector2 ls = ui::LogicalScreen();
		m_uiW = ls.x;
		m_uiH = ls.y;
		wm::BeginFrame(ls, m_now, m_scene == Scene::Game && ModalOpen(), m_uiEdit && m_scene == Scene::Game);
	}
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
	wind::Update(static_cast<float>(m_now));

	switch (m_scene)
	{
	case Scene::Login:
	case Scene::Connecting: UpdateLogin(dt); break;
	case Scene::Game: UpdateGame(dt); break;
	case Scene::Disconnected: m_time += dt; fx::Fx().Update(dt); break;
	}
	if (m_scene == Scene::Game) RenderSkillStage();   // 렌더 텍스처는 화면 그리기 전에
	// 창 배치 저장: 바뀐 뒤 2초에 한 번(끄는 동안 매 프레임 쓰지 않게)
	if (m_scene == Scene::Game && wm::Dirty() && !wm::Busy() && m_now >= m_uiSaveAt)
	{
		SaveUiLayout();
		m_uiSaveAt = m_now + 2.0;
	}

	BeginDrawing();
	ClearBackground(Color{ 8, 8, 12, 255 });
	switch (m_scene)
	{
	case Scene::Login:
	case Scene::Connecting: DrawLogin(); break;
	case Scene::Game: DrawGame(); break;
	case Scene::Disconnected: DrawGame(); ui::BindXform(); DrawDisconnected(); ui::UnbindXform(); break;
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
	const Vector2 c{ TownMap::kTownCenter.x + std::cos(a) * 14.f, TownMap::kTownCenter.y + std::sin(a) * 12.f };
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
bool Game::CombatBlocked() const { return InTown() && !WorldData::IsTrainingYard(m_predX, m_predY); }

// 바닥 종류(발소리 · 먼지): 0 풀 · 1 돌 · 2 나무 · 3 눈 · 4 모래
int Game::GroundAt(Vector2 tile) const
{
	const int x = static_cast<int>(std::floor(tile.x + 0.5f)), y = static_cast<int>(std::floor(tile.y + 0.5f));
	if (const char c = WorldData::TownCell(x, y); c != '\0')
	{
		switch (c)
		{
		case '=': case ',': case 'G': case 'N': case '^': case 'v': case '<': case '>': return 1;   // 돌 · 계단
		case 'b': return 2;   // 다리(나무)
		case 's': case ':': return 4;   // 모래 · 흙길
		default: return 0;
		}
	}
	const auto* r = WorldData::FindRegion(WorldData::RegionIdAt(x, y));
	if (!r) return 0;
	using WorldData::Biome;
	switch (r->biome)
	{
	case Biome::eSnow: return 3;
	case Biome::eDesert: case Biome::eBeach: case Biome::eCanyon: case Biome::eWasteland: return 4;
	case Biome::eVolcanic: case Biome::eRuins: case Biome::eOrcFort: case Biome::eHell: case Biome::eGraveyard: case Biome::eAntHill: return 1;
	default: return 0;
	}
}
int Game::Region() const { return WorldData::RegionIdAt(m_predX, m_predY); }

Vector2 Game::TileToIso(Vector2 tile, float z) const { return gfx::IsoPx(tile.x, tile.y, z); }
Vector2 Game::SurfaceIso(Vector2 tile, float z) const { return gfx::IsoPx(tile.x, tile.y, z + hgt::BodyZ(tile)); }

void Game::AddChat(const std::string& text, Color color)
{
	// 게임 안내 · 시스템 줄 → 상시 채팅창의 시스템 채널(Phase 3-B)
	AddChatMsg(chat::Ch::System, "", 0, text, color);
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
	// 이동 · 걸음 재생 시간: 프레임 시간 상한(0.05초)에 묶이면 FPS가 낮을 때 다른 개체 · NPC가 느려지고 밀린다.
	// 실제 경과 시간(0.25초 상한, 히트스톱만 반영)을 쓴다.
	const float netDt = std::min(clk::FrameDt(), 0.25f) * fx::Fx().TimeScale();

	HandleHotkeys();
	HandleWorldInput();
	TickGoal();
	TickMovement();
	RunScript(dt);
	TickCombat(dt);
	TickSkills(dt);
	TickSkillStage(dt);   // 기술 창 시연(창이 보여 줄 때만)
	TickLoot();
	UpdateNpcs(netDt);
	UpdateQuests(dt);

	// ── 이동(내 캐릭터: 선확정 예측 · 화면 추종자 / 다른 개체: 100ms 뒤 재생 시간표) ──
	// 통신이 멈췄나(원격 외삽 판단) — 마지막 패킷을 받은 뒤 흐른 시간
	if (m_net.PacketsIn() != m_lastPacketsIn) { m_lastPacketsIn = m_net.PacketsIn(); m_lastRecvAt = m_now; }
	motion::SetNetQuiet(static_cast<float>(m_now - m_lastRecvAt));
	m_mover.SetSpeedPercent(m_baseSpeedPercent + (m_hasteUntil > m_now ? MoveRule::kHasteBonusPercent : 0u));
	UpdateGuide();
	// 이동은 실제 경과 시간(히트스톱만 반영)으로 — 프레임 시간 상한(0.05초)에 묶이면 FPS가 낮을 때 화면이 논리를 못 따라간다
	m_mover.Update(m_now, netDt);
	SyncMover();
	if (!m_opts.traceMove.empty())
	{
		if (!m_traceFile)
		{
			m_traceFile = std::fopen(m_opts.traceMove.c_str(), "w");
			if (m_traceFile) std::fprintf(m_traceFile, "t,lx,ly,vx,vy,speed,pending,keyx,keyy,rx,ry,camx,camy,zoom,face,rface,heading,sector,turn,dev,guided,rlx,rly,aimx,aimy,lineox,lineoy,lineux,lineuy\n");
		}
		if (m_traceFile)
		{
			const Vector2 l = m_mover.Logical(), v = m_mover.Visual();
			// 다른 플레이어 하나(원격 보간 확인용)
			Vector2 r{ -1, -1 }, rl{ -1, -1 };
			int rface = -1;
			for (const auto& [oid, o] : m_ents)
				if (oid != m_selfId && o.kind == EntityKind::Player)
				{
					// 방향 그림은 몸(Body)의 값 — 그려지는 것. e.faceRight는 걸음 알림이 온 프레임에 잠깐 걸음 방향으로 덮인다(서 있을 때만 몸이 받아들임)
					r = o.pos;
					rl = o.logPos;
					rface = (o.body.faceRight ? 1 : 0) | (o.body.faceBack ? 2 : 0);
					break;
				}
			int face = -1, sector = -1;
			float heading = 0.f, turn = 1.f;
			if (const Entity* me = Self())
			{
				face = (me->body.faceRight ? 1 : 0) | (me->body.faceBack ? 2 : 0);
				sector = me->body.sector;
				heading = me->body.heading;
				turn = me->body.turn;
			}
			const Vector2 cam = m_cam.Target();
			// 조준(커서 아래 바닥) · 곧은 선(RunLine: 걸음 계획과 화면 안내선이 함께 쓰는 선)
			std::fprintf(m_traceFile, "%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%d,%d,%d,%.4f,%.4f,%.3f,%.3f,%.4f,%d,%d,%.4f,%d,%.3f,%.4f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.5f,%.5f\n", m_now, l.x, l.y, v.x, v.y, m_mover.Speed(), m_mover.PendingCount(),
				m_scriptKeyX, m_scriptKeyY, r.x, r.y, cam.x, cam.y, m_cam.Zoom(), face, rface, heading, sector, turn, m_mover.Deviation(), m_mover.Guided() ? 1 : 0, rl.x, rl.y,
				m_aim.world.x, m_aim.world.y, m_lineValid ? m_lineO.x : -1.f, m_lineValid ? m_lineO.y : -1.f, m_lineU.x, m_lineU.y);
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
			motion::Animate(e, prev, netDt, m_mover.NominalSpeed());
		}
		else
		{
			motion::UpdateRemote(e, netDt, netDt);
			// 넉백 미끄러짐 · 띄워짐 · 넘어짐: 발을 떼지 않는다(달리는 다리로 밀려나지 않게)
			if (m_now < e.slideUntil || e.airT >= 0.f || e.downT >= 0.f)
				e.moving = 0.f;
			static const bool debugRemote = std::getenv("HARVEN_REMOTE_DEBUG") != nullptr;   // 원격 재생 진단(stderr, 프레임마다)
			if (e.kind == EntityKind::Player && debugRemote)
			{
				const motion::Track& tr = e.track;
				std::fprintf(stderr, "[track] t=%.4f qn=%d tt=%.3f dur=%.3f wait=%.3f extra=%.3f merge=%.3f dryT=%.3f margin=%.3f late=%.3f L(%.3f,%.3f) to(%.0f,%.0f) V(%.3f,%.3f)\n",
					m_now, tr.qn, tr.t, tr.dur, tr.wait, tr.extra, tr.merge, tr.dryT, tr.margin, tr.lateMax, tr.logical.x, tr.logical.y, tr.to.x, tr.to.y, e.pos.x, e.pos.y);
			}
		}

		// 발이 땅에 닿는 순간(걸음 위상 = 이동 거리 ÷ 보폭): 바닥 종류별 발소리 · 먼지
		if (e.kind != EntityKind::Item && e.body.footfall && !e.dead)
		{
			const int ground = GroundAt(e.pos);
			if (e.row == nullptr || e.row->archetype != MonsterArchetype::eGhost)
				fx::Fx().Footfall(TileToIso(e.pos), ground, (id == m_selfId ? 1.f : 0.5f) * (1.f + e.run));
			static constexpr sfx::Id kStep[5] = { sfx::Id::StepGrass, sfx::Id::StepStone, sfx::Id::StepWood, sfx::Id::StepSnow, sfx::Id::StepSand };
			if (id == m_selfId)
				sfx::Play(kStep[ground], 0.5f + 0.15f * e.run, 0.12f);
			else if (e.kind == EntityKind::Player && util::Len(util::Sub(e.pos, m_mover.Visual())) < 7.f)
				PlayAt(static_cast<int>(kStep[ground]), e.pos, 0.22f, 0.12f);
		}
		if (id == m_selfId && m_hasteUntil > m_now && e.moving > 0.2f)
			fx::Fx().HasteTrail(TileToIso(e.pos));
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
	if (m_tourCam.on)
	{
		m_cam.Follow(SurfaceIso(m_tourCam.tile, 0.f));
		m_cam.SetLead(Vector2{ 0, 0 });
		m_cam.SetCursorBias(Vector2{ 0, 0 });
		m_cam.SetZoomFree(m_tourCam.zoom);
	}
	else if (Entity* me = Self())
	{
		// 도착 연출: 마을 밖(남문 아래)에서 남문 칸으로 들어서면 3.4초 동안 카메라가 샛별 탑 쪽으로 55% 옮겨 가고 조금 넓게 본다.
		// 쿼터뷰에서 북쪽은 화면 오른쪽 위라, 남문에서 곧장 올려다보면 광장 · 탑이 화면 밖이다(LEVEL_DENICA 6장).
		{
			const bool inTown = WorldData::IsInTown(m_predX, m_predY);
			const bool atGate = inTown && std::abs(m_predX - static_cast<int>(TownMap::kSouthGate.x)) <= 3
				&& m_predY >= WorldData::kTownY0 + WorldData::kTownH - 4;
			if (atGate && m_arrival.wasOutside && m_now - m_arrival.at > 20.0)
			{
				m_arrival.at = m_now;
				m_arrival.zoomFrom = m_cam.ZoomGoal();
			}
			m_arrival.wasOutside = !inTown;
		}
		Vector2 follow = SurfaceIso(me->pos, 0.6f);
		const float at = static_cast<float>(m_now - m_arrival.at);
		if (at >= 0.f && at < 3.4f)
		{
			auto smooth = [](float a, float b, float x) { const float t = util::Clamp01((x - a) / (b - a)); return t * t * (3.f - 2.f * t); };
			const float w = at < 2.4f ? smooth(0.f, 0.7f, at) : 1.f - smooth(2.4f, 3.4f, at);
			for (const auto& b : WorldData::kBuildings)
				if (b.kind == WorldData::BuildingKind::eStarTower)
				{
					const Vector2 tower = gfx::IsoPx((b.x0 + b.x1) * 0.5f, (b.y0 + b.y1) * 0.5f, 3.2f + b.level * hgt::kLevelZ);
					follow = util::LerpV(follow, tower, 0.55f * w);
					break;
				}
			m_cam.SetZoomFree(util::Lerp(m_arrival.zoomFrom, std::min(m_arrival.zoomFrom, 1.05f), w));
			if (at > 3.3f) m_cam.SetZoomFree(m_arrival.zoomFrom);
		}
		m_cam.Follow(follow);
		// 진행 방향으로 앞서기: 스프링이 늦는 만큼(2τ·속도) + 달릴 때 약 1칸 더 — 가는 쪽이 더 보인다
		const Vector2 v = m_mover.Velocity();
		const Vector2 isoV{ (v.x - v.y) * gfx::kHalfW, (v.x + v.y) * gfx::kHalfH };
		const float len = std::sqrt(isoV.x * isoV.x + isoV.y * isoV.y);
		Vector2 lead{ 0, 0 };
		if (len > 1.f)
			lead = util::Add(util::Mul(isoV, 2.f * GameCamera::kTau), util::Mul(util::Norm(isoV), 38.f * util::Clamp01(len / 140.f)));
		m_cam.SetLead(lead);
		// 커서 쪽으로 조금(최대 약 1.5칸) — 창 · HUD 위에 있으면 치우치지 않는다
		Vector2 bias{ 0, 0 };
		if (!ui::MouseOverUi() && !m_menuOpen && !m_keysOpen && m_aim.valid)
		{
			const Vector2 toCursor = util::Sub(TileToIso(m_aim.world, 0.f), TileToIso(me->pos, 0.f));
			bias = util::Mul(toCursor, 0.12f);
			const float bl = util::Len(bias);
			if (bl > 52.f) bias = util::Mul(bias, 52.f / bl);
		}
		m_cam.SetCursorBias(bias);
		if (m_mover.Snapped())
		{
			m_cam.SnapTo(SurfaceIso(me->pos, 0.6f));
			m_lineValid = false;
		}
	}
	if (!ui::TextFocused() && !ui::MouseOverUi())
	{
		const float wheel = GetMouseWheelMove();
		if (wheel != 0.f && !m_tourCam.on) m_cam.AdjustZoom(wheel * 0.12f);
	}
	m_cam.Update(dt);

	// ── 이펙트/분위기 ──
	fx::Fx().Update(dt);
	skillfx::Update(sdt);
	vfx::Update(sdt);
	trail::Update(sdt);
	flashlight::Update(sdt);
	post::Update(dt, sdt);
	const auto* region = WorldData::FindRegion(Region());
	const WorldData::Biome biome = region ? region->biome : WorldData::Biome::eMeadow;
	fx::Fx().Ambient(biome, m_cam.Target(), m_screenW / m_cam.Zoom(), m_screenH / m_cam.Zoom(), dt, m_light.Night(), InTown());
	// 소리 구역: 듣는 자리는 내 캐릭터(투어 중에는 카메라가 보는 곳)
	{
		const Vector2 ear = m_tourCam.on ? m_tourCam.tile : Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
		amb::Update(dt, ear, m_light.Night(), m_light.Hour(), wind::Strength(), true);
	}
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

// SubmitChat(채팅 보내기)은 game/GameChat.cpp(Phase 3-B)

// ─────────────────────────────────────────────────────────────────────────────
// 연출 설정 → 후처리 · 연출 세기 · 카메라(매 프레임 — 값 몇 개라 싸다)
void Game::ApplyFxOptions()
{
	const kb::Options& o = kb::Opt();
	const int q = m_opts.quality >= 0 ? m_opts.quality : o.gfxQuality;
	const bool photo = m_opts.photoSafe >= 0 ? m_opts.photoSafe != 0 : o.photoSafe;
	const int other = m_opts.otherFx >= 0 ? m_opts.otherFx : o.otherFx;
	post::SetQuality(q <= 0 ? post::Quality::Low : (q == 1 ? post::Quality::Medium : post::Quality::High));
	post::SetPhotoSafe(photo);
	fxscale::SetQuality(q <= 0 ? fxscale::Quality::Low : (q == 1 ? fxscale::Quality::Medium : fxscale::Quality::High));
	fxscale::SetPhotoSafe(photo);
	fxscale::SetOther(other / 100.f);
	fxscale::SetShake(o.shake ? 1.f : 0.f);
	m_cam.SetShakeScale(fxscale::Shake());
}

// ─────────────────────────────────────────────────────────────────────────────
// 셰이더 효과 목록(--script vfx, Phase 2-A 확인용) — 모든 종류를 격자로 한꺼번에 띄운다
namespace
{
	void SpawnVfxSheet(Vector2 c)
	{
		using vfx::Kind;
		struct Row { Kind k; vfx::Ramp r; float w, h; const char* name; };
		const Row rows[] = {
			{ Kind::Fire, vfx::FireRamp(), 90, 150, "Fire" }, { Kind::Explosion, vfx::FireRamp(), 170, 170, "Explosion" },
			{ Kind::Smoke, vfx::SmokeRamp(), 150, 150, "Smoke" }, { Kind::IceBurst, vfx::FrostRamp(), 170, 170, "IceBurst" },
			{ Kind::InkBurst, vfx::InkRamp(), 160, 160, "InkBurst" }, { Kind::Dust, vfx::DustRamp(), 190, 110, "Dust" },
			{ Kind::Pillar, vfx::GoldRamp(), 90, 230, "Pillar" }, { Kind::Vortex, vfx::TealRamp(), 170, 170, "Vortex" },
			{ Kind::Lightning, vfx::BoltRamp(), 80, 260, "Lightning" }, { Kind::Slash, vfx::MoonRamp(), 170, 170, "Slash" },
			{ Kind::Flare, vfx::GoldRamp(), 170, 170, "Flare" }, { Kind::Beam, vfx::TealRamp(), 200, 30, "Beam" },
			{ Kind::Glyph, vfx::TealRamp(), 110, 110, "Glyph" },
			{ Kind::Rune, vfx::TealRamp(), 210, 0, "Rune" }, { Kind::Seal, vfx::GoldRamp(), 210, 0, "Seal" },
			{ Kind::Crack, vfx::FireRamp(), 210, 0, "Crack" }, { Kind::Scorch, vfx::SmokeRamp(), 210, 0, "Scorch" },
			{ Kind::Frost, vfx::FrostRamp(), 210, 0, "Frost" }, { Kind::InkStain, vfx::InkRamp(), 210, 0, "InkStain" },
			{ Kind::GroundRing, vfx::GoldRamp(), 210, 0, "GroundRing" },
		};
		constexpr int kCols = 5;
		int i = 0;
		for (const Row& r : rows)
		{
			const int col = i % kCols, row = i / kCols;
			const Vector2 at{ c.x + (col - 2) * 240.f, c.y + (row - 1.5f) * 175.f + 40.f };
			++i;
			vfx::Spec s = vfx::Make(r.k, at, 0.f, r.w, 1.6f, r.r);
			s.h = r.h > 0 ? r.h : r.w;
			const bool bottom = r.k == Kind::Fire || r.k == Kind::Pillar || r.k == Kind::Lightning || r.k == Kind::Dust;
			const bool ground = r.k >= Kind::Rune;
			s.z = ground || bottom ? 0.f : s.h * 0.5f;
			if (r.k == Kind::Beam)
			{
				vfx::BeamBetween(Vector2{ at.x - 100.f, at.y }, 40.f, Vector2{ at.x + 100.f, at.y }, 60.f, 30.f, 1.6f, s);
			}
			else vfx::Spawn(s);
			fx::Fx().FloatText(at, -18.f, r.name, Color{ 240, 236, 226, 255 }, 15.f, 1.9f, ui::Face::Latin);
		}
		// 궤적 리본 두 개 · 순간 조명 · 충격파
		trail::ArcSpec a;
		a.iso = Vector2{ c.x + 240.f * 2.f, c.y + 2.5f * 175.f + 20.f };
		a.a0 = -2.4f; a.a1 = 1.2f;
		trail::Arc(a);
		trail::ArcSpec b = a;
		b.iso.x -= 240.f;
		b.tilt = 1.3f; b.a0 = -2.8f; b.a1 = 0.6f; b.edge = Color{ 230, 255, 250, 255 }; b.body = Color{ 111, 181, 172, 255 };
		b.delay = 0.3f;
		trail::Arc(b);
		flashlight::Add(c, 180.f, Color{ 255, 200, 120, 255 }, 1.f, 0.9f);
		post::Shockwave(c, 20.f, 320.f, 26.f, 1.f, 0.7f);
	}
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
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGateOut.x), static_cast<int>(TownMap::kSouthGateOut.y) });
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
	else if (s == "vfx")
	{
		// 마을 밖 빈 들판으로 나가(건물에 가리지 않게) 2.4초마다 모든 종류를 격자로
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGateOut.x), static_cast<int>(TownMap::kSouthGateOut.y) });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
		{
			m_scriptStep = 2;
			m_scriptT = 99.f;
			m_cam.SetZoom(0.9f);
		}
		if (m_scriptStep == 2 && m_scriptT > 2.4f)
		{
			m_scriptT = 0.f;
			if (Entity* me = Self()) SpawnVfxSheet(TileToIso(me->pos));
		}
	}
	else if (s == "skill" || s == "combat" || s == "loot")
	{
		// 마을 밖으로 나가 가까운 몬스터에게 직업 기술을 돌려 가며 쓴다(연계 순서대로).
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGateOut.x), static_cast<int>(TownMap::kSouthGateOut.y) });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
			m_scriptStep = 2;
		else if (m_scriptStep == 2)
		{
			// 누르고 있는 동안 걷던 목표가 남아 있으면 서버가 모으기를 끊는다(걸으면 끊김) — 멈춰 세운다
			if (m_scriptHolding && m_goal.kind == GoalKind::Move) ClearGoal();
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
				// 직업별 연계 순서(기술 id): 기사 방패 강타→단죄→회전 베기→철벽→돌진 /
				// 각인술사 서리 각인→낙성→번개→화염 고리 / 사냥꾼 독 칼날→그늘 걸음→후방 습격→그믐 베기
				// (차지 · 홀딩 · 캐스팅처럼 누르고 있어야 하는 새 기술은 skill 스크립트가 다룬다)
				static const uint16_t order[3][5] = { { 2, 6, 3, 5, 4 }, { 8, 12, 11, 9, 0 }, { 15, 14, 17, 18, 0 } };
				const int cls = static_cast<int>(m_classId);
				const int d = Cheb(m_predX, m_predY, t->tx, t->ty);
				for (int k = 0; k < 5; ++k)
				{
					const uint16_t id = order[cls][k];
					if (id == 0) continue;
					const SkillRow* row = SkillTable::Find(id);
					const SkillDef* def = SkillDefTable::Find(id);
					if (!row || !def || m_level < row->unlockLevel || m_now < m_skillReady[CdIndex(id)] || m_mp < static_cast<int>(row->mp)) continue;
					if (def->kind == SkillKind::Hold || def->kind == SkillKind::Charge || def->kind == SkillKind::Toggle) continue;
					if (row->target == SkillTarget::Enemy && d > row->range) continue;
					if ((row->target == SkillTarget::Self || row->target == SkillTarget::Direction) && d > std::max<int>(1, row->radius)) continue;
					SetScriptAim(t->tx, t->ty, t->id);   // 사람이 커서를 대상에 올린 것처럼(지점 기술은 그 칸으로)
					if (TryCastSkill(id, true, row->target == SkillTarget::Enemy ? t->id : ObjID{})) break;
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
		const int bx = static_cast<int>(TownMap::kPlazaRow.x), by = static_cast<int>(TownMap::kPlazaRow.y) + 1;   // 광장 남쪽 여섯 칸(anchors.txt plaza_row)
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
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kPlazaRow.x) + static_cast<int>(h % 6u), static_cast<int>(TownMap::kPlazaRow.y) + static_cast<int>((h / 6u) % 2u) * 2 });
			m_scriptStep = 1;
		}
	}
	else if (s == "plaza")
	{
		// 분수 앞에 서서 마을 사람들이 돌아다니는 것을 본다
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kPlaza.x), static_cast<int>(TownMap::kPlaza.y) });
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
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kPvpField.x) + (attacker ? 1 : 0), static_cast<int>(TownMap::kPvpField.y) });
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
		//   runline[:dx,dy[,초[,x,y]]]  화면 방향(dx,dy)으로 커서를 두고 좌클릭을 누른 채 달린다(기본 오른쪽 6초).
		//                               x,y(마을 기준 칸)를 주면 먼저 그 칸으로 걸어가 선 뒤 시작한다(부활 칸이 무작위라서)
		//   runcircle[:초]              분수 둘레를 도는 점을 커서로 앞서 가리키며 누른 채 달린다(기본 30초)
		//   follow:이름                 그 플레이어 위에 커서를 두고 누른 채 따라간다
		Entity* me = Self();
		if (!me) return;
		const Vector2 meScr = GetWorldToScreen2D(SurfaceIso(me->pos, 0.f), m_cam2d);
		float dx = 1.f, dy = 0.f, secs = 6.f, sx = -1.f, sy = -1.f;
		if (s.rfind("runline:", 0) == 0) std::sscanf(s.c_str() + 8, "%f,%f,%f,%f,%f", &dx, &dy, &secs, &sx, &sy);
		float start = 1.5f;
		if (sx >= 0.f)
		{
			const int gx = WorldData::kTownX0 + static_cast<int>(sx), gy = WorldData::kTownY0 + static_cast<int>(sy);
			if (m_scriptStep == 0 && m_scriptT > 0.3f)
			{
				SetGoal(Goal{ GoalKind::Move, ObjID{}, gx, gy });
				m_scriptStep = 1;
			}
			else if (m_scriptStep == 1 && m_goal.kind == GoalKind::None && !m_mover.IsWalking(m_now))
			{
				m_scriptStep = 2;
				m_scriptMark = m_scriptT + 0.8f;   // 선 뒤 잠깐 숨을 고르고
			}
			start = m_scriptStep == 2 ? m_scriptMark : 1e9f;
		}
		if (m_scriptT < start) { in::SetScriptMouse(Vector2{ meScr.x, meScr.y - 200.f }, false); return; }
		if (s.rfind("runline", 0) == 0)
		{
			const float l = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
			const bool hold = m_scriptT < start + secs;
			in::SetScriptMouse(Vector2{ meScr.x + dx / l * 240.f, meScr.y - 30.f + dy / l * 240.f }, hold);
		}
		else if (s.rfind("runcircle", 0) == 0)
		{
			float secs = 30.f;
			if (s.size() > 10) secs = static_cast<float>(std::atof(s.c_str() + 10));
			const Vector2 c{ TownMap::kFountain.x, TownMap::kFountain.y };   // 분수 가운데(anchors.txt fountain)
			const float R = 5.f;
			const float a = std::atan2(me->pos.y - c.y, me->pos.x - c.x) + 0.9f;          // 도는 쪽으로 약 50° 앞
			const Vector2 aim{ c.x + std::cos(a) * R, c.y + std::sin(a) * R };
			const Vector2 scr = GetWorldToScreen2D(SurfaceIso(aim, 0.f), m_cam2d);
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
				const Vector2 scr = GetWorldToScreen2D(SurfaceIso(t->pos, 0.f), m_cam2d);
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
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGateOut.x), static_cast<int>(TownMap::kSouthGateOut.y) });
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
			const Vector2 scr = GetWorldToScreen2D(util::Add(SurfaceIso(best->pos, 0.f), Vector2{ 0.f, -(best->row ? actors::MonsterHeight(*best->row) : 40.f) * 0.45f }), m_cam2d);
			const bool rmb = std::fmod(m_scriptT, 1.4f) < 0.5f;   // 우클릭을 눌렀다 뗐다
			in::SetScriptMouse(scr, false, rmb);
			// 누르고 있는 기술(차지 · 홀딩): 정한 시간이 되면 뗀다
			if (m_scriptHolding && m_now >= m_scriptHoldUntil)
			{
				kb::ScriptHold(static_cast<kb::Act>(m_scriptHoldAct), false);
				m_scriptHolding = false;
			}
			if (!m_scriptHolding && m_now >= m_scriptNextCast && m_now >= m_actionLockUntil)
			{
				for (int k = 0; k < 11; ++k)
				{
					const int hot = 1 + (m_scriptCastIdx + k) % 11;   // Q W E R A S D F V (Space) Z — 이동기는 빼고 도는 차례
					const SkillRow* row = HotSkill(hot);
					const SkillDef* def = row ? SkillDefTable::Find(row->id) : nullptr;
					if (!row || !def || hot == kHotMobility || m_level < row->unlockLevel || m_now < m_skillReady[CdIndex(row->id)] || m_mp < static_cast<int>(row->mp)) continue;
					if (def->kind == SkillKind::Identity && m_identity < 1000) continue;
					if (def->kind == SkillKind::Toggle && m_toggleSkill == row->id) continue;
					const kb::Act act = kb::HotAct(hot);
					kb::ScriptPress(act);
					if (def->kind == SkillKind::Charge || def->kind == SkillKind::Hold)
					{
						// 차지는 완벽 구간 가운데까지, 홀딩은 끝의 80%까지 누르고 있는다
						const double hold = def->kind == SkillKind::Charge ? (def->stageMs * def->stages + def->perfectMs * 0.5) / 1000.0 : def->holdMaxMs * 0.8 / 1000.0;
						kb::ScriptHold(act, true);
						m_scriptHoldAct = static_cast<int>(act);
						m_scriptHoldUntil = m_now + hold;
						m_scriptHolding = true;
					}
					m_scriptCastIdx = hot;
					break;
				}
				m_scriptNextCast = m_now + 1.6;
			}
			if (m_hp * 2 < m_maxhp) kb::ScriptPress(kb::Act::Potion);
		}
	}
	else if (s == "skills" || s.rfind("skills:", 0) == 0 || s == "training")
	{
		// training · skills:<직업>(Phase 2-E): 들판 대신 마을 동쪽 훈련장의 허수아비에게 같은 순서로 — 기술별 녹화 · 초당 피해 계측.
		// 기술이 바뀔 때마다 [skillseg](녹화 시작 기준 초)를 찍는다 → tools/record/split_skills.py가 기술별 GIF로 자른다
		const bool yard = s == "training" || s.rfind("skills:", 0) == 0;
		// 기술 구조 V3 전수 시험: 들판의 가까운 적에게 C(콤보) → Q W E R A S D F → Space → V → Z를 차례로.
		// 차지는 완벽 구간 가운데까지 · 홀딩은 80% · 체인은 끝 단까지 · 토글은 켰다 끄기. HARVEN_SKILL_TRACE=1로 결과를 모은다.
		// Phase 2-D: HARVEN_GROW="ab"(예: "12")면 먼저 마을에서 여덟 칸을 레벨 7로 올리고 갈래 Ⅰ=a · Ⅱ=b를 고른 뒤 시작한다(갈래 전수 시험)
		static const char* grow = std::getenv("HARVEN_GROW");
		if (grow && grow[0] >= '0' && grow[0] <= '2' && grow[1] >= '0' && grow[1] <= '2' && !m_scriptGrowDone)
		{
			if (m_scriptT > 1.0f && ScriptGrowStep(7, static_cast<uint8_t>(grow[0] - '0'), static_cast<uint8_t>(grow[1] - '0')))
			{
				m_scriptGrowDone = true;
				m_scriptT = 0.5f;   // 아래 순서(마을 밖으로)를 처음부터
				m_scriptSeq.clear();
			}
			return;
		}
		if (m_scriptSeq.empty())
		{
			const SkillDef* basic = SkillDefTable::Find(SkillDefTable::Default(m_classId, SkillKey::C));
			const int combo = basic ? basic->stages : 3;
			// 콤보는 다음 타를 받을 수 있는 순간(기본 공격 간격)마다 — 너무 이르면 누름이 버려져 콤보가 끊긴다(wait −1 = 누를 때 간격을 잰다)
			for (int i = 0; i < combo; ++i) m_scriptSeq.push_back(ScriptCast{ 0, 0.f, -1.f });
			for (int hot = 1; hot < kHotSlots; ++hot)
			{
				const SkillRow* row = HotSkill(hot);
				const SkillDef* def = row ? MyDef(row->id) : nullptr;   // 갈래(재빠름)를 입힌 시각
				if (!def) continue;
				const float lock = def->lockMs / 1000.f, cast = def->castMs / 1000.f;
				switch (def->kind)
				{
				case SkillKind::Charge: m_scriptSeq.push_back(ScriptCast{ hot, (def->stageMs * def->stages + def->perfectMs * 0.5f) / 1000.f, lock + 0.7f }); break;
				case SkillKind::Hold:   m_scriptSeq.push_back(ScriptCast{ hot, def->holdMaxMs * 0.8f / 1000.f, 0.8f }); break;
				case SkillKind::Chain:
					for (int k = 0; k < def->stages; ++k) m_scriptSeq.push_back(ScriptCast{ hot, 0.f, k + 1 < def->stages ? 0.7f : lock + 0.6f });
					break;
				case SkillKind::Toggle:
					m_scriptSeq.push_back(ScriptCast{ hot, 0.f, 1.4f });
					m_scriptSeq.push_back(ScriptCast{ hot, 0.f, 0.6f });
					break;
				default: m_scriptSeq.push_back(ScriptCast{ hot, 0.f, std::max(0.9f, lock + cast + 0.5f) }); break;
				}
				// 훈련장(기술별 녹화): 여운이 다 보이게 기술 사이를 0.9초 더 띄운다
				if (yard && !m_scriptSeq.empty()) m_scriptSeq.back().wait += def->kind == SkillKind::Awaken ? 1.6f : 0.9f;
			}
		}
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			// 훈련장: 가운데 허수아비 두 칸 앞(위쪽) — 들판: 남쪽 문 밖
			if (yard) SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kDummySpots[1].x, WorldData::kDummySpots[1].y - 2 });
			else SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kSouthGateOut.x), static_cast<int>(TownMap::kSouthGateOut.y) });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
			m_scriptStep = 2;
		else if (m_scriptStep == 2)
		{
			// 기본 공격 누름이 남긴 자동 공격 목표는 지운다 — 순서표의 누름만 나가게(허수아비는 죽지 않아 자동 공격이 끝나지 않는다)
			if (m_goal.kind == GoalKind::Attack) ClearGoal();
			Entity* best = nullptr;
			int bestD = 99;
			for (auto& [id, e] : m_ents)
			{
				if (e.kind != EntityKind::Monster || e.dead || e.removing) continue;
				const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
				if (d < bestD) { bestD = d; best = &e; }
			}
			if (m_scriptHolding && m_now >= m_scriptHoldUntil)
			{
				kb::ScriptHold(static_cast<kb::Act>(m_scriptHoldAct), false);
				m_scriptHolding = false;
			}
			// 근접 기술도 닿게 2칸 안까지 다가간다(누르고 있는 동안은 서서)
			if (!best || bestD > 2)
			{
				if (!m_scriptHolding)
				{
					in::ClearScriptMouse();
					if (m_goal.kind == GoalKind::None || (best && bestD < 12 && m_now - m_scriptApproachAt > 0.8))
					{
						m_scriptApproachAt = m_now;
						SetGoal(Goal{ GoalKind::Move, ObjID{}, best && bestD < 12 ? best->tx : m_predX + util::RandI(-6, 6), best && bestD < 12 ? best->ty : m_predY + util::RandI(3, 8) });
					}
				}
				if (!best || bestD > 6 || !m_scriptHolding) return;
			}
			if (m_goal.kind == GoalKind::Move) ClearGoal();
			const Vector2 scr = GetWorldToScreen2D(util::Add(SurfaceIso(best->pos, 0.f), Vector2{ 0.f, -(best->row ? actors::MonsterHeight(*best->row) : 40.f) * 0.45f }), m_cam2d);
			in::SetScriptMouse(scr, false, false);
			if (!m_scriptHolding && m_now >= m_scriptNextCast && m_now >= m_actionLockUntil && !m_scriptSeq.empty())
			{
				const ScriptCast sc = m_scriptSeq[static_cast<size_t>(m_scriptSeqIdx % static_cast<int>(m_scriptSeq.size()))];
				if (++m_scriptSeqIdx % static_cast<int>(m_scriptSeq.size()) == 0) ++m_scriptSeqLoops;
				const SkillRow* row = HotSkill(sc.hot);
				const SkillDef* def = row ? SkillDefTable::Find(row->id) : nullptr;
				const bool gaugeOk = !def || def->kind != SkillKind::Identity || m_identity >= 1000;
				if (row && m_level >= row->unlockLevel && gaugeOk)
				{
					const kb::Act act = kb::HotAct(sc.hot);
					if (row->id != m_scriptSegSkill)
					{
						m_scriptSegSkill = row->id;
						const double r0 = rec::StartAt();
						std::printf("[skillseg] t=%.3f rec=%.3f slot=%d id=%u name=%s\n", m_now, r0 >= 0.0 ? m_now - r0 : -1.0, sc.hot, row->id, row->name);
						std::fflush(stdout);
					}
					kb::ScriptPress(act);
					if (sc.hold > 0.f)
					{
						kb::ScriptHold(act, true);
						m_scriptHoldAct = static_cast<int>(act);
						m_scriptHoldUntil = m_now + sc.hold;
						m_scriptHolding = true;
					}
				}
				const float wait = sc.wait >= 0.f ? sc.wait : std::max(0.32f, SkillTable::BasicIntervalMs(m_classId, m_stats.attackSpeed) / 1000.f + 0.06f);
				m_scriptNextCast = m_now + sc.hold + wait;
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
		const Vector2 meScr = GetWorldToScreen2D(SurfaceIso(me->pos, 0.f), m_cam2d);
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
		const Vector2 meScr = GetWorldToScreen2D(SurfaceIso(me->pos, 0.f), m_cam2d);
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
	else if (s == "skillgrow" || s.rfind("skillgrow:", 0) == 0)
	{
		// Phase 2-D 기술 창: K를 열고 여덟 칸을 레벨 7까지 올리며 갈래를 고른다(skillgrow:ab — 기본 12). 끝나면 레벨이 가장 높은 기술을 골라 둔다
		const uint8_t a = s.size() >= 12 ? static_cast<uint8_t>(s[10] - '0') : 1;
		const uint8_t b = s.size() >= 12 ? static_cast<uint8_t>(s[11] - '0') : 2;
		if (m_scriptStep == 0 && m_scriptT > 1.0f) { kb::ScriptPress(kb::Act::SkillWindow); m_scriptStep = 1; }
		else if (m_scriptStep == 1 && m_scriptT > 2.0f && ScriptGrowStep(7, a, b))
		{
			m_growSel = SkillDefTable::Default(m_classId, SkillKey::W);
			m_scriptStep = 2;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 2 && m_scriptT > 2.5f)
		{
			// 고르지 않은 갈래 Ⅱ 칸에 마우스를 올린다 — 시연이 "미리 보기"로 바뀌는지(기술 창 시연 확인)
			const Rectangle& cr = m_growCardRect[1][b == 1 ? 1 : 0];
			if (cr.width > 0.f) in::SetScriptMouse(Vector2{ cr.x + cr.width * 0.5f, cr.y + cr.height * 0.5f }, false);
			m_scriptStep = 3;
		}
		else if (m_scriptStep == 3 && std::getenv("HARVEN_GROW_TOUR"))
		{
			// 갈래 네 칸을 3초씩 차례로 가리킨다(갈래별 시연 비교 녹화) — 가리킨 칸이 고른 칸이면 지금 모습
			const int idx = static_cast<int>((m_scriptT - 2.5f) / 3.f) % 4;
			const Rectangle& cr = m_growCardRect[idx / 2][idx % 2];
			if (cr.width > 0.f) in::SetScriptMouse(Vector2{ cr.x + cr.width * 0.5f, cr.y + cr.height * 0.5f }, false);
		}
	}
	else if (s == "awaken3")
	{
		// Phase 2 완료 기준 "3명 동시 각성기 + 몬스터 30마리 장면의 프레임 그래프" — 그리기 비용 측정.
		// 훈련장(기술을 쓸 수 있다)에서 가짜 몬스터 30 · 가짜 모험가 둘(각인술사 · 밤사냥꾼 — 클라에만 있는 개체)을 두고,
		// 내 각성기(서버 판정 그대로)와 둘의 각성기 알림(가짜 SKILL_EX_NFY — 남의 연출 50% 경로 그대로)을 같은 프레임에 시작한다.
		// 전 3초 · 각성 4초 · 뒤 2초의 프레임 CPU 시간 · 드로우콜 · 입자를 [bench]로 찍고, 절정(1.45초)에 F3 그래프를 켠 채 스크린샷
		Entity* me = Self();
		if (!me) return;
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kDummySpots[0].x, WorldData::kDummySpots[0].y - 2 });
			m_scriptStep = 1;
		}
		else if (m_scriptStep == 1 && (m_goal.kind == GoalKind::None || m_scriptT > 30.f))
		{
			static const uint16_t kSpecies[] = { 1, 2, 3, 4, 5, 7, 8, 11, 12, 14 };
			for (int i = 0; i < 30; ++i)
			{
				Entity e;
				e.id = ObjID(EnumCategory::eMonster, MONSTER_ID_START + 0x0FFFF000u + static_cast<uint32_t>(i));
				e.kind = EntityKind::Monster;
				e.species = kSpecies[i % 10];
				e.row = MonsterTable::Find(e.species);
				const float a = i * 2.f * util::kPi / 30.f, r = 2.4f + static_cast<float>(i % 3) * 1.1f;
				e.pos = e.logPos = Vector2{ me->pos.x + std::cos(a) * r, me->pos.y + std::sin(a) * r };
				e.tx = static_cast<int>(std::round(e.pos.x)); e.ty = static_cast<int>(std::round(e.pos.y));
				e.faceRight = std::cos(a) < 0.f;
				e.spawnT = 1.f; e.hp = e.maxhp = 1000; e.level = 20;
				e.nameHash = 991u + static_cast<uint32_t>(i) * 7u;
				motion::RemoteSnap(e, Vector2{ static_cast<float>(e.tx), static_cast<float>(e.ty) });   // 재생 시간표도 그 칸에(안 하면 (0,0)으로 끌려간다)
				m_ents[e.id] = e;
			}
			for (int k = 0; k < 2; ++k)
			{
				Entity e;
				e.id = ObjID(EnumCategory::eUser, 0x7FFFFF10u + static_cast<uint32_t>(k));
				e.kind = EntityKind::Player;
				e.classId = static_cast<uint8_t>(k + 1);
				e.pos = e.logPos = Vector2{ me->pos.x + (k == 0 ? -1.5f : 1.5f), me->pos.y - 1.5f };
				e.tx = static_cast<int>(std::round(e.pos.x)); e.ty = static_cast<int>(std::round(e.pos.y));
				e.spawnT = 1.f; e.hp = e.maxhp = 1000; e.level = 50;
				e.nameHash = 4242u + static_cast<uint32_t>(k);
				motion::RemoteSnap(e, Vector2{ static_cast<float>(e.tx), static_cast<float>(e.ty) });
				m_ents[e.id] = e;
			}
			m_debugOverlay = true;
			m_bench = Bench{};
			m_scriptStep = 2;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 2 && m_scriptT > 3.0f)
		{
			TryCastSkill(SkillDefTable::Default(m_classId, SkillKey::V));
			static const uint16_t kAwaken[2] = { 12, 18 };
			for (int k = 0; k < 2; ++k)
			{
				const ObjID fid(EnumCategory::eUser, 0x7FFFFF10u + static_cast<uint32_t>(k));
				const Entity* f = Find(fid);
				const SkillDef* d = SkillDefTable::Find(kAwaken[k]);
				if (!f || !d) continue;
				SKILL_EX_NFY_PACKET p{};
				p.casterId = fid; p.skillId = kAwaken[k]; p.phase = static_cast<uint8_t>(SkillExPhase::Start);
				p.fromX = static_cast<short>(f->tx); p.fromY = static_cast<short>(f->ty);
				p.aimX = static_cast<short>(me->tx + 2); p.aimY = static_cast<short>(me->ty + 1);
				p.castMs = d->castMs;
				HandleSkillEx(p);
			}
			m_opts.screenshotAfter = m_runTime + 1.45f;   // 절정(단죄 1.4초 · 그믐 베기 1.4초 · 낙성 1.6초)
			m_scriptStep = 3;
			m_scriptT = 0.f;
		}
		else if (m_scriptStep == 3 && m_scriptT > 6.0f)
		{
			const char* names[3] = { "전 3초", "각성 4초", "뒤 2초" };
			for (int k = 0; k < 3; ++k)
			{
				const Bench::Part& b = m_bench.part[k];
				if (b.frames == 0) continue;
				std::printf("[bench] %s frames=%d work avg=%.1fms max=%.1fms | draw avg=%d max=%d | particles max=%d | over33(work)=%d\n", names[k], b.frames,
					b.workSum / b.frames, b.workMax, b.drawSum / b.frames, b.drawMax, b.partMax, b.over33);
			}
			std::fflush(stdout);
			m_scriptStep = 4;
		}
		// 매 프레임 모으기(직전 프레임의 CPU 시간 · 드로우콜)
		if (m_scriptStep == 2 || m_scriptStep == 3)
		{
			const int k = m_scriptStep == 2 ? 0 : (m_scriptT < 4.f ? 1 : 2);
			Bench::Part& b = m_bench.part[k];
			const float w = perf::WorkMs(0);
			const int dc = perf::DrawCallsLast();
			const int pc = static_cast<int>(fx::Particles().Count());
			++b.frames; b.workSum += w; b.workMax = std::max(b.workMax, w); b.drawSum += std::max(0, dc); b.drawMax = std::max(b.drawMax, dc);
			b.partMax = std::max(b.partMax, pc); if (w > 33.f) ++b.over33;
		}
	}
	else if (s == "growcheck")
	{
		// Phase 2-D 서버 규칙 시험(레벨 3 캐릭터 = 점수 4): 초기화 → 올림 ×3 → 갈래 Ⅰ → 갈래 Ⅱ(TierLocked) → 올림(NoPoints) →
		// 내림(갈래 Ⅰ이 꺼짐) → 내림 ×2 → 내림(MinLevel) → 잠긴 기술(Locked) → 훈련장에서 기본 공격 직후(InCombat) → 5.5초 뒤(Ok) → 초기화(점수 4)
		struct Step { SkillGrowOp op; int key; uint8_t tier, choice; SkillGrowResult expect; };
		static const Step kSteps[] = {
			{ SkillGrowOp::ResetAll, -1, 0, 0, SkillGrowResult::Ok },
			{ SkillGrowOp::LevelUp, 1, 0, 0, SkillGrowResult::Ok }, { SkillGrowOp::LevelUp, 1, 0, 0, SkillGrowResult::Ok }, { SkillGrowOp::LevelUp, 1, 0, 0, SkillGrowResult::Ok },
			{ SkillGrowOp::Pick, 1, 0, 1, SkillGrowResult::Ok }, { SkillGrowOp::Pick, 1, 1, 1, SkillGrowResult::TierLocked },
			{ SkillGrowOp::LevelUp, 1, 0, 0, SkillGrowResult::NoPoints },
			{ SkillGrowOp::LevelDown, 1, 0, 0, SkillGrowResult::Ok }, { SkillGrowOp::LevelDown, 1, 0, 0, SkillGrowResult::Ok }, { SkillGrowOp::LevelDown, 1, 0, 0, SkillGrowResult::Ok },
			{ SkillGrowOp::LevelDown, 1, 0, 0, SkillGrowResult::MinLevel },
			{ SkillGrowOp::LevelUp, 99, 0, 0, SkillGrowResult::Locked },
			{ SkillGrowOp::LevelUp, 2, 0, 0, SkillGrowResult::InCombat },   // 훈련장에서 때린 직후
			{ SkillGrowOp::LevelUp, 2, 0, 0, SkillGrowResult::Ok },         // 5.5초 쉰 뒤
			{ SkillGrowOp::ResetAll, -1, 0, 0, SkillGrowResult::Ok },
		};
		constexpr int kN = static_cast<int>(sizeof(kSteps) / sizeof(kSteps[0]));
		auto skillFor = [&](int key) -> uint16_t
		{
			if (key < 0) return 0;
			if (key == 99)   // 아직 못 배운 자라는 기술
			{
				for (int k = static_cast<int>(SkillKey::Q); k <= static_cast<int>(SkillKey::F); ++k)
				{
					const uint16_t id = SkillDefTable::Default(m_classId, static_cast<SkillKey>(k));
					const SkillRow* row = SkillTable::Find(id);
					if (row && m_level < row->unlockLevel && SkillGrowth::Growable(id)) return id;
				}
				return 0;
			}
			return SkillDefTable::Default(m_classId, static_cast<SkillKey>(key));
		};
		const int i = m_scriptStep / 3, phase = m_scriptStep % 3;   // 단계마다 준비 · 보냄 · 결과
		if (i >= kN)
		{
			if (m_scriptStep == kN * 3)
			{
				std::printf("[growcheck] done pass=%d fail=%d points=%u/%u\n", m_checkPass, m_checkFail, m_skillPoints, m_skillPointsTotal);
				std::fflush(stdout);
				++m_scriptStep;
			}
			return;
		}
		const Step& st = kSteps[i];
		if (!m_skillTreeKnown || m_scriptT < 1.5f) return;
		if (phase == 0)
		{
			// InCombat 단계: 훈련장 허수아비 곁으로 가서 기본 공격 한 번 / 다음 단계: 5.5초 쉬기
			if (st.expect == SkillGrowResult::InCombat)
			{
				Entity* best = nullptr;
				int bestD = 99;
				for (auto& [id, e] : m_ents)
					if (e.kind == EntityKind::Monster && !e.dead && e.row && e.row->dummy)
						if (const int d = Cheb(m_predX, m_predY, e.tx, e.ty); d < bestD) { bestD = d; best = &e; }
				if (!best || bestD > 1)
				{
					if (m_goal.kind == GoalKind::None)
						SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kDummySpots[1].x, WorldData::kDummySpots[1].y - 1 });
					return;
				}
				if (m_goal.kind != GoalKind::None) ClearGoal();
				TryAttackTarget(*best);
				ClearGoal();
				m_scriptT = 1.5f;
				m_scriptStep = i * 3 + 1;
				return;
			}
			if (i > 0 && kSteps[i - 1].expect == SkillGrowResult::InCombat && m_scriptT < 7.0f) return;
			m_scriptStep = i * 3 + 1;
			return;
		}
		if (phase == 1)
		{
			if (st.expect == SkillGrowResult::InCombat && m_scriptT < 1.8f) return;   // 공격이 서버에 닿을 틈
			SendSkillGrow(st.op, skillFor(st.key), st.tier, st.choice);
			m_scriptStep = i * 3 + 2;
			m_scriptT = 1.5f;
			return;
		}
		if (m_growAckSeq != m_growSeq) { if (m_scriptT > 6.f) { std::printf("[growcheck] step %d no ack\n", i); ++m_checkFail; m_scriptStep = (i + 1) * 3; } return; }
		const bool ok = m_growAckResult == static_cast<uint8_t>(st.expect);
		// 내림으로 갈래 레벨 밑이 되면 그 갈래가 꺼져야 한다(두 번째 내림 = 레벨 3)
		bool extra = true;
		if (i == 7) extra = MyGrow(skillFor(1)).pick[0] == 0 && MyGrow(skillFor(1)).level == 3;
		if (i == kN - 1) extra = m_skillPoints == m_skillPointsTotal;
		std::printf("[growcheck] step %d op=%u id=%u expect=%u got=%u %s%s\n", i, static_cast<unsigned>(st.op), skillFor(st.key),
			static_cast<unsigned>(st.expect), m_growAckResult, ok && extra ? "PASS" : "FAIL", extra ? "" : " (state)");
		std::fflush(stdout);
		if (ok && extra) ++m_checkPass; else ++m_checkFail;
		m_scriptStep = (i + 1) * 3;
		m_scriptT = 1.5f;
	}
	else if (s == "behind")
	{
		// 잡화점 건물 바로 뒤(북쪽)에 서서 실루엣이 보이는지 확인(anchors.txt behind_store)
		if (m_scriptStep == 0 && m_scriptT > 1.0f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kBehindStore.x), static_cast<int>(TownMap::kBehindStore.y) });
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
		case 7: if (m_scriptT > 2.5f) { CloseNpcWindows(); SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kQuestField.x), static_cast<int>(TownMap::kQuestField.y) }); m_scriptStep = 8; m_scriptT = 0.f; } break;
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
	else if (!RunScriptUi(dt))   // Phase 3 시험(창 · 채팅 · HUD) — game/GameScriptUi.cpp
		RunScriptWorld(dt);      // Phase 4 시험(투어 · 지형) — game/GameScriptWorld.cpp
}
