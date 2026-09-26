#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// 마을의 작은 생명(Phase 4 자기 검토 — "넓어진 마을이 비어 보인다") — 클라 전용 연출, 서버와 관계없다.
//
//   비둘기 떼: 분수 광장 · 남문 광장 · 시장 거리 · 결사 회관 단 · 여관 골목에 5~8마리씩. 판석을 쪼고 종종걸음 치다가
//             움직이는 사람 · 짐승이 2칸 안에 들어오면(서 있는 사람은 1칸) 가까운 새부터 차례로 푸드덕 날아올라
//             (깃털 몇 장 · 날개 소리) 광장 위를 한두 바퀴 돌고, 그 사람이 아직 있으면 다른 자리로 가서 내려앉는다.
//             밤에는 둥지로 날아가 보이지 않고 아침에 날아 들어온다.
//   물고기:   개울(~) 칸에서 가끔 튀어 올랐다 떨어진다(포물선 · 물 고리 · 물방울 · 퐁당). 밤에 더 자주 —
//             어부 벨코의 말("요즘은 밤에만 올라온다오")과 맞춘다.
//
// 위치는 게임의 약속대로 바닥 평면 칸 좌표, 높이는 z 단위(1 = 36px, 층 하나 = 0.75z). 깊이는 바닥 위치의 X+Y.
// ─────────────────────────────────────────────────────────────────────────────
#include "raylib.h"

#include <vector>

namespace critters
{
	enum class MoverKind : unsigned char { Person, Player, Animal };   // 주민(NPC) · 플레이어 · 몬스터 / 짐승(개 · 고양이 · 닭)
	struct Mover
	{
		Vector2 tile;      // 연속 칸 좌표
		float speed;       // 칸/초(0 = 서 있음)
		MoverKind kind;
	};

	// 마을 자료에서 자리를 만든다(한 번). 마을이 없는 빌드면 아무것도 하지 않는다.
	void Init();

	// 매 프레임: movers = 주변의 사람 · 짐승(나 · 다른 플레이어 · NPC · 몬스터), listener = 소리를 듣는 자리(내 캐릭터 · 투어 카메라),
	// 비둘기는 천천히 걷는 주민에게는 몇 걸음 비켜 주기만 하고, 달려오는 플레이어 · 몬스터 · 짐승에게 떼 전체가 날아오른다.
	// view = 화면 가운데 칸(물고기는 보이는 곳에서만 뛴다), night 0 낮 ~ 1 한밤
	void Update(float dt, const std::vector<Mover>& movers, Vector2 listener, Vector2 view, float night);

	// 바닥 데칼 패스(드리우기 켠 상태): 나는 새의 그림자 · 물 고리
	void DrawGround();
	// 불투명 패스(깊이 쓰기): 비둘기(땅 · 하늘) · 뛰어오른 물고기
	void DrawWorld();

	// 점검 · 스크립트용
	struct Stats { int birds = 0, flying = 0, flocks = 0, startles = 0, landings = 0, fishJumps = 0; };
	[[nodiscard]] Stats GetStats();
	// 가장 가까운 땅에 앉은 비둘기 떼 번호 — 없으면 -1
	[[nodiscard]] int NearestFlock(Vector2 tile);
	struct FlockInfo { Vector2 center{ -1.f, -1.f }; int state = -1; int startles = 0; int landings = 0; int flying = 0; };   // state 0 땅 · 1 선회 · 2 내려앉기 · 3 둥지로 · 4 없음
	[[nodiscard]] FlockInfo Flock(int index);
}
