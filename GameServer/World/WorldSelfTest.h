#pragma once

class WorldRegistry;

// Sector / Zone 수학 전수 검사. 실패 건수를 돌려준다.
//
// 3번 단계의 검증 수단이다. 원래는 STRESS_TEST 봇으로 확인하려 했는데 봇의
// 3D 전환을 뒤로 미뤘고, 어차피 이 쪽이 강하다 — 봇은 지나간 자리만 보지만
// 여기서는 격자를 전수로 훑는다. 축이 바뀌거나 경계가 한 칸 어긋나도 월드가
// 정사각형이면 크래시 없이 조용히 틀리는데, 그런 것을 잡으려는 검사다.
namespace WorldSelfTest
{
	int Run(const WorldRegistry& world);
}
