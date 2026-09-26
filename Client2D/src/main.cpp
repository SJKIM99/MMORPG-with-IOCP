// ─────────────────────────────────────────────────────────────────────────────
// 하르벤 — MMORPG-with-IOCP 2D(아이소메트릭) 클라이언트
//
//   실행 옵션
//     --host 127.0.0.1 --port 4000   서버 주소(로그인 화면 기본값)
//     --name 이름                     자동 로그인
//     --size 1600x900                 창 크기
//     --mute                          소리 끄기
//     --gallery                       서버 없이 몬스터/NPC/아이템 도감 보기
//   테스트용
//     --screenshot out.png --after 8  지정 시간 뒤 스크린샷 저장 후 종료
//     --hour 22                       게임 시각 고정
//     --netsim latency=150,jitter=40,spike=2   나쁜 네트워크 흉내(한쪽 방향 ms · 패킷 100개당 재전송)
//     --lag 120 --jitter 60           (옛 이름) 인공 지연 · 지터
//     --record 폴더 --seconds 12 --fps 30 [--record-after 2 --record-scale 0.5 --fixed-step]
//                                     화면 녹화(ffmpeg가 있으면 capture.mp4, 없으면 프레임) — tools/record
//     --f3                            처음부터 개발 정보(F3) 표시
//     --sandbox-clock /dev/shm/harven_clock   샌드박스 공유 시계(서버 · 봇과 한 시계, tools/sandbox)
//     --class 0|1|2                   새 캐릭터 직업(0 서약기사 · 1 각인술사 · 2 밤사냥꾼)
//     --script hunt|town|inventory|map|help|enchant|chat|combat|charwin
// ─────────────────────────────────────────────────────────────────────────────
#include "raylib.h"
#include "rlgl.h"

