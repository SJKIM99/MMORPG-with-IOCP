// ─────────────────────────────────────────────────────────────────────────────
// Phase 6 시나리오(--script) — 전체 플레이 녹화 · 붐비는 마을 성능
//
//  fullplay          한 캐릭터(lv50_fp — 샌드박스 꾸러미 "_fp": 은 목걸이(영웅) · 귀환 두루마리 · 물약)로 접속부터 경매까지 한 번에:
//                    접속(로그인 화면에서 이름을 치고 접속) → 마을 투어(카메라) → 훈련장 기술 시연 → 데니차 들녘 사냥 →
//                    모로즈(벨고라 설원) → 채팅(전체 · 귓속말) → 결사(세우기 · 초대 · 결사 채팅 · 깃발 뜰) → 경매장(올리기 · 팔림 · 받기).
//                    장면 · 이동이 바뀔 때마다 [fp] seg/mark <이름> rec=<녹화 초>를 찍는다 →
//                    tools/record/fullplay_cut.py가 걷는 구간을 빠르게 감아 한 편으로 붙인다(README 대표 영상).
//                    훈련장 · 들녘 · 보스는 이미 있는 시나리오(training · skills · boss3)를 그 장면 동안 그대로 돌린다(RunSubScript).
//  fullplay:helper   도우미(lv30_fq): 회관 앞에서 기다리다 전체 채팅에 "결사"가 나오면 귓속말로 답하고, 초대를 받아 결사 채팅을 한 뒤,
//                    경매장에서 은 목걸이가 올라오면 사서 정산함에서 받는다.
//  crowd             분수 광장에 서서 다른 플레이어(harven_bot --crowd 30)와 마을 NPC가 모인 화면의 프레임을 잰다(F3 그래프를 켠 채 20초).
//                    [crowd] 줄(프레임 · CPU 시간 · 드로우콜 · 화면 안 수) + [mem] → 스크린샷 뒤 끝.
// ─────────────────────────────────────────────────────────────────────────────
#include "game/Game.h"
#include "game/KeyBinds.h"
#include "game/Input.h"
#include "core/Perf.h"
#include "core/Recorder.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "ui/Ui.h"
#include "ui/Font.h"
#include "ui/Wm.h"
#include "audio/Sfx.h"
#include "Data/GuildAuction.h"
#include "Data/TownMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	// 녹화 시작 뒤 초(녹화하지 않으면 −1)
	[[nodiscard]] double RecT(double now)
	{
		const double r0 = rec::StartAt();
		return r0 >= 0.0 ? now - r0 : -1.0;
	}

	struct Stat { float avg = 0.f, p95 = 0.f, p99 = 0.f, max = 0.f; };
	[[nodiscard]] Stat Summ(std::vector<float> v)
	{
		Stat s;
		if (v.empty()) return s;
		double sum = 0.0;
		for (float x : v) sum += x;
		s.avg = static_cast<float>(sum / v.size());
		std::sort(v.begin(), v.end());
		s.p95 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.95))];
		s.p99 = v[std::min(v.size() - 1, static_cast<size_t>(v.size() * 0.99))];
		s.max = v.back();
		return s;
	}

	// UTF-8 한 글자 뒤의 바이트 위치
	[[nodiscard]] size_t NextChar(const std::string& s, size_t i)
	{
		if (i >= s.size()) return s.size();
		++i;
		while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
		return i;
	}

	// 마을 투어 자리(구획 표 TownMap::kDistricts에서 이름으로 — 월드 칸)
	struct FpStop { const char* name; float hold; };
	constexpr FpStop kFpStops[] = {
		{ "데니차", 2.6f }, { "분수 광장", 1.9f }, { "시장 거리", 1.9f }, { "결사 회관 단", 1.9f }, { "장인 거리", 1.9f }, { "훈련장", 1.7f },
	};
	[[nodiscard]] const TownMap::DistrictRow* District(const char* name)
	{
		for (const auto& d : TownMap::kDistricts)
			if (std::strcmp(d.name, name) == 0) return &d;
		return nullptr;
	}
	float EaseInOut(float u) { return u < 0.5f ? 4.f * u * u * u : 1.f - std::pow(-2.f * u + 2.f, 3.f) / 2.f; }
}

