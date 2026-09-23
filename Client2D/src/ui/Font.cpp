#include "ui/Font.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace ui
{
	FontSystem& Fonts()
	{
		static FontSystem instance;
		return instance;
	}

	void DecodeUtf8(std::string_view text, std::vector<int>& out)
	{
		out.clear();
		size_t i = 0;
		while (i < text.size())
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			int cp = '?';
			int len = 1;
			if (c < 0x80) { cp = c; }
			else if ((c >> 5) == 0x6 && i + 1 < text.size()) { cp = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F); len = 2; }
			else if ((c >> 4) == 0xE && i + 2 < text.size()) { cp = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F); len = 3; }
			else if ((c >> 3) == 0x1E && i + 3 < text.size()) { cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) | ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F); len = 4; }
			out.push_back(cp);
			i += static_cast<size_t>(len);
		}
	}

	void AppendUtf8(std::string& out, int cp)
	{
		if (cp < 0x80) out.push_back(static_cast<char>(cp));
		else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
		else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
		else { out.push_back(static_cast<char>(0xF0 | (cp >> 18))); out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))); out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
	}

	void PopUtf8(std::string& s)
	{
		if (s.empty()) return;
		size_t i = s.size() - 1;
		while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
			--i;
		s.erase(i);
	}

	bool FontSystem::Init(const std::string& path)
	{
		m_fontData = LoadFileData(path.c_str(), &m_fontDataSize);
		return m_fontData != nullptr;
	}

	void FontSystem::Shutdown()
	{
		for (auto& page : m_pages)
			UnloadTexture(page.tex);
		m_pages.clear();
		m_glyphs.clear();
		if (m_fontData)
			UnloadFileData(m_fontData);
		m_fontData = nullptr;
	}

	bool FontSystem::Allocate(int w, int h, int& outPage, int& outX, int& outY)
	{
		if (w > kPageSize - 2 || h > kPageSize - 2)
			return false;

		auto tryPage = [&](Page& page) -> bool
		{
			if (page.cursorX + w + 1 > kPageSize)
			{
				page.cursorX = 1;
				page.cursorY += page.rowH + 1;
				page.rowH = 0;
			}
			if (page.cursorY + h + 1 > kPageSize)
				return false;
			outX = page.cursorX;
			outY = page.cursorY;
			page.cursorX += w + 1;
			page.rowH = std::max(page.rowH, h);
			return true;
		};

		if (!m_pages.empty() && tryPage(m_pages.back()))
		{
			outPage = static_cast<int>(m_pages.size()) - 1;
			return true;
		}

		// 새 페이지(투명 RGBA). 글자 색은 그릴 때 tint로 준다.
		Page page;
		Image blank = GenImageColor(kPageSize, kPageSize, BLANK);
		page.tex = LoadTextureFromImage(blank);
		UnloadImage(blank);
		SetTextureFilter(page.tex, TEXTURE_FILTER_BILINEAR);
		m_pages.push_back(page);
		if (!tryPage(m_pages.back()))
			return false;
		outPage = static_cast<int>(m_pages.size()) - 1;
		return true;
	}

	const FontSystem::Glyph& FontSystem::GetGlyph(int size, int codepoint)
	{
		const uint64_t key = (static_cast<uint64_t>(size) << 32) | static_cast<uint32_t>(codepoint);
		auto it = m_glyphs.find(key);
		if (it != m_glyphs.end())
			return it->second;

		Glyph glyph;
		int cp = codepoint;
		GlyphInfo* info = LoadFontData(m_fontData, m_fontDataSize, size, &cp, 1, FONT_DEFAULT);
		if (info != nullptr)
		{
			glyph.offX = static_cast<float>(info->offsetX);
			glyph.offY = static_cast<float>(info->offsetY);
			glyph.advance = static_cast<float>(info->advanceX);
			if (glyph.advance <= 0)
				glyph.advance = static_cast<float>(info->image.width);

			const int w = info->image.width;
			const int h = info->image.height;
			int page = -1, x = 0, y = 0;
			if (w > 0 && h > 0 && info->image.data != nullptr && Allocate(w, h, page, x, y))
			{
				// GRAYSCALE → RGBA(흰색 + 커버리지 알파)
				std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
				const unsigned char* src = static_cast<const unsigned char*>(info->image.data);
				for (int i = 0; i < w * h; ++i)
				{
					rgba[i * 4 + 0] = 255;
					rgba[i * 4 + 1] = 255;
					rgba[i * 4 + 2] = 255;
					rgba[i * 4 + 3] = src[i];
				}
				UpdateTextureRec(m_pages[page].tex, Rectangle{ (float)x, (float)y, (float)w, (float)h }, rgba.data());
				glyph.page = page;
				glyph.src = Rectangle{ (float)x, (float)y, (float)w, (float)h };
			}
			UnloadFontData(info, 1);
		}

		// 폰트에 없는 글자는 '?'로 대체(무한 재귀 방지)
		if (glyph.advance <= 0 && codepoint != '?')
			return m_glyphs[key] = GetGlyph(size, '?');

		return m_glyphs[key] = glyph;
	}

	Vector2 FontSystem::Measure(std::string_view text, float size, float spacing)
	{
		if (!m_fontData) return { 0, 0 };
		const int isize = std::max(4, static_cast<int>(std::lround(size)));
		static thread_local std::vector<int> cps;
		DecodeUtf8(text, cps);
		float w = 0.f;
		for (int cp : cps)
			w += GetGlyph(isize, cp).advance + spacing;
		if (!cps.empty()) w -= spacing;
		return { w, static_cast<float>(isize) };
	}

	float FontSystem::DrawRun(std::string_view text, Vector2 pos, int isize, float spacing, Color color)
	{
		static thread_local std::vector<int> cps;
		DecodeUtf8(text, cps);
		float x = pos.x;
		for (int cp : cps)
		{
			const Glyph& g = GetGlyph(isize, cp);
			if (g.page >= 0)
			{
				const Rectangle dst{ std::floor(x + g.offX), std::floor(pos.y + g.offY), g.src.width, g.src.height };
				DrawTexturePro(m_pages[g.page].tex, g.src, dst, Vector2{ 0, 0 }, 0.f, color);
			}
			x += g.advance + spacing;
		}
		return x - pos.x;
	}

	float FontSystem::Draw(std::string_view text, Vector2 pos, const TextStyle& style)
	{
		if (!m_fontData || text.empty()) return 0.f;
		const int isize = std::max(4, static_cast<int>(std::lround(style.size)));

		if (style.shadow > 0.f)
			DrawRun(text, Vector2{ pos.x + style.shadow, pos.y + style.shadow }, isize, style.spacing, style.shadowColor);

		if (style.outline > 0.f)
		{
			const float o = style.outline;
			static constexpr float dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{.7f,.7f},{-.7f,.7f},{.7f,-.7f},{-.7f,-.7f} };
			for (const auto& d : dirs)
				DrawRun(text, Vector2{ pos.x + d[0] * o, pos.y + d[1] * o }, isize, style.spacing, style.outlineColor);
		}

		return DrawRun(text, pos, isize, style.spacing, style.color);
	}

	float FontSystem::Draw(std::string_view text, Vector2 pos, float size, Color color)
	{
		TextStyle style;
		style.size = size;
		style.color = color;
		return Draw(text, pos, style);
	}

	void FontSystem::DrawCentered(std::string_view text, Vector2 center, const TextStyle& style)
	{
		const Vector2 m = Measure(text, style.size, style.spacing);
		Draw(text, Vector2{ center.x - m.x * 0.5f, center.y - m.y * 0.5f }, style);
	}
}