#include "game/Game.h"
#include "render/Art.h"
#include "render/Assets.h"
#include "render/Icons.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Terrain.h"
#include "render/Lighting.h"
#include "render/Props.h"
#include "render/Sprites.h"
#include "render/EnvArt.h"
#include "render/FxArt.h"
#include "render/Post.h"
#include "fx/Vfx.h"
#include "fx/Trail.h"
#include "render/FlashLight.h"
#include "ui/UiArt.h"
#include "audio/Sfx.h"
#include "audio/Ambience.h"
#include "audio/Voice.h"
#include "platform/Ime.h"
#include "ui/Font.h"
#include "ui/Ui.h"
#include "core/Util.h"
#include "core/Perf.h"
#include "core/Recorder.h"
#include "core/Clock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	// 에셋 폴더(assets/fonts, assets/art) — 실행 파일 옆 → 작업 폴더 → 상위 폴더 순서로 찾는다.
	// probe = 그 폴더에 반드시 있는 파일 하나
	std::string FindAssetDir(const char* rel, const char* probe)
	{
		const std::string appDir = GetApplicationDirectory();
		const std::string candidates[] = { appDir + rel, std::string(rel), std::string("../") + rel, std::string("../../") + rel };
		for (const auto& c : candidates)
			if (FileExists((c + "/" + probe).c_str()))
				return c;
		return rel;
	}

	// ── 서버 없이 보는 도감(그림 확인용) ──
	// ─────────────────────────────────────────────────────────────────────────
	// 셰이더 효과 표(--vfx-sheet out): 종류(줄) × 나이(칸) — 들판 바닥 위에 실제 블룸까지 그려 쪽마다 PNG로
	void RunVfxSheet(const std::string& outBase, float hour)
	{
		Terrain terrain;
		terrain.Init();
		Lighting light;
		light.UpdateClock(hour);
		props::SetNight(light.Night());
		const float cx = TownMap::kPvpField.x, cy = TownMap::kPvpField.y;   // 남문 밖 먼 들판(anchors.txt pvp_field)
		constexpr float ages[6] = { 0.05f, 0.14f, 0.28f, 0.45f, 0.65f, 0.85f };
		constexpr float kLife = 1.f, kT = 0.9f;
		const int kinds = static_cast<int>(vfx::Kind::Count);
		const char* names[] = { "Fire", "Explosion", "Smoke", "IceBurst", "InkBurst", "Dust", "Pillar", "Vortex", "Lightning", "Slash", "Flare",
			"Beam", "Glyph", "Rune", "Seal", "Crack", "Scorch", "Frost", "InkStain", "GroundRing" };
		const vfx::Ramp ramps[] = { vfx::FireRamp(), vfx::FireRamp(), vfx::SmokeRamp(), vfx::FrostRamp(), vfx::InkRamp(), vfx::DustRamp(), vfx::GoldRamp(),
			vfx::TealRamp(), vfx::BoltRamp(), vfx::MoonRamp(), vfx::GoldRamp(), vfx::TealRamp(), vfx::TealRamp(), vfx::TealRamp(), vfx::GoldRamp(),
			vfx::FireRamp(), vfx::SmokeRamp(), vfx::FrostRamp(), vfx::InkRamp(), vfx::GoldRamp() };
		const int pages = (kinds + 4) / 5 + 1;   // 마지막 쪽 = 궤적 리본 · 순간 조명 · 충격파
		for (int page = 0; page < pages && !WindowShouldClose(); ++page)
		{
			vfx::Clear(); trail::Clear(); post::Clear(); flashlight::Clear();
			const int W = GetScreenWidth(), H = GetScreenHeight();
			Camera2D cam{};
			cam.offset = Vector2{ W * 0.5f, H * 0.5f };
			cam.target = gfx::IsoPx(cx, cy);
			cam.zoom = 1.f;
			const float colW = std::floor((W - 170.f) / 6.f), rowH = std::floor((H - 40.f) / 5.f);
			auto cellWorld = [&](int r, int c, float fy)
			{
				return GetScreenToWorld2D(Vector2{ 170.f + colW * (c + 0.5f), 34.f + rowH * r + rowH * fy }, cam);
			};
			const bool lastPage = page == pages - 1;
			struct StreakReq { Vector2 a, b; float age; };
			std::vector<StreakReq> streaks;
			for (int r = 0; r < 5; ++r)
			{
				const int k = page * 5 + r;
				if (!lastPage && k >= kinds) break;
				for (int c = 0; c < 6; ++c)
				{
					if (lastPage)
					{
						// 0: 수평 베기 리본 · 1: 세로 내려치기 리본 · 2: 따라가기 궤적 · 3: 순간 조명 · 4: 충격파(나이 = 칸)
						const float t = ages[c];
						if (r == 0 || r == 1)
						{
							trail::ArcSpec a;
							a.iso = cellWorld(r, c, 0.78f);
							a.radius = 70.f; a.blade = 40.f; a.z = 20.f;
							if (r == 0) { a.a0 = -2.6f; a.a1 = 1.0f; }
							else { a.tilt = 1.25f; a.a0 = -2.9f; a.a1 = 0.4f; a.edge = util::Rgb(0xEFFFFB); a.body = util::Rgb(0x7FD0C4); }
							a.delay = kT - t * 0.3f;   // 리본은 0.28초 안에 끝난다 — 나이를 0.3배로
							trail::Arc(a);
						}
						else if (r == 2)
						{
							trail::FollowSpec f;
							f.width = 22.f; f.fade = 0.3f;
							const Vector2 a = cellWorld(r, c, 0.7f);
							streaks.push_back({ Vector2{ a.x - 80.f, a.y + 10.f }, Vector2{ a.x + 80.f, a.y - 10.f }, t * 0.3f });
						}
						continue;
					}
					const vfx::Kind kind = static_cast<vfx::Kind>(k);
					const bool ground = kind >= vfx::Kind::Rune;
					const bool bottom = kind == vfx::Kind::Fire || kind == vfx::Kind::Pillar || kind == vfx::Kind::Lightning || kind == vfx::Kind::Dust;
					const float size = ground ? 190.f : 130.f;
					vfx::Spec s = vfx::Make(kind, cellWorld(r, c, ground ? 0.55f : 0.88f), 0.f, size, kLife, ramps[k]);
					s.seed = 0.37f;
					if (kind == vfx::Kind::Pillar || kind == vfx::Kind::Lightning) { s.w = 70.f; s.h = rowH * 0.95f; }
					if (kind == vfx::Kind::Fire) { s.w = 90.f; s.h = rowH * 0.9f; }
					if (!ground && !bottom) s.z = s.h * 0.5f + 6.f;
					s.delay = kT - ages[c] * kLife;
					if (kind == vfx::Kind::Beam)
					{
						const Vector2 a = cellWorld(r, c, 0.6f);
						vfx::BeamBetween(Vector2{ a.x - 90.f, a.y }, 10.f, Vector2{ a.x + 90.f, a.y }, 30.f, 30.f, kLife, s);
					}
					else vfx::Spawn(s);
				}
			}
			// 시간을 kT까지 돌린다(1/60초씩)
			for (int i = 0; i < static_cast<int>(kT * 60.f + 0.5f); ++i) { vfx::Update(1.f / 60.f); trail::Update(1.f / 60.f); }
			for (const auto& q : streaks)
			{
				trail::FollowSpec f;
				f.width = 22.f; f.fade = 0.3f;
				trail::Streak(q.a, 20.f, q.b, 30.f, f, q.age);
			}
			if (lastPage)
			{
				// 순간 조명 · 충격파는 칸마다 같은 나이로 따로 그려야 해서 한 칸씩(마지막 칸 나이) 보여 준다
				for (int c = 0; c < 6; ++c)
				{
					flashlight::Add(cellWorld(3, c, 0.6f), 110.f, util::Rgb(0xFFC878), 1.f, 1.f, 0.02f);
				}
				for (int c = 0; c < 6; ++c)
					post::Shockwave(cellWorld(4, c, 0.5f), 10.f, 90.f, 16.f, 1.2f, 1.f);
				for (int i = 0; i < 12; ++i) { flashlight::Update(1.f / 60.f); post::Update(1.f / 60.f, 1.f / 60.f); }
			}
			for (int frame = 0; frame < 2; ++frame)
			{
				BeginDrawing();
				ClearBackground(BLACK);
				terrain.DrawGround(cam, W, H, Vector2{ cx, cy }, 0.f, false, 0.f);
				BeginMode2D(cam);
				gfx::BeginWorld(cx + cy, cam.zoom);
				vfx::DrawGround();
				trail::Draw();
				vfx::DrawWorld();
				EndMode2D();
				flashlight::Apply(cam, W, H, light.Night());
				post::Apply(cam, W, H, [] { vfx::DrawGround(); trail::Draw(); vfx::DrawWorld(); });
				if (frame == 1)
				{
					const auto& st = post::LastStats();
					TraceLog(LOG_INFO, "VFXSHEET: post shocks=%d grabbed=%d bloom=%d %.2fms trails=%d", st.shocks, st.grabbed ? 1 : 0, st.bloom ? 1 : 0, st.bloomMs, trail::Count());
				}
				// 칸 선 · 이름
				for (int r = 0; r <= 5; ++r) DrawLine(0, 34 + static_cast<int>(rowH * r), W, 34 + static_cast<int>(rowH * r), Color{ 0, 0, 0, 120 });
				for (int c = 0; c < 6; ++c)
					DrawText(TextFormat("age %.2f", ages[c]), 180 + static_cast<int>(colW * c), 8, 20, RAYWHITE);
				for (int r = 0; r < 5; ++r)
				{
					const int k = page * 5 + r;
					const char* n = lastPage ? (r == 0 ? "Arc" : r == 1 ? "Arc tilt" : r == 2 ? "Streak" : r == 3 ? "FlashLight" : "Shockwave") : (k < kinds ? names[k] : "");
					DrawRectangle(0, 34 + static_cast<int>(rowH * r), 165, static_cast<int>(rowH), Color{ 12, 14, 18, 230 });
					DrawText(n, 10, 34 + static_cast<int>(rowH * r + rowH * 0.5f) - 10, 20, RAYWHITE);
				}
				if (frame == 1)
				{
					rlDrawRenderBatchActive();
					Image img = LoadImageFromScreen();
					const std::string path = outBase + "_" + std::to_string(page) + ".png";
					ExportImage(img, path.c_str());
					UnloadImage(img);
					TraceLog(LOG_INFO, "VFXSHEET: %s", path.c_str());
				}
				EndDrawing();
			}
		}
		terrain.Shutdown();
		light.Shutdown();
	}

	void RunGallery(const std::string& shot, float after, float hour)
	{
		Terrain terrain;
		terrain.Init();
		Lighting light;
		const float cx = TownMap::kTownCenter.x, cy = TownMap::kTownCenter.y;
		float zoom = 1.25f;
		float time = 0.f;
		float run = 0.f;
		while (!WindowShouldClose())
		{
			const float dt = GetFrameTime();
			time += dt;
			run += dt;
			light.UpdateClock(hour);
			props::SetNight(light.Night());
			zoom = std::clamp(zoom + GetMouseWheelMove() * 0.1f, 0.6f, 2.5f);
			const int W = GetScreenWidth(), H = GetScreenHeight();
			Camera2D cam{};
			cam.offset = Vector2{ W * 0.5f, H * 0.5f };
			cam.target = gfx::IsoPx(cx, cy);
			cam.zoom = zoom;

			BeginDrawing();
			ClearBackground(BLACK);
			terrain.DrawGround(cam, W, H, Vector2{ cx, cy }, time, false, 0.f);
			BeginMode2D(cam);
			gfx::BeginWorld(cx + cy, zoom);
			props::FrameLights().clear();
			gfx::DepthTest(true);
			gfx::DepthWrite(true);
			sprites::BeginWorld(0.35f);
			terrain.DrawProps(cam, W, H, Vector2{ cx, cy }, time, false);
			for (size_t i = 0; i < MonsterTable::kRowCount; ++i)
			{
				const MonsterTableRow& row = MonsterTable::kRows[i];
				const float X = cx - 9.f + static_cast<float>(i % 9) * 2.3f;
				const float Y = cy - 12.f + static_cast<float>(i / 9) * 4.5f;
				actors::Pose p;
				p.base = gfx::IsoPx(X, Y);
				p.time = time;
				p.walk = time * 6.f;
				p.moving = (static_cast<int>(time / 3.f) + static_cast<int>(i)) % 2 ? 1.f : 0.f;
				p.attack = std::fmod(time + i * 0.37f, 2.5f) < 0.5f ? std::fmod(time + i * 0.37f, 2.5f) / 0.5f : -1.f;
				p.faceRight = (i % 2) == 0;
				p.seed = static_cast<uint32_t>(i);
				gfx::SetDepth(X + Y);
				actors::DrawShadow(p.base, actors::MonsterShadow(row), 1.f);
				actors::DrawMonster(p, row);
			}
			// 직업 3종: 서기 · 걷기 · 평타 · 기술 · 맞음/쓰러짐을 차례로
			for (int c = 0; c < 3; ++c)
				for (int k = 0; k < 5; ++k)
				{
					const float X = cx - 9.f + static_cast<float>(k) * 2.f + c * 0.25f;
					const float Y = cy + 3.5f + static_cast<float>(c) * 2.6f - k * 0.0f;
					actors::Pose p;
					p.base = gfx::IsoPx(X, Y);
					p.time = time + k * 0.9f;
					p.walk = time * 8.f;
					p.seed = static_cast<uint32_t>(c * 7 + k);
					p.faceRight = true;
					const float cyc = std::fmod(time + k * 0.31f, 2.4f);
					switch (k)
					{
					case 1: p.moving = 1.f; break;
					case 2: p.attack = cyc < 0.42f ? cyc / 0.42f : -1.f; break;
					case 3:
					{
						static constexpr uint16_t kSk[3] = { 3, 9, 13 };
						p.skill = kSk[c];
						p.skillT = cyc < 0.9f ? cyc / 0.9f : -1.f;
						break;
					}
					case 4:
						p.hurt = cyc < 0.35f ? 1.f - cyc / 0.35f : 0.f;
						p.dead = cyc > 1.2f ? util::Clamp01((cyc - 1.2f) / 0.7f) : 0.f;
						break;
					default: break;
					}
					gfx::SetDepth(X + Y);
					actors::DrawShadow(p.base, 15.f, 1.f);
					actors::DrawPlayer(p, actors::LookForPlayer(0x9E3779B9u * (c + 1), 0, 0, static_cast<uint8_t>(c)));
				}
			// 마을 사람들
			for (size_t i = 0; i < WorldData::kNpcCount; ++i)
			{
				const auto& npc = WorldData::kNpcs[i];
				const float X = cx - 9.f + static_cast<float>(i % 10) * 1.9f;
				const float Y = cy + 11.5f + static_cast<float>(i / 10) * 2.6f;
				actors::Pose p;
				p.base = gfx::IsoPx(X, Y);
				p.time = time + i * 1.3f;
				p.walk = time * 6.f;
				p.moving = (static_cast<int>(time / 2.5f) + static_cast<int>(i)) % 3 == 0 ? 1.f : 0.f;
				p.cast = (static_cast<int>(time / 2.5f) + static_cast<int>(i)) % 3 == 1 ? 0.6f : 0.f;
				p.faceRight = (i % 2) == 0;
				p.seed = static_cast<uint32_t>(i);
				gfx::SetDepth(X + Y);
				actors::DrawShadow(p.base, WorldData::IsAnimal(npc.kind) ? 10.f : 15.f, 1.f);
				actors::DrawNpc(p, npc);
			}
			sprites::EndWorld();
			gfx::Flush();
			gfx::DepthTest(false);
			EndMode2D();
			light.Apply(cam, W, H, props::FrameLights());

			ui::TextStyle st;
			st.outline = 1.5f;
			st.size = 13.f;
			for (size_t i = 0; i < MonsterTable::kRowCount; ++i)
			{
				const MonsterTableRow& row = MonsterTable::kRows[i];
				const float X = cx - 9.f + static_cast<float>(i % 9) * 2.3f;
				const float Y = cy - 12.f + static_cast<float>(i / 9) * 4.5f;
				const Vector2 s = GetWorldToScreen2D(gfx::IsoPx(X, Y), cam);
				st.color = row.boss ? util::Rgb(0xFF8A3A) : WHITE;
				ui::Fonts().DrawCentered(std::string(row.name) + " Lv." + std::to_string(row.level), Vector2{ s.x, s.y + 14.f }, st);
			}
			float ix = 16.f;
			for (const auto& row : ItemTableData::kRows)
			{
				icons::Draw(row.id, Rectangle{ ix, static_cast<float>(H) - 70.f, 52.f, 52.f });
				ix += 58.f;
			}
			st.size = 22.f;
			st.color = util::Rgb(0xFFE08A);
			ui::Fonts().Draw("도감 — 몬스터 34종 · 아이템 18종  (휠: 확대/축소, Esc: 종료)", Vector2{ 16, 14 }, st);
			if (!shot.empty() && run > after)
			{
				rlDrawRenderBatchActive();
				TakeScreenshot(shot.c_str());
				EndDrawing();
				break;
			}
			EndDrawing();
		}
		terrain.Shutdown();
		light.Shutdown();
	}
}

