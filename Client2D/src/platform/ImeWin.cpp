#include "platform/Ime.h"

// raylib.h와 windows.h를 한 번역 단위에 함께 두지 않는다(Rectangle/CloseWindow 등 충돌).
// 필요한 raylib 함수 하나만 직접 선언한다.
extern "C" void* GetWindowHandle(void);

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>

namespace
{
	HWND g_hwnd = nullptr;
	WNDPROC g_prevProc = nullptr;
	bool g_enabled = true;
	std::wstring g_comp;
	std::vector<int> g_committed;
	POINT g_caret{ 0, 0 };

	std::string ToUtf8(const std::wstring& w)
	{
		if (w.empty()) return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		std::string out(static_cast<size_t>(n), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
		return out;
	}

	std::wstring ReadComp(HIMC imc, DWORD kind)
	{
		const LONG bytes = ImmGetCompositionStringW(imc, kind, nullptr, 0);
		if (bytes <= 0) return {};
		std::wstring w(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
		ImmGetCompositionStringW(imc, kind, w.data(), bytes);
		return w;
	}

	void PushCommitted(const std::wstring& w)
	{
		for (size_t i = 0; i < w.size(); ++i)
		{
			unsigned int cp = w[i];
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size())
			{
				cp = 0x10000 + ((cp - 0xD800) << 10) + (w[i + 1] - 0xDC00);
				++i;
			}
			if (cp >= 32)
				g_committed.push_back(static_cast<int>(cp));
		}
	}

	void PlaceWindows(HIMC imc)
	{
		COMPOSITIONFORM cf{};
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos = g_caret;
		ImmSetCompositionWindow(imc, &cf);
		CANDIDATEFORM cand{};
		cand.dwIndex = 0;
		cand.dwStyle = CFS_CANDIDATEPOS;
		cand.ptCurrentPos = POINT{ g_caret.x, g_caret.y + 24 };
		ImmSetCandidateWindow(imc, &cand);
	}

	LRESULT CALLBACK ImeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_IME_SETCONTEXT:
			// 시스템 조합 창은 숨기고(우리가 입력칸 안에 직접 그린다) 후보 창은 둔다
			lParam &= ~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
			break;
		case WM_IME_STARTCOMPOSITION:
			g_comp.clear();
			if (HIMC imc = ImmGetContext(hwnd)) { PlaceWindows(imc); ImmReleaseContext(hwnd, imc); }
			return 0;
		case WM_IME_COMPOSITION:
		{
			if (HIMC imc = ImmGetContext(hwnd))
			{
				if (lParam & GCS_RESULTSTR)
					PushCommitted(ReadComp(imc, GCS_RESULTSTR));
				if (lParam & GCS_COMPSTR)
					g_comp = ReadComp(imc, GCS_COMPSTR);
				else if (lParam & GCS_RESULTSTR)
					g_comp.clear();
				ImmReleaseContext(hwnd, imc);
			}
			return 0;  // DefWindowProc로 보내지 않는다 → WM_IME_CHAR/WM_CHAR 중복 없음
		}
		case WM_IME_ENDCOMPOSITION:
			g_comp.clear();
			return 0;
		default:
			break;
		}
		return CallWindowProcW(g_prevProc, hwnd, msg, wParam, lParam);
	}
}

namespace ime
{
	void Attach()
	{
		g_hwnd = static_cast<HWND>(GetWindowHandle());
		if (!g_hwnd) return;
		g_prevProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ImeProc)));
		g_enabled = true;
		SetEnabled(false);
	}

	void Detach()
	{
		if (g_hwnd && g_prevProc)
			SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_prevProc));
		g_prevProc = nullptr;
		g_hwnd = nullptr;
	}

	void SetEnabled(bool enabled)
	{
		if (!g_hwnd || enabled == g_enabled) return;
		if (enabled)
			ImmAssociateContextEx(g_hwnd, nullptr, IACE_DEFAULT);
		else
		{
			Cancel();
			ImmAssociateContextEx(g_hwnd, nullptr, 0);
		}
		g_enabled = enabled;
		g_comp.clear();
	}

	bool Enabled() { return g_enabled; }

	std::string Composition() { return ToUtf8(g_comp); }

	void TakeCommitted(std::vector<int>& out)
	{
		out.insert(out.end(), g_committed.begin(), g_committed.end());
		g_committed.clear();
	}

	void Cancel()
	{
		if (!g_hwnd) return;
		if (HIMC imc = ImmGetContext(g_hwnd))
		{
			ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
			ImmReleaseContext(g_hwnd, imc);
		}
		g_comp.clear();
	}

	void SetCaret(int x, int y)
	{
		g_caret = POINT{ x, y };
		if (!g_hwnd || !g_enabled) return;
		if (HIMC imc = ImmGetContext(g_hwnd))
		{
			PlaceWindows(imc);
			ImmReleaseContext(g_hwnd, imc);
		}
	}
}

#else  // ── Windows 외: IME 없음(raylib 문자 입력만 사용) ──

namespace ime
{
	void Attach() {}
	void Detach() {}
	void SetEnabled(bool) {}
	bool Enabled() { return false; }
	std::string Composition() { return {}; }
	void TakeCommitted(std::vector<int>&) {}
	void Cancel() {}
	void SetCaret(int, int) {}
}

#endif
