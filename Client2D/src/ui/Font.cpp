#include "ui/Font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// stb_truetype(공개 도메인, v1.26)를 이 번역 단위 안에서만 쓴다(STBTT_STATIC).
// raylib도 내부에 같은 라이브러리를 static으로 넣어 두었으므로 이름이 부딪히지 않는다.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ui
{
	namespace
	{
		// 크기 기준: 예전 본문 글꼴(나눔스퀘어라운드)의 줄 상자. size px 줄 상자에서 em은
		// size × 0.905, 기준선은 위에서 size × 0.769. 계열이 바뀌어도 화면 배치가 그대로다.
		constexpr float kEmRatio = 1000.f / 1105.f;
		constexpr float kBaselineRatio = 850.f / 1105.f;

		struct FaceSpec
		{
			const char* file;
			float scale;        // em 배율(한글 · 숫자 높이를 본문과 맞춘다)
			float shift;        // 기준선 보정(기준 em 단위, + = 아래로) — 세로 중심을 본문과 맞춘다
			float gamma;        // 커버리지 곡선(< 1 = 조금 더 또렷하게)
			bool  tabular;      // 숫자를 고정폭으로
			std::array<Face, 5> chain;   // 폴백 순서(자기 자신 포함, Count = 끝)
		};

		constexpr Face N = Face::Count;
		// Face 열거 순서와 같아야 한다.
		constexpr FaceSpec kSpecs[] =
		{
			{ "Pretendard-Medium.ttf",        1.00f, 0.000f, 0.85f, false, { Face::Body, Face::Symbol, N, N, N } },
			{ "Pretendard-SemiBold.ttf",      1.00f, 0.000f, 0.88f, false, { Face::Strong, Face::Body, Face::Symbol, N, N } },
			{ "Pretendard-Bold.ttf",          1.00f, 0.000f, 0.90f, false, { Face::Bold, Face::Strong, Face::Body, Face::Symbol, N } },
			{ "GowunBatang-Bold.ttf",         0.97f, 0.060f, 0.85f, false, { Face::Display, Face::Strong, Face::Body, Face::Symbol, N } },
			{ "GowunBatang-Regular.ttf",      0.97f, 0.060f, 0.80f, false, { Face::Serif, Face::Display, Face::Body, Face::Symbol, N } },
			{ "BarlowCondensed-SemiBold.ttf", 1.12f, 0.046f, 0.90f, true,  { Face::Number, Face::Bold, Face::Body, Face::Symbol, N } },
			{ "BarlowCondensed-ExtraBold.ttf",1.12f, 0.046f, 0.90f, true,  { Face::NumberBold, Face::Number, Face::Bold, Face::Body, Face::Symbol } },
			{ "Marcellus-Regular.ttf",        1.00f, 0.000f, 0.85f, false, { Face::Latin, Face::Display, Face::Body, Face::Symbol, N } },
			{ "NanumSquareRoundB.ttf",        1.00f, 0.000f, 0.90f, false, { Face::Symbol, Face::Body, N, N, N } },
		};
		static_assert(sizeof(kSpecs) / sizeof(kSpecs[0]) == static_cast<size_t>(Face::Count), "Face와 kSpecs 순서/개수가 맞아야 한다");
	}

	struct FontSystem::FaceData
	{
		unsigned char* data = nullptr;
		int dataSize = 0;
		stbtt_fontinfo info{};
		bool ok = false;
		int digitAdvance = 0;              // 숫자 중 가장 넓은 전진 폭(글꼴 단위)
		std::array<unsigned char, 256> lut{};
	};

	FontSystem::FontSystem() = default;
	FontSystem::~FontSystem() = default;

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

	bool FontSystem::Init(const std::string& dir)
	{
		m_faces.assign(static_cast<size_t>(Face::Count), FaceData{});
		bool any = false;
		for (size_t i = 0; i < m_faces.size(); ++i)
		{
			FaceData& f = m_faces[i];
			const FaceSpec& spec = kSpecs[i];
			const std::string path = dir + "/" + spec.file;
			if (!FileExists(path.c_str()))
				continue;
			f.data = LoadFileData(path.c_str(), &f.dataSize);
			if (f.data == nullptr)
				continue;
			const int offset = stbtt_GetFontOffsetForIndex(f.data, 0);
			if (offset < 0 || !stbtt_InitFont(&f.info, f.data, offset))
			{
				UnloadFileData(f.data);
				f.data = nullptr;
				continue;
			}
			f.ok = true;
			any = true;
			for (int d = '0'; d <= '9'; ++d)
			{
				int adv = 0, lsb = 0;
				stbtt_GetCodepointHMetrics(&f.info, d, &adv, &lsb);
				f.digitAdvance = std::max(f.digitAdvance, adv);
			}
			for (int a = 0; a < 256; ++a)
				f.lut[a] = static_cast<unsigned char>(std::lround(255.0 * std::pow(a / 255.0, spec.gamma)));
		}
		m_faceStack.clear();
		m_ready = any;
		return any;
	}

	void FontSystem::Shutdown()
	{
		for (auto& page : m_pages)
			UnloadTexture(page.tex);
		m_pages.clear();
		m_glyphs.clear();
		for (auto& f : m_faces)
			if (f.data) UnloadFileData(f.data);
		m_faces.clear();
		m_ready = false;
	}

	void FontSystem::PushFace(Face f) { m_faceStack.push_back(f == Face::Inherit ? CurrentFace() : f); }
	void FontSystem::PopFace() { if (!m_faceStack.empty()) m_faceStack.pop_back(); }
	Face FontSystem::CurrentFace() const { return m_faceStack.empty() ? Face::Body : m_faceStack.back(); }
	Face FontSystem::Resolve(Face f) const { return (f == Face::Inherit || f >= Face::Count) ? CurrentFace() : f; }

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

	bool FontSystem::Rasterize(int faceIndex, int size, int codepoint, Glyph& out)
	{
		if (faceIndex < 0 || faceIndex >= static_cast<int>(m_faces.size()))
			return false;
		FaceData& f = m_faces[faceIndex];
		if (!f.ok)
			return false;
		const int gi = stbtt_FindGlyphIndex(&f.info, codepoint);
		if (gi == 0)
			return false;
		const FaceSpec& spec = kSpecs[faceIndex];
		const float emPx = static_cast<float>(size) * kEmRatio * spec.scale;
		const float sc = stbtt_ScaleForMappingEmToPixels(&f.info, emPx);

		int adv = 0, lsb = 0;
		stbtt_GetGlyphHMetrics(&f.info, gi, &adv, &lsb);
		out = Glyph{};
		out.advance = static_cast<float>(adv) * sc;
		float tabShift = 0.f;
		if (spec.tabular && codepoint >= '0' && codepoint <= '9')
		{
			const float tab = static_cast<float>(f.digitAdvance) * sc;
			tabShift = (tab - out.advance) * 0.5f;
			out.advance = tab;
		}

		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		stbtt_GetGlyphBitmapBox(&f.info, gi, sc, sc, &x0, &y0, &x1, &y1);
		const int w = x1 - x0, h = y1 - y0;
		const float baseline = static_cast<float>(size) * kBaselineRatio + spec.shift * static_cast<float>(size) * kEmRatio;
		out.offX = static_cast<float>(x0) + tabShift;
		out.offY = baseline + static_cast<float>(y0);

		if (w > 0 && h > 0)
		{
			std::vector<unsigned char> bmp(static_cast<size_t>(w) * h);
			stbtt_MakeGlyphBitmap(&f.info, bmp.data(), w, h, w, sc, sc, gi);
			int page = -1, px = 0, py = 0;
			if (Allocate(w, h, page, px, py))
			{
				// 흰색 + 커버리지 알파(계열별 곡선). 색은 그릴 때 tint로 준다.
				std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
				for (int i = 0; i < w * h; ++i)
				{
					rgba[i * 4 + 0] = 255;
					rgba[i * 4 + 1] = 255;
					rgba[i * 4 + 2] = 255;
					rgba[i * 4 + 3] = f.lut[bmp[i]];
				}
				UpdateTextureRec(m_pages[page].tex, Rectangle{ (float)px, (float)py, (float)w, (float)h }, rgba.data());
				out.page = page;
				out.src = Rectangle{ (float)px, (float)py, (float)w, (float)h };
			}
		}
		return true;
	}

	const FontSystem::Glyph& FontSystem::GetGlyph(Face face, int size, int codepoint)
	{
		const uint64_t key = (static_cast<uint64_t>(face) << 48) | (static_cast<uint64_t>(size & 0xFFFF) << 32) | static_cast<uint32_t>(codepoint);
		auto it = m_glyphs.find(key);
		if (it != m_glyphs.end())
			return it->second;

		Glyph glyph;
		bool found = false;
		for (Face f : kSpecs[static_cast<size_t>(face)].chain)
		{
			if (f == Face::Count) break;
			if (Rasterize(static_cast<int>(f), size, codepoint, glyph)) { found = true; break; }
		}
		// 어느 글꼴에도 없는 글자는 '?'로 대체(무한 재귀 방지)
		if (!found && codepoint != '?')
			return m_glyphs[key] = GetGlyph(face, size, '?');
		return m_glyphs[key] = glyph;
	}

	Vector2 FontSystem::Measure(std::string_view text, float size, float spacing, Face face)
	{
		if (!IsReady()) return { 0, 0 };
		const Face f = Resolve(face);
		const int isize = std::max(4, static_cast<int>(std::lround(size)));
		static thread_local std::vector<int> cps;
		DecodeUtf8(text, cps);
		float w = 0.f;
		if (m_xK != 1.f)
		{
			// 확대된 곳: 그릴 때와 같은 래스터 크기로 재고 지역 단위로 되돌린다
			const int rsize = std::max(4, static_cast<int>(std::lround(isize * m_xK)));
			for (int cp : cps)
				w += GetGlyph(f, rsize, cp).advance / m_xK + spacing;
		}
		else
			for (int cp : cps)
				w += GetGlyph(f, isize, cp).advance + spacing;
		if (!cps.empty()) w -= spacing;
		return { w, static_cast<float>(isize) };
	}

	float FontSystem::DrawRun(std::string_view text, Vector2 pos, Face face, int isize, float spacing, Color color)
	{
		static thread_local std::vector<int> cps;
		DecodeUtf8(text, cps);
		float x = pos.x;
		if (m_xK != 1.f)
		{
			// 확대된 곳(UI 배율 · HUD 덩어리 배율): size × k로 래스터라이즈 → 1/k 크기로 찍고,
			// 자리는 물리 픽셀 경계에 맞춘다(행렬이 k배 하면 글리프 1픽셀 = 화면 1픽셀)
			const float k = m_xK, inv = 1.f / k;
			const int rsize = std::max(4, static_cast<int>(std::lround(isize * k)));
			for (int cp : cps)
			{
				const Glyph& g = GetGlyph(face, rsize, cp);
				if (g.page >= 0)
				{
					const float px = std::round(m_xOff.x + (x + g.offX * inv) * k), py = std::round(m_xOff.y + (pos.y + g.offY * inv) * k);
					const Rectangle dst{ (px - m_xOff.x) * inv, (py - m_xOff.y) * inv, g.src.width * inv, g.src.height * inv };
					DrawTexturePro(m_pages[g.page].tex, g.src, dst, Vector2{ 0, 0 }, 0.f, color);
				}
				x += g.advance * inv + spacing;
			}
			return x - pos.x;
		}
		for (int cp : cps)
		{
			const Glyph& g = GetGlyph(face, isize, cp);
			if (g.page >= 0)
			{
				// 정수 픽셀에 맞춰 찍는다(힌팅 없는 래스터라이저라 반 픽셀 위치면 번진다)
				const Rectangle dst{ std::round(x + g.offX), std::round(pos.y + g.offY), g.src.width, g.src.height };
				DrawTexturePro(m_pages[g.page].tex, g.src, dst, Vector2{ 0, 0 }, 0.f, color);
			}
			x += g.advance + spacing;
		}
		return x - pos.x;
	}

	float FontSystem::Draw(std::string_view text, Vector2 pos, const TextStyle& style)
	{
		if (!IsReady() || text.empty()) return 0.f;
		const Face face = Resolve(style.face);
		const int isize = std::max(4, static_cast<int>(std::lround(style.size)));

		if (style.shadow > 0.f)
			DrawRun(text, Vector2{ pos.x + style.shadow, pos.y + style.shadow }, face, isize, style.spacing, style.shadowColor);

		if (style.outline > 0.f)
		{
			const float o = style.outline;
			static constexpr float dirs[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{.7f,.7f},{-.7f,.7f},{.7f,-.7f},{-.7f,-.7f} };
			for (const auto& d : dirs)
				DrawRun(text, Vector2{ pos.x + d[0] * o, pos.y + d[1] * o }, face, isize, style.spacing, style.outlineColor);
		}

		return DrawRun(text, pos, face, isize, style.spacing, style.color);
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
		const Vector2 m = Measure(text, style.size, style.spacing, style.face);
		Draw(text, Vector2{ center.x - m.x * 0.5f, center.y - m.y * 0.5f }, style);
	}
}
