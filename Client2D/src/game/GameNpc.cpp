#include "game/Game.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "audio/Sfx.h"
#include "core/Util.h"
#include "Data/NpcTalk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// 살아 있는 마을 — NPC는 서버(마을 Zone)가 배회시킨다. 클라는 받은 칸 사이를 걸음
// 시간 동안 보간해 걷게 그리고, 말풍선·인사·쓰다듬기 반응·음유시인의 음표 같은
// "생기"를 얹는다. 대화/서비스는 서버가 거리를 확인한다.
// ─────────────────────────────────────────────────────────────────────────────

using util::Rgb;
using WorldData::NpcKind;

namespace
{
	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	template <typename V>
	void FaceFromTile(V& v, int fx, int fy)
	{
		const int sx = fx - fy, sy = fx + fy;   // 타일 → 화면(오른쪽 = X-Y, 아래 = X+Y)
		if (sx != 0) v.faceRight = sx > 0;
		if (sy != 0) v.faceBack = sy < 0;
	}

	// 목소리 높낮이(종류별)
	float VoicePitch(const WorldData::NpcRow& row)
	{
		switch (row.kind)
		{
		case NpcKind::eChild: return 1.55f;
		case NpcKind::eElder: case NpcKind::eVillager: return 0.8f;
		case NpcKind::eWeaponSmith: case NpcKind::eGuard: case NpcKind::eFarmer: return 0.9f;
		case NpcKind::ePriest: case NpcKind::eMerchant: case NpcKind::eInnkeeper: return 1.2f;
		default: return 1.f;
		}
	}
}

void Game::InitNpcs()
{
	m_npcView.clear();
	for (const auto& row : WorldData::kNpcs)
	{
		NpcView v;
		v.row = &row;
		v.tx = row.x;
		v.ty = row.y;
		v.pos = v.logPos = v.from = v.to = Vector2{ static_cast<float>(row.x), static_cast<float>(row.y) };
		v.faceRight = row.facing == 1 || row.facing == 2;
		v.faceBack = row.facing >= 2;
		v.noteAt = util::RandF(0.f, 4.f);
		m_npcView[row.id] = v;
	}
}

Game::NpcView* Game::NpcViewOf(uint16_t npcId)
{
	auto it = m_npcView.find(npcId);
	return it == m_npcView.end() ? nullptr : &it->second;
}

Vector2 Game::NpcTile(uint16_t npcId) const
{
	auto it = m_npcView.find(npcId);
	if (it != m_npcView.end())
		return Vector2{ static_cast<float>(it->second.tx), static_cast<float>(it->second.ty) };
	const auto* npc = WorldData::FindNpc(npcId);
	return npc ? Vector2{ static_cast<float>(npc->x), static_cast<float>(npc->y) } : Vector2{ -1000.f, -1000.f };
}

// ─────────────────────────────────────────────────────────────────────────────
// 서버 알림
// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleNpcList(const NPC_LIST_NFY_PACKET& p)
{
	const int n = std::min<int>(p.count, kNpcListMax);
	for (int i = 0; i < n; ++i)
	{
		const NPC_ENTRY& e = p.npcs[i];
		NpcView* v = NpcViewOf(e.npcId);
		if (!v) continue;
		v->tx = e.x;
		v->ty = e.y;
		const Vector2 t{ static_cast<float>(e.x), static_cast<float>(e.y) };
		// 처음이거나 많이 어긋났으면 바로 옮기고, 가까우면 짧게 걸어가게 둔다
		if (!v->known || util::Len(util::Sub(v->logPos, t)) > 2.5f)
			v->pos = v->logPos = v->from = v->to = t;
		else
		{
			v->from = v->logPos;
			v->to = t;
			v->moveT = 0.f;
			v->moveDur = 0.35f;
		}
		v->known = true;
		v->state = e.state;
		FaceFromTile(*v, e.faceX, e.faceY);
	}
}

