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
	[[nodiscard]] float Alpha();        // 현재 알파(SetAlpha)
	[[nodiscard]] Color Tint();         // 현재 틴트(SetTint)
	[[nodiscard]] float Flash();        // 현재 번쩍임 양
	[[nodiscard]] Color FlashColor();

	// ── 렌더 상태(배치를 비우고 바꾼다) ──
	void Flush();
	void DepthTest(bool on);
	void DepthWrite(bool on);
	[[nodiscard]] bool DepthWriteOn();
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
	// 텍스처를 입힌 면(건물 벽 · 지붕 · 성벽). bl→br가 텍스처 가로(u), tl→bl이 세로(v, 위가 0).
	// repU · repV = 면 전체에 텍스처가 몇 번 반복되는가. 반복마다 작은 면으로 나눠 텍스처 좌표를 0~1에 둔다
	// (월드 셰이더가 텍스처 좌표의 정수부를 번쩍임 · 알파 테스트 표시로 쓰기 때문).
	void FaceTex(const Texture2D& tex, P3 bl, P3 br, P3 tr, P3 tl, float repU, float repV, Color col);
	// 텍스처 상자(윗면 · 남면 · 동면). 음영은 면마다 곱하는 색으로.
	void BoxTex(const Texture2D& tex, float x0, float y0, float x1, float y1, float z0, float z1, float texUnit, Color top, Color south, Color east);
	// 바닥 타일 마름모(데칼용, 현재 깊이)
	void Diamond(float X, float Y, float scale, Color col);

	// ── 텍스처 쿼드(현재 깊이) ──
	void TexQuad(const Texture2D& tex, Vector2 center, Vector2 size, float rot, Color col);
	void TexQuadSrc(const Texture2D& tex, Rectangle src, Vector2 center, Vector2 size, float rot, Color col);
	// 바닥에 눕힌 텍스처(바닥 평면에서 rot만큼 돌린 뒤 세로를 squash배 — 아이소 바닥의 원 = 가로 2 : 세로 1)
	void TexQuadGround(const Texture2D& tex, Vector2 center, Vector2 size, float rot, float squash, Color col);

	// ── 그린 스프라이트(캐릭터 · 소품) ──
	// 발(anchor)을 base에 놓고, 세로 띠 여섯 칸짜리 메시로 그린다 — 윗부분만 기울이기(lean) · 눌림(squash) ·
	// 늘림(stretch) · 발 기준 회전(rot)을 비트는 데 쓴다. 현재 깊이 · 틴트 · 알파 · 번쩍임(SetFlash)을 따른다.
	// 번쩍임은 정점 색이 아니라 텍스처 좌표의 정수부에 실어 월드 셰이더(SpriteShader)가 풀어 쓴다.
	struct SpriteXform
	{
		float lean = 0.f;       // px: 머리가 앞(+) · 뒤(-)로 쏠리는 양(발은 고정)
		float squash = 1.f;     // 세로 배율(발 기준)
		float stretch = 1.f;    // 가로 배율(음수면 뒤집힌 것처럼 — 돌기 연출)
		float rot = 0.f;        // 발 기준 회전(라디안, + = 앞으로 넘어짐)
		Vector2 offset{};       // 발 위치 이동(px, 화면)
		bool flip = false;      // 왼쪽을 보게(그림은 오른쪽을 보고 있다)
		bool cutout = true;     // 알파 테스트(깊이를 쓰는 본 그림). 잔상 · 몸빛처럼 깊이를 안 쓰는 겹은 false — 가장자리가 부드럽다
	};
	void SpriteMesh(const Texture2D& tex, Rectangle src, Vector2 anchor, Vector2 base, float scale, const SpriteXform& xf);
	// 번쩍임 색 팔레트(셰이더 uniform과 같은 순서) — SetFlashColor를 가장 가까운 색으로 옮긴다
	inline constexpr int kFlashPaletteSize = 8;
	const float* FlashPalette();   // rgb * 8
}
