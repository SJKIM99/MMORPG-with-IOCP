#pragma once

#include <cmath>

// 3D 좌표. CLAUDE.md 3장의 규약을 그대로 옮긴 것이다.
//
//   단위      1 unit = 1 meter
//   좌표계    오른손, Y-up, -Z 가 forward (Godot 4 기본과 동일)
//   지면 평면 XZ. 높이는 Y.
//   회전      서버는 yaw(Y축 회전) float 하나만 관리한다. 쿼터니언을 넣지 않는다.
//
// **Sector 분할은 XZ 평면 2D 격자로만 한다. Y는 분할하지 않는다.**
// 그래서 거리 비교는 대부분 2D(XZ)다 — 시야·어그로·사거리 모두 높이를 무시한다.
// 3D 거리가 필요한 곳이 생기면 그때 따로 만든다. 지금 넣으면 어느 쪽을 써야
// 하는지 헷갈리기만 한다.
//
// 2D 시절의 (x, y) 는 **높이가 아니라 지면의 두 번째 축**이었다. 그대로 두면
// 새 y(높이)와 이름이 겹쳐 조용히 틀린다. 그래서 옛 y 는 전부 z 로 옮겼고
// Transform 에서 옛 GetY() 를 **삭제**해 모든 호출부가 컴파일 에러로 드러나게 했다.
// (새 GetY() 는 높이다 — 이름은 같지만 뜻이 다르므로 한 번은 반드시 눈으로 본다.)
struct Vec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr Vec3() noexcept = default;
	constexpr Vec3(float inX, float inY, float inZ) noexcept : x(inX), y(inY), z(inZ) {}

	constexpr bool operator==(const Vec3&) const noexcept = default;

	constexpr Vec3 operator+(const Vec3& rhs) const noexcept
	{
		return Vec3{ x + rhs.x, y + rhs.y, z + rhs.z };
	}

	constexpr Vec3 operator-(const Vec3& rhs) const noexcept
	{
		return Vec3{ x - rhs.x, y - rhs.y, z - rhs.z };
	}

	constexpr Vec3 operator*(float scalar) const noexcept
	{
		return Vec3{ x * scalar, y * scalar, z * scalar };
	}

	// ── 지면(XZ) 거리 ────────────────────────────────────────
	// 제곱 거리를 기본으로 둔다. 비교만 할 때 sqrt 를 부르지 않기 위함이다.
	[[nodiscard]] constexpr float DistanceSq2D(const Vec3& other) const noexcept
	{
		const float dx = x - other.x;
		const float dz = z - other.z;
		return dx * dx + dz * dz;
	}

	[[nodiscard]] float Distance2D(const Vec3& other) const noexcept
	{
		return std::sqrt(DistanceSq2D(other));
	}

	[[nodiscard]] constexpr bool IsWithin2D(const Vec3& other, float radius) const noexcept
	{
		return DistanceSq2D(other) <= radius * radius;
	}
};

// **2D 시절 세계로 나가는 유일한 문.**
//
// 아직 옛 것으로 남아 있는 두 곳이 정수 좌표를 요구한다:
//   1. 패킷 — SUBJECT_MOVE_NFY 등이 short x, y 를 보낸다 (교체는 Week 1 의 5번)
//   2. 타일 격자 — A* / Collision / 인접 판정 (교체는 Week 2 의 Recast)
// 옛 월드에서는 1타일 = 1좌표 = 패킷 단위라 셋이 같은 값이었다.
//
// **암묵 변환(C4244)에 맡기지 않고 반드시 이 함수를 거친다.** 122군데가
// 조용히 잘리고 있었는데, 그중 어디가 의도한 절단이고 어디가 실수인지
// 컴파일러는 구분해 주지 않는다. 명시적으로 부르면 나중에 이 함수를 지울 때
// 고쳐야 할 자리가 그대로 목록이 된다.
[[nodiscard]] inline short ToLegacyTile(float meters) noexcept
{
	return static_cast<short>(std::lround(meters));
}
