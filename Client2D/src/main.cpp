// ─────────────────────────────────────────────────────────────────────────────
// 여명의 대지 — MMORPG-with-IOCP 2D(아이소메트릭) 클라이언트
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
//     --lag 120 --jitter 60           인공 네트워크 지연(한쪽 방향 ms)·지터 — 조작감 검증용
//     --class 0|1|2                   새 캐릭터 직업(0 황혼의 기사 · 1 룬술사 · 2 그림자 사냥꾼)
//     --script hunt|town|inventory|map|help|enchant|chat|combat|charwin
// ─────────────────────────────────────────────────────────────────────────────
#include "raylib.h"
#include "rlgl.h"

#include "game/Game.h"
#include "render/Assets.h"
#include "render/Icons.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Terrain.h"
#include "render/Lighting.h"
#include "render/Props.h"
#include "audio/Sfx.h"
#include "platform/Ime.h"
#include "ui/Font.h"
#include "ui/Ui.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{
	std::string FindFont()
	{
		const char* rel = "assets/fonts/NanumSquareRoundB.ttf";
		const std::string appDir = GetApplicationDirectory();
		const std::string candidates[] = { appDir + rel, std::string(rel), std::string("../") + rel, std::string("../../") + rel };
		for (const auto& c : candidates)
			if (FileExists(c.c_str()))
				return c;
		return rel;
	}

	// ── 서버 없이 보는 도감(그림 확인용) ──
	void RunGallery(const std::string& shot, float after, float hour)
	{
		Terrain terrain;
		terrain.Init();
		Lighting light;
		const float cx = WorldData::kTownX0 + 22.f, cy = WorldData::kTownY0 + 30.f;
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
	int width = 1280, height = 720;
	bool gallery = false;
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
		else if (a == "--class") opts.classId = std::clamp(std::atoi(next("0").c_str()), 0, 2);
		else if (a == "--size")
		{
			const std::string s = next("1280x720");
			std::sscanf(s.c_str(), "%dx%d", &width, &height);
		}
	}

	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
	InitWindow(width, height, "여명의 대지 - MMORPG-with-IOCP");
	if (!IsWindowReady())
	{
		// 그래픽 드라이버/디스플레이를 열 수 없다 — 크래시 대신 알리고 끝낸다.
		std::fprintf(stderr, "창을 만들 수 없습니다(OpenGL 3.3 드라이버/디스플레이 확인).\n");
		return 1;
	}
	SetWindowMinSize(1024, 640);
	SetExitKey(KEY_NULL);
	SetTargetFPS(144);
	rlDisableBackfaceCulling();
	HideCursor();

	ui::Fonts().Init(FindFont());
	assets::Load();
	icons::Build();
	if (!opts.mute)
		sfx::Init();
	ime::Attach();

	if (gallery)
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
	sfx::Shutdown();
	icons::Unload();
	assets::Unload();
	ui::Fonts().Shutdown();
	CloseWindow();
	return 0;
}
