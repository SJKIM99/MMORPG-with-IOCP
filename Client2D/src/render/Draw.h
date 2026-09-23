#pragma once

#include "raylib.h"
#include "core/Util.h"

// ─────────────────────────────────────────────────────────────────────────────
// 아이소메트릭 월드 드로잉 기본기.
//
// 좌표계: 타일 (X, Y), 높이 Z(1 = 타일 한 변 정도). 화면(아이소 px)으로는
//   sx = (X - Y) * 32,  sy = (X + Y) * 16 - Z * kUnitZ
// 타일 (tx, ty)의 중심이 정수 좌표다.
//
// 깊이: 오브젝트끼리의 앞뒤는 깊이 버퍼로 판정한다. 등각 투영에서는 같은 화면 픽셀로
// 겹치는 두 점 중 (X + Y)가 큰 쪽이 항상 카메라에 가깝다(높이와 무관 — 광선 위에서
// X+Y가 단조 증가). 그래서 모든 정점의 깊이를 X+Y 하나로 정하면
//   · 캐릭터(발 위치를 지나는 수직 평면 위의 스프라이트)
//   · 여러 칸을 차지하는 건물(실제 3D 면)
// 이 섞여도 픽셀 단위로 정확히 가려진다. 한 줄 정렬(painter's)로는 큰 건물 옆을
// 지나가는 캐릭터가 잘못 그려지는 문제를 피할 수 없어서 이 방식을 택했다.
// ─────────────────────────────────────────────────────────────────────────────
namespace gfx
{
	inline constexpr float kHalfW = 32.f;
	inline constexpr float kHalfH = 16.f;
	inline constexpr float kUnitZ = 36.f;

	inline Vector2 IsoPx(float X, float Y, float Z = 0.f)
	{
		return Vector2{ (X - Y) * kHalfW, (X + Y) * kHalfH - Z * kUnitZ };
	}
	// 아이소 px(Z=0 바닥) → 연속 타일 좌표
	inline Vector2 IsoToTile(Vector2 p)
	{
		const float a = p.x / kHalfW, b = p.y / kHalfH;
		return Vector2{ (a + b) * 0.5f, (b - a) * 0.5f };
	}

	// ── 프레임 상태 ──
	void BeginWorld(float cameraDepthXY, float zoom);   // 깊이 기준점(카메라가 보는 X+Y)
	float DepthOf(float xPlusY);                         // X+Y → 정점 z
	float Zoom();

	// 현재 "스프라이트" 깊이/색 변조 — 캐릭터 한 명을 그리는 동안 고정된다.
	void SetDepth(float xPlusY);
	void SetDepthRaw(float z);
	float CurrentZ();
	void SetTint(Color tint);           // 곱하기
	void SetFlash(float amount);        // 흰색 쪽으로 섞기(피격 번쩍임)
	void SetFlashColor(Color c);
	void SetAlpha(float alpha);
	void ResetMod();
	Color Mod(Color c);                 // 현재 변조 적용

	// ── 렌더 상태(배치를 비우고 바꾼다) ──
	void Flush();
	void DepthTest(bool on);
	void DepthWrite(bool on);
	void Additive(bool on);
	// 깊이 비교를 GREATER로(가려진 부분에만 그리기 — 실루엣). GlExtra.cpp
	void DepthGreater(bool on);

	// ── 2D 도형(현재 깊이) ──
	void Tri(Vector2 a, Vector2 b, Vector2 c, Color col);
	void Tri3(Vector2 a, Vector2 b, Vector2 c, Color ca, Color cb, Color cc);
	void Quad(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color col);
	void Quad4(Vector2 a, Vector2 b, Vector2 c, Vector2 d, Color ca, Color cb, Color cc, Color cd);
	void Circle(Vector2 c, float r, Color col);
	void CircleGrad(Vector2 c, float r, Color inner, Color outer);
	void Ellipse(Vector2 c, float rx, float ry, Color col);
	void EllipseGrad(Vector2 c, float rx, float ry, Color inner, Color outer);
	void EllipseArc(Vector2 c, float rx, float ry, float a0, float a1, float thickness, Color col);
	void Ring(Vector2 c, float r0, float r1, Color col, float a0 = 0.f, float a1 = util::kPi * 2.f);
	void RingGrad(Vector2 c, float r0, float r1, Color inner, Color outer);
	void Line(Vector2 a, Vector2 b, float w, Color col);
	void LineGrad(Vector2 a, Vector2 b, float w0, float w1, Color ca, Color cb);
	void Capsule(Vector2 a, Vector2 b, float r, Color col);
	void Poly(const Vector2* pts, int n, Color col);               // 볼록 다각형(부채꼴 분할)
	void PolyGrad(const Vector2* pts, int n, Vector2 center, Color centerCol, Color edgeCol);
	void Star(Vector2 c, float r0, float r1, int points, float rot, Color col);

	// ── 3D 면(정점마다 깊이) ──
	struct P3 { float x, y, z; };
	void Face(P3 a, P3 b, P3 c, P3 d, Color col);
	void Face4(P3 a, P3 b, P3 c, P3 d, Color ca, Color cb, Color cc, Color cd);
	void FaceTri(P3 a, P3 b, P3 c, Color col);
	// 축 정렬 상자: 보이는 세 면(윗면, 남(+Y)면, 동(+X)면)만 그린다.
	void Box(float x0, float y0, float x1, float y1, float z0, float z1, Color top, Color south, Color east);
	// 바닥 타일 마름모(데칼용, 현재 깊이)
	void Diamond(float X, float Y, float scale, Color col);

	// ── 텍스처 쿼드(현재 깊이) ──
	void TexQuad(const Texture2D& tex, Vector2 center, Vector2 size, float rot, Color col);
	void TexQuadSrc(const Texture2D& tex, Rectangle src, Vector2 center, Vector2 size, float rot, Color col);
}
