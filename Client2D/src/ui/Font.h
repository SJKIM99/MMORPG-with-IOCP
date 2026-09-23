#pragma once

#include "raylib.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 한글 동적 글리프 아틀라스.
//
// 한글 완성형 11,172자를 미리 전부 굽는 대신, 화면에 처음 등장하는 글자만 그때그때
// 래스터라이즈해서 아틀라스 페이지에 붙인다(채팅 입력처럼 어떤 글자가 나올지 모르는
// 경우에도 로딩 시간/메모리 부담이 없다). 크기(px)별로 캐시를 따로 둔다.
// ─────────────────────────────────────────────────────────────────────────────
namespace ui
{
	struct TextStyle
	{
		float size    = 16.f;
		Color color   = WHITE;
		float spacing = 0.f;
		// 외곽선(0 = 없음). 이름표/데미지 숫자처럼 배경 위에서 읽혀야 하는 글자에 쓴다.
		float outline = 0.f;
		Color outlineColor = { 0, 0, 0, 200 };
		// 그림자(0 = 없음)
		float shadow = 0.f;
		Color shadowColor = { 0, 0, 0, 140 };
	};

	class FontSystem
	{
	public:
		bool Init(const std::string& path);
		void Shutdown();

		Vector2 Measure(std::string_view text, float size, float spacing = 0.f);
		// 좌상단 기준. 반환값은 그린 너비.
		float Draw(std::string_view text, Vector2 pos, const TextStyle& style);
		float Draw(std::string_view text, Vector2 pos, float size, Color color);
		// 가운데 정렬(pos = 중심점)
		void DrawCentered(std::string_view text, Vector2 center, const TextStyle& style);

		[[nodiscard]] bool IsReady() const noexcept { return m_fontData != nullptr; }

	private:
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

		const Glyph& GetGlyph(int size, int codepoint);
		bool Allocate(int w, int h, int& outPage, int& outX, int& outY);
		float DrawRun(std::string_view text, Vector2 pos, int isize, float spacing, Color color);

		static constexpr int kPageSize = 1024;
		unsigned char* m_fontData = nullptr;
		int m_fontDataSize = 0;
		std::vector<Page> m_pages;
		std::unordered_map<uint64_t, Glyph> m_glyphs;  // (size << 32) | codepoint
	};

	FontSystem& Fonts();

	// UTF-8 → 코드포인트 목록
	void DecodeUtf8(std::string_view text, std::vector<int>& out);
	// 코드포인트 하나를 UTF-8로 붙인다
	void AppendUtf8(std::string& out, int codepoint);
	// 마지막 글자 하나 지우기(UTF-8 경계 존중)
	void PopUtf8(std::string& s);
}
