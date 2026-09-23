#pragma once

#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 한글 입력(IME). Windows에서는 창 프로시저를 서브클래싱해서 조합 중인 글자
// (WM_IME_COMPOSITION/GCS_COMPSTR)와 완성된 글자(GCS_RESULTSTR)를 직접 받는다 —
// 시스템 조합 창을 띄우지 않고 채팅창 안에 조합 중인 글자를 그대로 그리기 위함.
// 입력칸에 포커스가 없을 때는 IME를 꺼서, 한글 모드에서도 Q/W/Z 같은 단축키가
// 그대로 동작하게 한다. Windows가 아니면 아무 것도 하지 않는다(raylib 문자 입력만).
// ─────────────────────────────────────────────────────────────────────────────
namespace ime
{
	void Attach();
	void Detach();
	void SetEnabled(bool enabled);
	[[nodiscard]] bool Enabled();
	// 조합 중인 글자(UTF-8). 없으면 빈 문자열
	[[nodiscard]] std::string Composition();
	// 지난 호출 이후 IME가 완성한 글자들(코드포인트)
	void TakeCommitted(std::vector<int>& out);
	// 조합 취소(Enter로 조합 중인 글자를 직접 확정한 뒤 호출)
	void Cancel();
	// 후보 창 위치(화면 좌표)
	void SetCaret(int x, int y);
}
