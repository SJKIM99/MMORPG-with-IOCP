#pragma once

#include "raylib.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 타이포그래피 — 여러 글꼴 계열(Face) + 동적 글리프 아틀라스.
//
//  · 계열마다 역할이 정해져 있다(docs/DESIGN_SYSTEM.md 2장).
//      Body(본문 · UI) · Strong(이름 · 제목줄 · 버튼) · Bold(강조)
//      Display(창 제목 · 장 제목 · 보스 이름) · Serif(이야기 · 장소 문구)
//      Number(HUD 수치) · NumberBold(피해 숫자) · Latin(영문 로고) · Symbol(기호 폴백)
//  · 계열에 없는 글자는 정해진 순서로 다른 계열에서 빌려 온다(폴백). 예: 숫자 글꼴에
//    한글이 나오면 Pretendard Bold로, 기호는 나눔스퀘어라운드로.
//  · 크기(size)는 "줄 상자 높이"다. 계열마다 글자가 차지하는 비율이 달라도 같은 size면
//    한글 높이와 세로 중심이 맞도록 계열별 배율 · 기준선을 보정한다.
//  · 한글 완성형 11,172자를 미리 굽지 않고, 처음 나온 글자만 그때 래스터라이즈한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace ui
{
	enum class Face : uint8_t
	{
		Body,        // Pretendard Medium
		Strong,      // Pretendard SemiBold
		Bold,        // Pretendard Bold
		Display,     // 고운바탕 Bold
		Serif,       // 고운바탕 Regular
		Number,      // Barlow Condensed SemiBold(숫자는 고정폭으로 맞춘다)
		NumberBold,  // Barlow Condensed ExtraBold
		Latin,       // Marcellus
		Symbol,      // 나눔스퀘어라운드 Bold(기호 폴백)
		Count,
		Inherit = 0xFF,   // TextStyle 기본값: 지금 PushFace된 계열을 쓴다
	};

	struct TextStyle
	{
		float size    = 16.f;
		Color color   = WHITE;
		float spacing = 0.f;     // 자간(px)
		// 외곽선(0 = 없음). 이름표/데미지 숫자처럼 배경 위에서 읽혀야 하는 글자에 쓴다.
		float outline = 0.f;
		Color outlineColor = { 0, 0, 0, 200 };
		// 그림자(0 = 없음)
		float shadow = 0.f;
		Color shadowColor = { 0, 0, 0, 140 };
		Face  face = Face::Inherit;
	};

	class FontSystem
	{
	public:
		FontSystem();
		~FontSystem();
		FontSystem(const FontSystem&) = delete;
		FontSystem& operator=(const FontSystem&) = delete;

		// dir = 글꼴 폴더(assets/fonts). 읽은 글꼴이 하나도 없으면 false.
		bool Init(const std::string& dir);
		void Shutdown();

		// face를 주지 않으면 지금 계열(PushFace 스택의 맨 위)
		Vector2 Measure(std::string_view text, float size, float spacing = 0.f, Face face = Face::Inherit);
		// 좌상단 기준. 반환값은 그린 너비.
		float Draw(std::string_view text, Vector2 pos, const TextStyle& style);
		float Draw(std::string_view text, Vector2 pos, float size, Color color);
		// 가운데 정렬(pos = 중심점)
		void DrawCentered(std::string_view text, Vector2 center, const TextStyle& style);

		// 계열 스택 — ui::Text 같은 편의 함수가 이 계열로 그린다(ImGui의 PushFont처럼).
		void PushFace(Face f);
		void PopFace();
		[[nodiscard]] Face CurrentFace() const;

		// UI 배율(Phase 3-A): 그리는 곳이 물리 = off + 지역 × k로 확대돼 있으면 글자를 size × k로 래스터라이즈해
		// 1/k로 찍는다 → 행렬이 다시 k배 하면 물리 픽셀 1:1(흐려지지 않는다). Measure도 같은 크기로 잰다.
		void SetRenderXform(Vector2 off, float k) { m_xOff = off; m_xK = k > 0.01f ? k : 1.f; }

		[[nodiscard]] bool IsReady() const noexcept { return m_ready; }

	private:
		struct FaceData;
		struct Glyph
		{
			Rectangle src{};
			float offX = 0, offY = 0, advance = 0;
			int page = -1;
		};
		struct Page
		{
			Texture2D tex{};
			int cursorX = 1, cursorY = 1, rowH = 0;
		};

		const Glyph& GetGlyph(Face face, int size, int codepoint);
		bool Rasterize(int faceIndex, int size, int codepoint, Glyph& out);
		bool Allocate(int w, int h, int& outPage, int& outX, int& outY);
		float DrawRun(std::string_view text, Vector2 pos, Face face, int isize, float spacing, Color color);
		[[nodiscard]] Face Resolve(Face f) const;

		static constexpr int kPageSize = 1024;
		std::vector<FaceData> m_faces;
		std::vector<Page> m_pages;
		std::unordered_map<uint64_t, Glyph> m_glyphs;  // (face << 48) | (size << 32) | codepoint
		std::vector<Face> m_faceStack;
		bool m_ready = false;
		Vector2 m_xOff{ 0, 0 };
		float m_xK = 1.f;
	};

	FontSystem& Fonts();

	// 계열을 잠깐 바꾸는 범위 객체: { ui::FaceScope f(ui::Face::Display); ui::Text(...); }
	struct FaceScope
	{
		explicit FaceScope(Face f) { Fonts().PushFace(f); }
		~FaceScope() { Fonts().PopFace(); }
		FaceScope(const FaceScope&) = delete;
		FaceScope& operator=(const FaceScope&) = delete;
	};

	// UTF-8 → 코드포인트 목록
	void DecodeUtf8(std::string_view text, std::vector<int>& out);
	// 코드포인트 하나를 UTF-8로 붙인다
	void AppendUtf8(std::string& out, int codepoint);
	// 마지막 글자 하나 지우기(UTF-8 경계 존중)
	void PopUtf8(std::string& s);
}