// ─────────────────────────────────────────────────────────────────────────────
// 성능 CSV(--perf-csv) — 프레임마다 한 줄(그래프는 tools/perf/plot_frames.py)
// ─────────────────────────────────────────────────────────────────────────────
void Game::WritePerfRow()
{
	if (!m_perfCsv) return;
	int players = 0, monsters = 0;
	for (const auto& [id, e] : m_ents)
	{
		if (e.kind == EntityKind::Player && id != m_selfId) ++players;
		else if (e.kind == EntityKind::Monster && !e.dead) ++monsters;
	}
	std::fprintf(m_perfCsv, "%.3f,%.2f,%.2f,%d,%d,%d,%d,%d,%d\n", m_runTime, perf::FrameMs(0), perf::WorkMs(0), perf::DrawCallsLast(),
		static_cast<int>(fx::Particles().Count()), players, monsters, static_cast<int>(m_npcView.size()), m_perfPhase);
}

// ─────────────────────────────────────────────────────────────────────────────
// 접속 장면(fullplay): 로그인 화면에서 이름을 한 자씩 치고 접속한다 — 녹화는 이 화면부터
// ─────────────────────────────────────────────────────────────────────────────
void Game::TickLoginDemo(float dt)
{
	if (!m_loginDemo.on || m_loginDemo.sent || m_scene != Scene::Login) return;
	m_loginDemo.t += dt;
	const float typeFrom = 2.2f, perChar = 0.11f;   // 들어오는 연출(로고 · 판)이 자리 잡은 뒤에 친다
	if (m_loginDemo.t > typeFrom && m_loginDemo.typed < m_loginDemo.name.size())
	{
		const size_t want = static_cast<size_t>((m_loginDemo.t - typeFrom) / perChar) + 1;
		size_t at = 0, n = 0;
		while (at < m_loginDemo.name.size() && n < want) { at = NextChar(m_loginDemo.name, at); ++n; }
		if (at != m_loginDemo.typed)
		{
			m_loginDemo.typed = at;
			m_fName.text = m_loginDemo.name.substr(0, at);
			sfx::Play(sfx::Id::Click, 0.15f);
		}
		return;
	}
	if (m_loginDemo.typed >= m_loginDemo.name.size() && m_loginDemo.t > typeFrom + perChar * static_cast<float>(m_loginDemo.name.size()) + 1.3f)
	{
		std::printf("[fp] seg 0 login rec=%.2f\n", RecT(m_now));
		std::fflush(stdout);
		m_loginDemo.sent = true;
		sfx::Play(sfx::Id::Click, 0.5f);
		BeginConnect();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 장면 자막(fullplay) — 화면 위쪽 가운데. HUD 위에 겹치지 않게 먹빛 띠 위에 이름 · 한 줄
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawSceneCaption()
{
	if (m_fp.title.empty()) return;
	const float age = static_cast<float>(m_now - m_fp.captionAt);
	if (age < 0.f || age > 3.8f) return;
	const float in = util::Clamp01(age / 0.45f);
	const float out = 1.f - util::Clamp01((age - 3.1f) / 0.7f);
	const float a = util::EaseOutCubic(in) * out;
	if (a <= 0.01f) return;
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const float k = H / 720.f;
	const float cy = std::round(H * 0.19f);
	const float rise = (1.f - util::EaseOutCubic(in)) * 10.f * k;
	const int bandW = static_cast<int>(380.f * k), bandH = static_cast<int>(100.f * k);
	const int by = static_cast<int>(cy - 42.f * k);
	DrawRectangleGradientH(static_cast<int>(W * 0.5f) - bandW, by, bandW, bandH, util::A8(ui::theme::kInk, 0), util::MulA(util::A8(ui::theme::kInk, 205), a));
	DrawRectangleGradientH(static_cast<int>(W * 0.5f), by, bandW, bandH, util::MulA(util::A8(ui::theme::kInk, 205), a), util::A8(ui::theme::kInk, 0));
	ui::TextStyle ts;
	ts.size = std::round(34.f * k);
	ts.face = ui::Face::Display;
	ts.color = util::MulA(ui::theme::kTextHi, a);
	ts.outline = 1.5f;
	ts.outlineColor = util::MulA(util::A8(ui::theme::kShadow, 160), a);
	ui::Fonts().DrawCentered(m_fp.title, Vector2{ std::round(W * 0.5f), std::round(cy - 12.f * k + rise) }, ts);
	DrawRectangle(static_cast<int>(W * 0.5f - 32.f * k), static_cast<int>(cy + 12.f * k + rise), static_cast<int>(64.f * k), 2, util::MulA(ui::theme::kDawn, a));
	if (!m_fp.sub.empty())
	{
		ui::TextStyle ss;
		ss.size = std::round(15.f * k);
		ss.face = ui::Face::Body;
		ss.color = util::MulA(ui::theme::kText, a);
		ss.outline = 1.f;
		ss.outlineColor = util::MulA(util::A8(ui::theme::kShadow, 150), a);
		ui::Fonts().DrawCentered(m_fp.sub, Vector2{ std::round(W * 0.5f), std::round(cy + 32.f * k + rise) }, ss);
	}
}

// 이미 있는 시나리오 하나를 이번 프레임만 돌린다(그 시나리오의 단계 · 시간 · 순서표를 그대로 쓴다)
void Game::RunSubScript(const char* name, float dt)
{
	const std::string keep = m_opts.script;
	m_opts.script = name;
	RunScript(dt);
	m_opts.script = keep;
}

// 채팅 입력칸에 한 자씩 치고 Enter — 다 보냈으면 true
bool Game::TypeChatLine(const std::string& line, float dt, float perChar)
{
	if (m_fp.typing != line)
	{
		m_fp.typing = line;
		m_fp.typedAt = 0;
		m_fp.typeT = 0.f;
		OpenChatInput("");
		m_chatField.text.clear();
		return false;
	}
	m_fp.typeT += dt;
	if (m_fp.typedAt < line.size())
	{
		if (m_fp.typeT >= perChar)
		{
			m_fp.typeT = 0.f;
			m_fp.typedAt = NextChar(line, m_fp.typedAt);
			if (ui::Focused() != &m_chatField) OpenChatInput("");
			m_chatField.text = line.substr(0, m_fp.typedAt);
		}
		return false;
	}
	if (m_fp.typeT < 0.45f) return false;
	SubmitChat();
	m_chatOpen = false;
	ui::Focus(nullptr);
	m_fp.typing.clear();
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// fullplay — 한 번에 도는 전체 플레이
// ─────────────────────────────────────────────────────────────────────────────
void Game::RunFullPlay(float dt)
{
	if (!m_hasSelf || m_scene != Scene::Game) return;
	m_fp.segT += dt;
	m_fp.stepT += dt;
	const std::string helper = std::getenv("HARVEN_FP_HELPER") ? std::getenv("HARVEN_FP_HELPER") : "lv30_fq";
	auto seg = [&](int n, const char* key, const char* title, const char* sub)
	{
		// 누르고 있던 기술 키 · 커서 · 목표 · 순서표를 비우고 다음 장면으로
		if (m_scriptHolding) { kb::ScriptHold(static_cast<kb::Act>(m_scriptHoldAct), false); m_scriptHolding = false; }
		in::ClearScriptMouse();
		ClearGoal();
		m_fp.seg = n; m_fp.step = 0; m_fp.segT = 0.f; m_fp.stepT = 0.f;
		m_scriptStep = 0; m_scriptT = 0.f; m_scriptMark = 0.f;
		m_scriptSeq.clear(); m_scriptSeqIdx = 0; m_scriptSeqLoops = 0; m_scriptNextCast = 0.0;
		if (title && title[0]) { m_fp.title = title; m_fp.sub = sub ? sub : ""; m_fp.captionAt = m_now; }
		std::printf("[fp] seg %d %s rec=%.2f\n", n, key, RecT(m_now));
		std::fflush(stdout);
	};
	auto step = [&](int n) { m_fp.step = n; m_fp.stepT = 0.f; };
	auto mark = [&](const char* what) { std::printf("[fp] mark %s rec=%.2f\n", what, RecT(m_now)); std::fflush(stdout); };
	auto caption = [&](const char* title, const char* sub) { m_fp.title = title; m_fp.sub = sub; m_fp.captionAt = m_now; };

	switch (m_fp.seg)
	{
	case 0:   // 막 들어왔다 — 한숨 돌리고 투어로
		if (m_fp.segT > 1.2f) seg(1, "tour", "", "");
		break;

	case 1:   // 마을 투어: 카메라가 구획을 돌고, 그 사이 캐릭터는 훈련장으로 걸어간다
	{
		if (m_fp.step == 0)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, WorldData::kDummySpots[1].x, WorldData::kDummySpots[1].y - 2 });
			const auto* d0 = District(kFpStops[0].name);
			m_tourCam.on = true;
			m_tourCam.tile = d0 ? Vector2{ d0->x, d0->y } : Vector2{ TownMap::kTownCenter.x, TownMap::kTownCenter.y };
			m_tourCam.zoom = d0 ? d0->zoom : 0.5f;
			m_cam.SnapTo(TileToIso(m_tourCam.tile, 0.f));
			m_cam.SetZoomFree(m_tourCam.zoom);
			step(1);
			break;
		}
		if (m_fp.step == 1)
		{
			constexpr int n = static_cast<int>(std::size(kFpStops));
			float t = m_fp.stepT;
			bool done = true;
			for (int i = 0; i < n; ++i)
			{
				const auto* a = District(kFpStops[i].name);
				if (!a) continue;
				if (t < kFpStops[i].hold)
				{
					m_tourCam.tile = Vector2{ a->x, a->y };
					m_tourCam.zoom = a->zoom;
					if (m_tourCam.caption != a->name)
					{
						m_tourCam.caption = a->name;
						m_tourCam.sub = a->sub;
						m_tourCam.captionAt = m_now;
					}
					done = false;
					break;
				}
				t -= kFpStops[i].hold;
				if (i + 1 >= n) break;
				const auto* b = District(kFpStops[i + 1].name);
				if (!b) continue;
				const float travel = std::clamp(std::hypot(b->x - a->x, b->y - a->y) / 14.f, 1.4f, 2.6f);
				if (t < travel)
				{
					const float u = EaseInOut(t / travel);
					m_tourCam.tile = Vector2{ a->x + (b->x - a->x) * u, a->y + (b->y - a->y) * u };
					m_tourCam.zoom = a->zoom + (b->zoom - a->zoom) * u;
					done = false;
					break;
				}
				t -= travel;
			}
			if (done)
			{
				// 카메라를 캐릭터에게 돌려준다(쫓아가며 돌아온다)
				m_tourCam.on = false;
				m_tourCam.caption.clear();
				m_cam.SetZoomFree(1.f);
				mark("tour-end");
				step(2);
			}
			break;
		}
		// 훈련장에 닿으면(또는 오래 걸리면) 기술 시연
		if (m_goal.kind == GoalKind::None || m_fp.stepT > 25.f)
			seg(2, "training", "훈련장", "허수아비에게 직업 기술을 차례로 — 옆에 초당 피해");
		break;
	}

	case 2:   // 훈련장: training 시나리오(콤보 → 기술 여덟 → 이동기 → 각성기 → 고유 기술)를 한 바퀴
		RunSubScript("training", dt);
		if ((m_scriptSeqLoops >= 1 && m_fp.segT > 8.f) || m_fp.segT > 48.f)
			seg(3, "hunt", "", "");
		break;

	case 3:   // 데니차 들녘: skills 시나리오(남문 밖으로 → 가까운 적에게 다가가 기술 구조 V3 순서 — 콤보 · 기술 여덟 · 이동기 · 각성기 · 고유 기술)
	{
		const int before = m_scriptStep;
		RunSubScript("skills", dt);
		if (before < 2 && m_scriptStep >= 2)
		{
			caption("데니차 들녘", "마을을 둘러싼 들 — 수액덩이 · 들개 · 그락 약탈꾼");
			mark("hunt-arrive");
			step(1);
		}
		if (m_fp.step == 1 && m_fp.stepT > 24.f)
			seg(4, "boss", "", "");
		else if (m_fp.segT > 70.f)
			seg(4, "boss", "", "");
		break;
	}

	case 4:   // 모로즈: boss3 시나리오(길잡이 → 벨고라 설원 → 기술을 돌려 쓴다)
	{
		const int before = m_scriptStep;
		RunSubScript("boss3", dt);
		if (before < 4 && m_scriptStep >= 4)
		{
			caption("벨고라 설원", "모로즈 — 벨고라의 혹한");
			mark("boss-arrive");
			step(1);
		}
		if (m_fp.step == 1)
		{
			const Entity* boss = nullptr;
			for (const auto& [id, e] : m_ents)
				if (e.IsBoss()) { boss = &e; break; }
			if (boss) m_fp.bossSeen = true;
			if (m_fp.bossSeen && !m_fp.bossDown && (!boss || boss->dead))
			{
				m_fp.bossDown = true;
				mark("boss-down");
				step(2);
			}
			else if (m_fp.stepT > 100.f)
			{
				mark("boss-timeout");
				step(2);
			}
		}
		else if (m_fp.step == 2 && m_fp.stepT > 4.5f)
			seg(5, "chat", "", "");
		break;
	}

	case 5:   // 채팅: 귀환 → 전체 채팅에 사람을 모은다 → 도우미의 귓속말에 답한다
		switch (m_fp.step)
		{
		case 0:
			if (m_fp.stepT > 0.5f) { UseItemSlot(5); mark("return"); step(1); }   // 보조 5 = 귀환 두루마리
			break;
		case 1:
			if (InTown() && m_fp.stepT > 2.0f)
			{
				caption("채팅", "전체 · 결사 · 귓속말 — 채널마다 색이 다르다");
				mark("chat");
				step(2);
			}
			else if (m_fp.stepT > 8.f && !InTown()) { UseItemSlot(5); m_fp.stepT = 0.f; }   // 한 번 더(재사용 대기에 걸렸으면)
			break;
		case 2:
			if (m_fp.stepT > 1.5f && TypeChatLine("/전 모로즈를 쓰러뜨렸소. 새 결사를 세우는데 함께할 사람?", dt))
				step(3);
			break;
		case 3:
			if (m_fp.whispered && m_fp.stepT > 1.2f) step(4);
			else if (m_fp.stepT > 16.f) step(5);
			break;
		case 4:
			if (TypeChatLine("/귓 " + (m_fp.heardFrom.empty() ? helper : m_fp.heardFrom) + " 좋소. 회관 앞에서 봅시다.", dt)) step(5);
			break;
		default:
			if (m_fp.stepT > 1.8f)
				seg(6, "guild", "결사", "회관지기 즈데노에게서 결사를 세우고 사람을 들인다");
			break;
		}
		break;

	case 6:   // 결사: 즈데노 → 세우기(이름 · 문장) → 축하 → /초대 → 결사 채팅 → 깃발 뜰
		switch (m_fp.step)
		{
		case 0:
			if (m_fp.stepT > 1.f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, GuildRule::kHallNpcId }); step(1); }
			break;
		case 1:
			if ((m_dialogNpc == GuildRule::kHallNpcId && m_fp.stepT > 1.4f) || m_fp.stepT > 45.f)
			{
				mark("guild-npc");
				CloseNpcWindows();
				OpenGuildCreate(false);
				m_guild.createName.text.clear();
				m_guild.shape = 0;
				m_guild.color = 0;
				step(2);
			}
			break;
		case 2:
		{
			// 이름을 한 자씩 · 문장 모양과 바탕색을 몇 번 바꿔 본다
			const std::string name = "첫새벽";
			const size_t want = std::min<size_t>(3, static_cast<size_t>(m_fp.stepT / 0.3f));
			size_t at = 0;
			for (size_t i = 0; i < want; ++i) at = NextChar(name, at);
			if (m_guild.createName.text != name.substr(0, at)) { m_guild.createName.text = name.substr(0, at); sfx::Play(sfx::Id::Click, 0.15f); }
			if (m_fp.stepT > 1.4f) m_guild.shape = 3;
			if (m_fp.stepT > 2.0f) m_guild.shape = 10;    // 불꽃
			if (m_fp.stepT > 2.6f) m_guild.color = 1;     // 적갈
			if (m_fp.stepT > 4.0f)
			{
				SendGuildReq(GuildOp::Create, GuildRule::Emblem(m_guild.shape, m_guild.color), 0, std::string(), name);
				step(3);
			}
			break;
		}
		case 3:
			if (m_guild.id != 0) { mark("guild-created"); CloseNpcWindows(); step(4); }
			else if (m_fp.stepT > 10.f) { std::printf("[fp] FAIL guild create\n"); step(4); }
			break;
		case 4:   // 축하 연출(3.8초)을 다 보고 /초대
			if (m_fp.stepT > 4.2f && TypeChatLine("/초대 " + helper, dt)) step(5);
			break;
		case 5:
			m_fp.pollT += dt;
			if (m_fp.pollT > 1.5f) { m_fp.pollT = 0.f; SendGuildReq(GuildOp::Info); }
			if (m_guild.members >= 2 && m_fp.stepT > 1.f) { mark("guild-joined"); step(6); }
			else if (m_fp.stepT > 30.f) { std::printf("[fp] FAIL helper join\n"); step(6); }
			break;
		case 6:
			if (m_fp.stepT > 1.5f && TypeChatLine("/길 첫새벽에 온 것을 환영하오. 깃발부터 보시오.", dt)) step(7);
			break;
		case 7:   // 깃발 뜰: 카메라를 물려 회관 앞 깃대 둘(우리 문장 천)이 들어오게
			if (m_fp.stepT > 2.5f) { m_cam.SetZoomFree(0.8f); mark("guild-flags"); step(8); }
			break;
		case 8:
			if (m_fp.stepT > 5.5f)
			{
				m_cam.SetZoomFree(1.f);
				seg(7, "auction", "경매장", "경매인 로렌초 — 올리기 · 팔림 · 정산함에서 받기");
			}
			break;
		}
		break;

	case 7:   // 경매장: 로렌초 → 은 목걸이(영웅)를 올린다 → 도우미가 산다 → 팔림 → 정산함에서 받는다
	{
		const bool acked = m_auction.pendingSeq == 0;
		switch (m_fp.step)
		{
		case 0:
			if (m_fp.stepT > 0.8f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, AuctionRule::kAuctioneerNpcId }); step(1); }
			break;
		case 1:
			if ((m_dialogNpc == AuctionRule::kAuctioneerNpcId && m_fp.stepT > 1.2f) || m_fp.stepT > 45.f)
			{
				mark("auction-npc");
				CloseNpcWindows();
				OpenAuction(1);
				step(2);
			}
			break;
		case 2:
			if (m_fp.stepT > 1.2f)
			{
				int slot = -1;
				for (int i = 0; i < static_cast<int>(m_inv.slots.size()); ++i)
					if (m_inv.slots[static_cast<size_t>(i)].itemId == ItemTableData::Id::SilverPendant && !m_inv.slots[static_cast<size_t>(i)].equipped) { slot = i; break; }
				if (slot < 0) { std::printf("[fp] FAIL no pendant\n"); step(9); break; }
				m_fp.slot = slot;
				m_auction.regSlot = slot;
				m_auction.regCount = 1;
				m_auction.hintItem = m_inv.slots[static_cast<size_t>(slot)].itemId;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::PriceHint);
				q.itemId = m_auction.hintItem;
				SendAuctionReq(q);
				m_auction.regPrice.text.clear();
				step(3);
			}
			break;
		case 3:
		{
			const std::string price = "30000";
			const size_t want = std::min(price.size(), static_cast<size_t>(std::max(0.f, m_fp.stepT - 0.8f) / 0.16f));
			if (m_auction.regPrice.text.size() != want) m_auction.regPrice.text = price.substr(0, want);
			if (m_fp.stepT > 2.6f && acked)
			{
				const InvSlot& sl = m_inv.slots[static_cast<size_t>(m_fp.slot)];
				m_auction.regDuration = 1;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Register);
				q.slot = static_cast<uint16_t>(m_fp.slot);
				q.count = 1;
				q.price = 30000;
				q.duration = 1;
				q.itemId = sl.itemId;
				SendAuctionReq(q);
				mark("auction-listed");
				step(4);
			}
			break;
		}
		case 4:
			if (acked && m_fp.stepT > 1.2f) { OpenAuction(2); step(5); }
			break;
		case 5:   // 팔릴 때까지(도우미가 산다)
			if (m_auction.settleCount >= 1 && m_fp.stepT > 1.f) { mark("auction-sold"); step(6); }
			else if (m_fp.stepT > 90.f) { std::printf("[fp] FAIL not sold\n"); step(9); }
			break;
		case 6:
			if (m_fp.stepT > 2.2f) { OpenAuction(3); step(7); }
			break;
		case 7:
			if (m_fp.stepT > 1.8f && !m_auction.settles.empty() && acked)
			{
				m_auction.claimAll = true;
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Claim);
				q.id = m_auction.settles.front().id;
				SendAuctionReq(q);
				mark("auction-claim");
				step(8);
			}
			else if (m_fp.stepT > 15.f) step(8);
			break;
		case 8:
			if (m_fp.stepT > 3.f) { m_auctionOpen = false; step(9); }
			break;
		default:
			if (m_fp.stepT > 0.8f) seg(8, "end", "", "");
			break;
		}
		break;
	}

	case 8:   // 끝: 시장 위로 카메라를 올려 마을을 한 번 더 — 마지막 자막
		if (m_fp.step == 0)
		{
			m_tourCam.on = true;
			m_tourCam.tile = Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
			m_tourCam.zoom = 1.f;
			m_tourCam.caption = "하르벤";
			m_tourCam.sub = "변경의 마지막 마을 데니차에서 — 접속부터 경매까지 한 번에";
			m_tourCam.captionAt = m_now;
			step(1);
		}
		else if (m_fp.step == 1)
		{
			const float u = EaseInOut(util::Clamp01(m_fp.stepT / 5.f));
			const Vector2 from{ static_cast<float>(m_predX), static_cast<float>(m_predY) };
			const Vector2 to{ TownMap::kTownCenter.x, TownMap::kTownCenter.y };
			m_tourCam.tile = Vector2{ from.x + (to.x - from.x) * u, from.y + (to.y - from.y) * u };
			m_tourCam.zoom = 1.f + (0.55f - 1.f) * u;
			if (m_fp.stepT > 6.5f)
			{
				std::printf("[fp] done rec=%.2f bossDown=%d guild=%u members=%d adena=%u\n", RecT(m_now), m_fp.bossDown ? 1 : 0, m_guild.id, m_guild.members, m_inv.adena);
				std::fflush(stdout);
				step(2);
				if (!m_opts.screenshot.empty()) m_opts.screenshotAfter = std::min(m_opts.screenshotAfter, m_runTime + 0.3f);
				else m_quit = true;
			}
		}
		break;
	default:
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 6 시나리오(RunScript에서) — fullplay:helper · crowd
// ─────────────────────────────────────────────────────────────────────────────
bool Game::RunScriptPhase6(float dt)
{
	(void)dt;
	const std::string& s = m_opts.script;
	if (s != "fullplay:helper" && s != "crowd")
		return false;
	auto elapsed = [this] { return m_scriptT - m_scriptMark; };
	auto next = [this](int step) { m_scriptStep = step; m_scriptMark = m_scriptT; std::printf("[p6] step %d t=%.1f\n", step, m_scriptT); std::fflush(stdout); };
	if (!m_hasSelf || m_scene != Scene::Game)
		return true;

	if (s == "fullplay:helper")
	{
		const bool acked = m_auction.pendingSeq == 0;
		switch (m_scriptStep)
		{
		case 0:
			if (m_scriptT > 2.f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, GuildRule::kHallNpcId }); next(1); }
			break;
		case 1:   // 회관 앞에서 기다린다(대화창은 닫고) — 전체 채팅에 "결사"가 나오면 귓속말
			if (m_dialogNpc != 0 && elapsed() > 2.f) CloseNpcWindows();
			if (m_fp.heard && !m_fp.heardFrom.empty() && elapsed() > 1.f && m_scriptT - m_fp.heardAt > 2.2f)
			{
				SendChatLine(chat::Target{ chat::Ch::WhisperOut, m_fp.heardFrom }, "저요! 마침 회관 앞에 있소. 불러 주시오.");
				std::printf("[fp-helper] whisper to=%s\n", m_fp.heardFrom.c_str());
				next(2);
			}
			break;
		case 2:
			if (m_guild.inviteGuild != 0 && m_now - m_guild.inviteAt > 1.6)
			{
				SendGuildReq(GuildOp::InviteReply, 1, m_guild.inviteGuild);
				m_guild.inviteGuild = 0;
				next(3);
			}
			break;
		case 3:
			if (m_guild.id != 0 && elapsed() > 4.5f)
			{
				SendChatLine(chat::Target{ chat::Ch::Guild, "" }, "불러 줘서 고맙소! 잘 부탁하오.");
				std::printf("[fp-helper] joined guild=%s\n", m_guild.name.c_str());
				next(4);
			}
			break;
		case 4:
			if (elapsed() > 5.f) { SetGoal(Goal{ GoalKind::Talk, ObjID{}, 0, 0, AuctionRule::kAuctioneerNpcId }); next(5); }
			break;
		case 5:
			if ((m_dialogNpc == AuctionRule::kAuctioneerNpcId && elapsed() > 1.f) || elapsed() > 60.f)
			{
				CloseNpcWindows();
				m_auction.cat = static_cast<uint8_t>(AuctionRule::Cat::Accessory);
				m_auction.sub = 0;
				OpenAuction(0);
				next(6);
			}
			break;
		case 6:   // 은 목걸이가 올라올 때까지 2초마다 다시 찾는다
		{
			const AuctionRow* found = nullptr;
			for (const auto& r : m_auction.rows)
				if (r.itemId == ItemTableData::Id::SilverPendant && (r.flags & 3) == 0) { found = &r; break; }
			if (found && elapsed() > 1.f)
			{
				m_p5.target = found->id;
				m_auction.confirmId = found->id;
				m_auction.confirmRow = *found;
				std::printf("[fp-helper] found listing=%u price=%u\n", found->id, found->price);
				next(7);
			}
			else if (elapsed() > 1.f && m_scriptT - m_p5.at > 2.f) { m_p5.at = m_scriptT; AuctionSearch(0); }
			if (elapsed() > 240.f) { std::printf("[fp-helper] FAIL nothing found\n"); next(20); }
			break;
		}
		case 7:
			if (elapsed() > 1.5f)
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Buy);
				q.id = m_p5.target;
				q.price = m_auction.confirmRow.price;
				SendAuctionReq(q);
				m_auction.confirmId = 0;
				next(8);
			}
			break;
		case 8:
			if (acked && elapsed() > 1.5f)
			{
				std::printf("[fp-helper] buy result=%d adena=%u\n", m_auction.lastResult, m_inv.adena);
				OpenAuction(3);
				next(9);
			}
			break;
		case 9:
			if (elapsed() > 2.f && !m_auction.settles.empty() && acked)
			{
				AUCTION_REQ_PACKET q{};
				q.op = static_cast<uint8_t>(AuctionOp::Claim);
				q.id = m_auction.settles.front().id;
				SendAuctionReq(q);
				next(10);
			}
			else if (elapsed() > 20.f) next(10);
			break;
		case 10:
			if (acked && elapsed() > 1.5f) { std::printf("[fp-helper] done\n"); std::fflush(stdout); next(11); }
			break;
		default:
			break;
		}
		return true;
	}

	// ── crowd: 분수 광장 남쪽에 서서 20초 ──
	switch (m_scriptStep)
	{
	case 0:
		if (m_scriptT > 1.f)
		{
			SetGoal(Goal{ GoalKind::Move, ObjID{}, static_cast<int>(TownMap::kPlaza.x), static_cast<int>(TownMap::kPlaza.y) + 3 });
			next(1);
		}
		break;
	case 1:
		if (m_goal.kind == GoalKind::None || elapsed() > 40.f)
		{
			const char* z = std::getenv("HARVEN_CROWD_ZOOM");
			m_cam.SetZoomFree(z ? static_cast<float>(std::atof(z)) : 0.9f);
			m_debugOverlay = true;
			next(2);
		}
		break;
	case 2:
	{
		const char* minEnv = std::getenv("HARVEN_CROWD_MIN");
		const int want = minEnv ? std::atoi(minEnv) : 28;
		int players = 0;
		for (const auto& [id, e] : m_ents)
			if (e.kind == EntityKind::Player && id != m_selfId) ++players;
		if ((players >= want && elapsed() > 3.f) || elapsed() > 90.f)
		{
			m_crowd = CrowdBench{};
			m_perfPhase = 1;
			std::printf("[crowd] start players=%d npcs=%zu\n", players, m_npcView.size());
			perf::PrintMem("crowd-start");
			next(3);
		}
		break;
	}
	case 3:
	{
		// 직전 프레임의 값(perf는 EndFrame에서 채운다) + 화면 안에 보이는 수
		m_crowd.work.push_back(perf::WorkMs(0));
		m_crowd.frame.push_back(perf::FrameMs(0));
		m_crowd.draws.push_back(static_cast<float>(std::max(0, perf::DrawCallsLast())));
		const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
		auto onScreen = [&](Vector2 tile)
		{
			const Vector2 p = GetWorldToScreen2D(TileToIso(tile), m_cam2d);
			return p.x >= -20.f && p.x <= W + 20.f && p.y >= -40.f && p.y <= H + 60.f;
		};
		int players = 0, playersIn = 0, npcsIn = 0;
		for (const auto& [id, e] : m_ents)
			if (e.kind == EntityKind::Player && id != m_selfId) { ++players; if (onScreen(e.pos)) ++playersIn; }
		for (const auto& [nid, v] : m_npcView)
			if (v.row && onScreen(v.pos)) ++npcsIn;
		m_crowd.maxPlayers = std::max(m_crowd.maxPlayers, players);
		m_crowd.maxOnScreen = std::max(m_crowd.maxOnScreen, playersIn + npcsIn);
		m_crowd.sumPlayersIn += playersIn;
		m_crowd.sumNpcsIn += npcsIn;
		if (elapsed() > 20.f)
		{
			const Stat w = Summ(m_crowd.work), f = Summ(m_crowd.frame), d = Summ(m_crowd.draws);
			const size_t n = m_crowd.work.size();
			int over33 = 0;
			for (float x : m_crowd.work) if (x > 33.f) ++over33;
			std::printf("[crowd] frames=%zu players=%d(화면 안 평균 %.1f) npcs=%zu(화면 안 평균 %.1f) 화면 안 최대 %d\n", n, m_crowd.maxPlayers,
				n ? static_cast<double>(m_crowd.sumPlayersIn) / n : 0.0, m_npcView.size(), n ? static_cast<double>(m_crowd.sumNpcsIn) / n : 0.0, m_crowd.maxOnScreen);
			std::printf("[crowd] work avg=%.1fms p95=%.1fms p99=%.1fms max=%.1fms over33=%d | frame avg=%.1fms p95=%.1fms max=%.1fms | draw avg=%.0f max=%.0f\n",
				w.avg, w.p95, w.p99, w.max, over33, f.avg, f.p95, f.max, d.avg, d.max);
			perf::PrintMem("crowd-end");
			m_perfPhase = 2;
			if (!m_opts.screenshot.empty()) m_opts.screenshotAfter = std::min(m_opts.screenshotAfter, m_runTime + 0.2f);
			next(4);
		}
		break;
	}
	default:
		break;
	}
	return true;
}
