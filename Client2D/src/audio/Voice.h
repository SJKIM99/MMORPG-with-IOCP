#pragma once

#include <string>
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// 음성 대사(Higgsfield 음성 합성 — tools/assetgen/voices.py가 assets/voice/<열쇠>.ogg로 만든다).
//   npc_<id>              마을 사람 인사(대화창을 열 때)
//   pc_<직업>_ult|lvl|die  궁극기 외침 · 레벨 업 · 쓰러짐
//   boss_<id>_spawn|die   필드 보스 등장 · 최후
// 대사는 한 번에 하나만(새 대사가 앞 대사를 끊는다) — 여러 목소리가 겹쳐 뭉개지지 않게.
// 파일이 없으면 조용히 넘어간다(합성 효과음은 audio/Sfx가 따로 낸다).
// ─────────────────────────────────────────────────────────────────────────────
namespace voice
{
	void Init(const std::string& dir);   // sfx::Init 뒤(오디오 장치가 열린 뒤)
	void Shutdown();

	// 대사 한 줄. priority가 낮으면 이미 나오는 대사를 끊지 않는다(작은 인사가 보스 대사를 덮지 않게).
	bool Play(std::string_view key, float volume = 1.f, int priority = 1);
	[[nodiscard]] bool Has(std::string_view key);
	void Stop();
}