int main(int argc, char** argv)
{
	Game::Options opts;
	rec::Options recOpts;
	int width = 1280, height = 720;
	bool gallery = false;
	std::string vfxSheet;
	bool noMsaa = false;
	std::string sandboxClock;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		auto next = [&](const char* def) -> std::string { return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def); };
		if (a == "--host") opts.host = next("127.0.0.1");
		else if (a == "--port") opts.port = static_cast<uint16_t>(std::atoi(next("4000").c_str()));
		else if (a == "--name") opts.autoName = next("");
		else if (a == "--screenshot") opts.screenshot = next("shot.png");
		else if (a == "--after") opts.screenshotAfter = static_cast<float>(std::atof(next("8").c_str()));
		else if (a == "--count") opts.screenshotCount = std::max(1, std::atoi(next("1").c_str()));
		else if (a == "--every") opts.screenshotEvery = static_cast<float>(std::atof(next("1").c_str()));
		else if (a == "--hour") opts.hour = static_cast<float>(std::atof(next("12").c_str()));
		else if (a == "--script") opts.script = next("");
		else if (a == "--mute") opts.mute = true;
		else if (a == "--lag") opts.lagMs = std::atoi(next("0").c_str());
		else if (a == "--trace-move") opts.traceMove = next("move.csv");
		else if (a == "--jitter") opts.jitterMs = std::atoi(next("0").c_str());
		else if (a == "--gallery") gallery = true;
		else if (a == "--vfx-sheet") vfxSheet = next("vfx_sheet");
		else if (a == "--quality")
		{
			const std::string q = next("high");
			opts.quality = q == "low" || q == "0" ? 0 : (q == "medium" || q == "1" ? 1 : 2);
		}
		else if (a == "--photosafe") opts.photoSafe = 1;
		else if (a == "--otherfx") opts.otherFx = std::clamp(std::atoi(next("50").c_str()), 0, 100);
		else if (a == "--f3") opts.debugOverlay = true;
		else if (a == "--no-msaa") noMsaa = true;
		else if (a == "--record") recOpts.dir = next("record");
		else if (a == "--seconds") recOpts.seconds = static_cast<float>(std::atof(next("10").c_str()));
		else if (a == "--fps") recOpts.fps = std::clamp(std::atoi(next("30").c_str()), 1, 120);
		else if (a == "--record-after") recOpts.after = static_cast<float>(std::atof(next("0").c_str()));
		else if (a == "--record-scale") recOpts.scale = static_cast<float>(std::atof(next("1").c_str()));
		else if (a == "--record-frames") recOpts.frames = true;
		else if (a == "--fixed-step") recOpts.fixedStep = true;
		else if (a == "--sandbox-clock") sandboxClock = next("/dev/shm/harven_clock");
		else if (a == "--frame-jitter")
		{
			float j = 0.f, h = 0.f;
			std::sscanf(next("0.3,2").c_str(), "%f,%f", &j, &h);
			clk::SetFrameJitter(j, h);
		}
		else if (a == "--netsim")
		{
			// latency=150,jitter=40,spike=2  (spike = 패킷 100개당 재전송 흉내 개수)
			const std::string spec = next("latency=150,jitter=40,spike=2");
			size_t pos = 0;
			while (pos < spec.size())
			{
				const size_t comma = spec.find(',', pos);
				const std::string kv = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
				const size_t eq = kv.find('=');
				if (eq != std::string::npos)
				{
					const std::string k = kv.substr(0, eq);
					const double v = std::atof(kv.c_str() + eq + 1);
					if (k == "latency" || k == "lag") opts.netsim.latencyMs = static_cast<int>(v);
					else if (k == "jitter") opts.netsim.jitterMs = static_cast<int>(v);
					else if (k == "spike") opts.netsim.spikePercent = static_cast<float>(v);
					else if (k == "spikemin") opts.netsim.spikeMinMs = static_cast<int>(v);
					else if (k == "spikemax") opts.netsim.spikeMaxMs = static_cast<int>(v);
				}
				if (comma == std::string::npos) break;
				pos = comma + 1;
			}
		}
		else if (a == "--class") opts.classId = std::clamp(std::atoi(next("0").c_str()), 0, 2);
		else if (a == "--size")
		{
			const std::string s = next("1280x720");
			std::sscanf(s.c_str(), "%dx%d", &width, &height);
		}
	}

	// --script skills:<직업>(Phase 2-E): 훈련장 허수아비에 그 직업의 모든 기술 — 직업은 이름(서약기사 · 각인술사 · 밤사냥꾼) 또는 0~2
	if (opts.script.rfind("skills:", 0) == 0)
	{
		const std::string c = opts.script.substr(7);
		if (c == "서약기사" || c == "0") opts.classId = 0;
		else if (c == "각인술사" || c == "1") opts.classId = 1;
		else if (c == "밤사냥꾼" || c == "2") opts.classId = 2;
	}

	// --no-msaa: 소프트웨어 렌더링(샌드박스 녹화)에서는 4배 표본이 그대로 4배 비용이다
	SetConfigFlags((noMsaa ? 0u : static_cast<unsigned>(FLAG_MSAA_4X_HINT)) | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(width, height, "하르벤");
	if (!IsWindowReady())
	{
		// 그래픽 드라이버/디스플레이를 열 수 없다 — 크래시 대신 알리고 끝낸다.
		std::fprintf(stderr, "창을 만들 수 없습니다(OpenGL 3.3 드라이버/디스플레이 확인).\n");
		return 1;
	}
	SetWindowMinSize(1024, 640);
	{
		// 창 · 작업 표시줄 아이콘(지평선과 샛별). 실행 파일 아이콘은 res/app.rc
		const std::string icon = FindAssetDir("assets/icon", "harven.png") + "/harven.png";
		if (FileExists(icon.c_str()))
		{
			Image img = LoadImage(icon.c_str());
			if (img.data)
			{
				ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
				SetWindowIcon(img);
				UnloadImage(img);
			}
		}
	}
	SetExitKey(KEY_NULL);
	SetTargetFPS(144);
	perf::InstallGlHooks();
	if (!sandboxClock.empty() && clk::AttachShared(sandboxClock.c_str(), recOpts.fps))
		recOpts.fixedStep = true;   // 공유 시계 = 한 프레임에 정확히 1/fps초
	rec::Configure(recOpts);
	rlDisableBackfaceCulling();
	HideCursor();

	ui::Fonts().Init(FindAssetDir("assets/fonts", "Pretendard-Medium.ttf"));
	art::Init(FindAssetDir("assets/art", "key_art.jpg"));
	sprites::Init(FindAssetDir("assets/sprites", "actors.txt"));
	envart::Init(FindAssetDir("assets/terrain", "ground_atlas.jpg"));
	fxart::Init(FindAssetDir("assets/fx", "hit_star.png"));
	uiart::Init(FindAssetDir("assets/ui", "panel_tex.png"));
	assets::Load();
	icons::Build();
	icons::LoadPainted(FindAssetDir("assets/icons", "icons.png"));
	if (!opts.mute)
	{
		sfx::Init();
		amb::Init();   // 마을 소리 구역(분수 · 강 · 바람 · 시장 · 대장간) — 소리 장치가 있을 때만
		voice::Init(FindAssetDir("assets/voice", "voice.txt"));
	}
	ime::Attach();
	if (const char* beds = std::getenv("CLIENT_AMB_BEDS")) amb::DumpBeds(beds);   // 개발용: 소리 구역 고리 소리 점검

	if (!vfxSheet.empty())
		RunVfxSheet(vfxSheet, opts.hour);
	else if (gallery)
	{
		ShowCursor();
		RunGallery(opts.screenshot, opts.screenshotAfter, opts.hour);
	}
	else
	{
		Game game;
		game.Init(opts);
		while (!WindowShouldClose() && !game.WantsQuit())
			game.Frame();
		game.Shutdown();
	}

	ime::Detach();
	voice::Shutdown();
	amb::Shutdown();
	sfx::Shutdown();
	icons::Unload();
	assets::Unload();
	sprites::Shutdown();
	envart::Shutdown();
	fxart::Shutdown();
	post::Shutdown();
	vfx::Shutdown();
	trail::Shutdown();
	uiart::Shutdown();
	art::Shutdown();
	ui::Fonts().Shutdown();
	CloseWindow();
	return 0;
}