void Game::HandleNpcState(const NPC_STATE_NFY_PACKET& p)
{
	NpcView* v = NpcViewOf(p.npc.npcId);
	if (!v) return;
	const Vector2 t{ static_cast<float>(p.npc.x), static_cast<float>(p.npc.y) };
	v->tx = p.npc.x;
	v->ty = p.npc.y;
	v->state = p.npc.state;
	v->talkTo = p.talkTo;
	if (p.stepMs > 0)
	{
		// 한 걸음: 지금 논리 위치에서 새 칸으로 걸음 시간 동안
		v->from = util::Len(util::Sub(v->logPos, t)) > 2.5f ? t : v->logPos;
		v->to = t;
		v->moveT = 0.f;
		v->moveDur = std::max(0.08f, p.stepMs / 1000.f);
	}
	else if (util::Len(util::Sub(v->to, t)) > 0.01f)
	{
		v->from = v->logPos;
		v->to = t;
		v->moveT = 0.f;
		v->moveDur = 0.25f;
	}
	v->known = true;
	if (p.npc.state != static_cast<uint8_t>(NpcActState::Walk) || p.stepMs > 0)
		FaceFromTile(*v, p.npc.faceX, p.npc.faceY);
	// 나에게 말을 거는 중이면 나를 본다
	if (p.talkTo == m_selfId && m_hasSelf)
		FaceFromTile(*v, m_predX - v->tx, m_predY - v->ty);
}

void Game::HandleNpcBark(const NPC_BARK_NFY_PACKET& p)
{
	NpcView* v = NpcViewOf(p.npcId);
	if (!v || !v->row) return;
	const char* text = nullptr;
	if (p.line == kNpcGreetLine)
		text = v->row->greeting;
	else if (const auto* lines = NpcTalk::Find(p.npcId); lines && p.line < NpcTalk::kMaxBarks)
		text = lines->barks[p.line];
	if (!text) return;
	v->bark = text;
	v->barkLife = std::clamp(2.2f + static_cast<float>(v->bark.size()) * 0.045f, 2.5f, 6.f);
	v->barkT = v->barkLife;
	const bool toMe = p.toUser == m_selfId && m_hasSelf;
	if (toMe)
	{
		v->hopT = 0.f;   // 반가워서 폴짝
		FaceFromTile(*v, m_predX - v->tx, m_predY - v->ty);
	}
	const Vector2 tile{ static_cast<float>(v->tx), static_cast<float>(v->ty) };
	switch (v->row->kind)
	{
	case NpcKind::eDog: PlayAt(static_cast<int>(sfx::Id::DogBark), tile, 0.55f, 0.08f); break;
	case NpcKind::eCat: PlayAt(static_cast<int>(sfx::Id::CatMeow), tile, 0.45f, 0.08f); break;
	case NpcKind::eChicken: PlayAt(static_cast<int>(sfx::Id::Cluck), tile, 0.4f, 0.1f); break;
	default:
	{
		// 가까울 때만 웅얼거림(겹치지 않게)
		const int d = Cheb(m_predX, m_predY, v->tx, v->ty);
		if (d <= 6 && m_now - m_lastVoiceAt > 0.8)
		{
			m_lastVoiceAt = m_now;
			const float vol = (toMe ? 0.55f : 0.35f) * util::Clamp01(1.2f - d / 7.f);
			const float dx = static_cast<float>((v->tx - v->ty) - (m_predX - m_predY));
			sfx::PlayPitched(sfx::Id::Voice, vol, VoicePitch(*v->row) * util::RandF(0.94f, 1.06f), util::Clamp01(0.5f + dx * 0.06f));
		}
		break;
	}
	}
}

void Game::HandleNpcTalkAck(const NPC_TALK_ACK_PACKET& p)
{
	const auto r = static_cast<NpcTalkResult>(p.result);
	if (r == NpcTalkResult::Ok)
		return;
	if (m_dialogNpc == p.npcId)
	{
		m_dialogNpc = 0;
		m_shopMode = ShopMode::None;
	}
	AddCenter(r == NpcTalkResult::TooFar ? "조금 더 가까이 가서 말을 거세요." : "지금은 말을 걸 수 없습니다.", Rgb(0xD8D0C0));
}

void Game::RequestNpcService(uint16_t npcId, WorldData::NpcService service)
{
	const int idx = static_cast<int>(service);
	if (idx > 0 && idx < 3 && m_now < m_npcServiceReadyAt[idx])
	{
		char buf[80];
		std::snprintf(buf, sizeof(buf), "조금 뒤에 다시 부탁하세요 (%.0f초)", m_npcServiceReadyAt[idx] - m_now);
		AddCenter(buf, Rgb(0xC8C8C8));
		sfx::Play(sfx::Id::Error, 0.3f);
		return;
	}
	m_net.SendNpcService(npcId, static_cast<uint8_t>(service));
}

