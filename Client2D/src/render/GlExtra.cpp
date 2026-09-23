// ─────────────────────────────────────────────────────────────────────────────
// rlgl이 노출하지 않는 OpenGL 상태 하나(깊이 비교 함수)를 직접 바꾼다.
// glDepthFunc는 OpenGL 1.0 함수라 확장 로더 없이 opengl32(Windows)/libGL에서 바로
// 링크된다. raylib/windows.h 헤더와 섞이지 않도록 이 파일에만 둔다.
// ─────────────────────────────────────────────────────────────────────────────
#include "render/Draw.h"
#include "rlgl.h"

#if defined(_WIN32)
#define CLIENT_GLAPIENTRY __stdcall
#else
#define CLIENT_GLAPIENTRY
#endif

extern "C" void CLIENT_GLAPIENTRY glDepthFunc(unsigned int func);

namespace gfx
{
	void DepthGreater(bool on)
	{
		constexpr unsigned int kLequal = 0x0203;   // GL_LEQUAL (rlgl 기본값)
		constexpr unsigned int kGreater = 0x0204;  // GL_GREATER
		rlDrawRenderBatchActive();
		glDepthFunc(on ? kGreater : kLequal);
	}
}
