#pragma once

#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// 녹화(--record) — 움직임 · 연출은 정지 화면으로 판단하지 않는다(공통 규칙 6).
//
//   Client2D --record out/run --seconds 12 --fps 30 [--record-after 3] [--record-scale 0.5]
//
//  · 화면에 보이는 그대로(HUD · F3 오버레이 포함) 벽시계 기준 1/fps마다 한 장씩 뜬다.
//    게임이 fps보다 느리면 같은 장을 되풀이해 시간축을 지킨다 — 영상의 끊김 = 실제 끊김.
//  · ffmpeg가 PATH에 있으면 원시 프레임을 파이프로 넘겨 <폴더>/capture.mp4를 바로 만든다
//    (디스크에 수 GB의 프레임을 쌓지 않는다). 없거나 --record-frames면 <폴더>/frame_00001.ppm …
//  · GIF는 tools/record/make_gif.sh(팔레트 생성 2패스)로 만든다.
//  · 고정 시간 모드(--fixed-step): 한 장 = 정확히 1/fps초씩 게임 시간을 진행한다. 소프트웨어
//    렌더링처럼 실시간을 못 따라가는 곳에서 "내 캐릭터의 움직임 규칙"만 볼 때 쓴다(네트워크는 실시간).
// ─────────────────────────────────────────────────────────────────────────────
namespace rec
{
	struct Options
	{
		std::string dir;           // 비우면 녹화 안 함
		float seconds = 10.f;
		int   fps = 30;
		float after = 0.f;         // 게임 화면에 들어간 뒤 몇 초 뒤부터
		float scale = 1.f;         // 0.25 ~ 1(작게 저장)
		bool  frames = false;      // ffmpeg가 있어도 프레임 파일로
		bool  fixedStep = false;
	};

	void Configure(const Options& o);
	[[nodiscard]] const Options& Get();
	[[nodiscard]] bool Enabled();                // --record가 주어졌다
	[[nodiscard]] bool Recording();              // 지금 뜨는 중
	[[nodiscard]] bool Finished();               // 다 떴다(게임을 끝내도 된다)
	[[nodiscard]] bool FixedStep();
	[[nodiscard]] float StepSeconds();           // 고정 시간 모드의 한 장 = 1/fps

	// 게임 화면에 들어온 시각(녹화 시작 기준점). 한 번만 의미가 있다.
	void Arm(double now);
	// 첫 장을 뜨는 시각(Arm + after) — 스크립트가 "녹화 몇 초에 무엇"을 찍을 때(기술별 GIF 자르기). 아직이면 −1
	[[nodiscard]] double StartAt();
	// 매 프레임, 모든 그리기가 끝나고 EndDrawing 직전에. 필요한 만큼 장을 뜬다.
	void Capture(double now);
	void Finish();
}