void Game::HandleNpcServiceAck(const NPC_SERVICE_ACK_PACKET& p)
{
	const auto r = static_cast<NpcServiceResult>(p.result);
	const auto service = static_cast<WorldData::NpcService>(p.service);
	const int idx = static_cast<int>(service);
	if ((r == NpcServiceResult::Ok || r == NpcServiceResult::Cooldown) && idx > 0 && idx < 3)
		m_npcServiceReadyAt[idx] = m_now + p.cooldownMs / 1000.0;
	switch (r)
	{
	case NpcServiceResult::Ok:
		if (service == WorldData::NpcService::Heal)
		{
			AddCenter("따뜻한 빛이 몸을 감쌉니다. (HP·MP 회복)", Rgb(0xFFF0B0));
			sfx::Play(sfx::Id::Heal, 0.9f, 0.f);
			fx::Fx().Flash(Rgb(0xFFF0C0), 0.18f, 0.35f);
		}
		else
		{
			AddCenter("든든한 수프 한 그릇! (HP·MP 40% 회복)", Rgb(0xFFD8A0));
			sfx::Play(sfx::Id::Potion, 0.8f);
		}
		break;
	case NpcServiceResult::Cooldown:
	{
		char buf[80];
		std::snprintf(buf, sizeof(buf), "조금 뒤에 다시 부탁하세요 (%.0f초)", p.cooldownMs / 1000.0);
		AddCenter(buf, Rgb(0xC8C8C8));
		break;
	}
	case NpcServiceResult::NoAdena: AddCenter("아데나가 부족합니다.", Rgb(0xFF8A7A)); sfx::Play(sfx::Id::Error, 0.4f); break;
	case NpcServiceResult::Full: AddCenter("이미 몸이 가뿐합니다.", Rgb(0xC8E0C8)); break;
	case NpcServiceResult::TooFar: AddCenter("조금 더 가까이 가세요.", Rgb(0xD8D0C0)); break;
	default: AddCenter("지금은 부탁할 수 없습니다.", Rgb(0xD8D0C0)); break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 매 프레임
// ─────────────────────────────────────────────────────────────────────────────
void Game::UpdateNpcs(float dt)
{
	for (auto& [id, v] : m_npcView)
	{
		if (!v.row) continue;
		const Vector2 prev = v.pos;
		if (v.moveT < 1.f)
			v.moveT = std::min(1.f, v.moveT + dt / std::max(0.05f, v.moveDur));
		v.logPos = util::LerpV(v.from, v.to, v.moveT);
		v.pos = util::DampV(v.pos, v.logPos, 1.f / 0.06f, dt);
		const float dist = util::Len(util::Sub(v.pos, prev));
		v.walkPhase += dist * util::kPi;
		if (dt > 1e-4f) v.speed = util::Damp(v.speed, dist / dt, 12.f, dt);
		const float walkTarget = util::Clamp01((v.speed - 0.3f) / 1.1f);
		v.moving = util::Damp(v.moving, walkTarget, walkTarget > v.moving ? 14.f : 8.f, dt);
		if (dist > 0.003f)
		{
			const float dx = v.pos.x - prev.x, dy = v.pos.y - prev.y;
			const float sx = dx - dy, sy = dx + dy;
			if (std::fabs(sx) > 0.002f) v.faceRight = sx > 0.f;
			if (std::fabs(sy) > 0.002f) v.faceBack = sy < 0.f;
		}
		else if (v.talkTo == m_selfId && m_hasSelf)
			FaceFromTile(v, m_predX - v.tx, m_predY - v.ty);
		v.barkT = std::max(0.f, v.barkT - dt);
		if (v.heartT >= 0.f) { v.heartT += dt; if (v.heartT > 1.6f) v.heartT = -1.f; }
		if (v.hopT >= 0.f) { v.hopT += dt / 0.35f; if (v.hopT >= 1.f) v.hopT = -1.f; }

		// 음유시인: 가까이 있으면 가끔 류트를 튕기고 음표가 떠오른다
		if (v.row->kind == NpcKind::eBard && v.moving < 0.2f)
		{
			v.noteAt -= dt;
			const int d = Cheb(m_predX, m_predY, v.tx, v.ty);
			if (v.noteAt <= 0.f && d <= 12)
			{
				v.noteAt = util::RandF(5.f, 9.f);
				PlayAt(static_cast<int>(sfx::Id::Lute), Vector2{ static_cast<float>(v.tx), static_cast<float>(v.ty) }, 0.6f, 0.03f);
				for (int k = 0; k < 4; ++k)
				{
					const Vector2 iso = TileToIso(v.pos);
					fx::Fx().FloatText(Vector2{ iso.x + util::RandF(-10.f, 14.f), iso.y }, 60.f + k * 10.f, k % 2 ? "♪" : "♫",
						util::Mix(Rgb(0xFFE08A), Rgb(0xB8E0FF), util::RandF()), 16.f + k * 2.f, 1.6f + k * 0.25f);
				}
			}
		}
	}
}
