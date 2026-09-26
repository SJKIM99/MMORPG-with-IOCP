#include "game/Game.h"
#include "ui/Wm.h"
#include "game/KeyBinds.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Art.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "ui/Font.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 · 메인 이야기 — "멈춘 심장"
//
// 판정(받기 · 진행 · 보고 · 보상)은 전부 서버가 한다. 클라는 같은 표(Data/QuestData.h)를
// 읽어 이야기를 들려주고, 어디로 가야 하는지 보여 주고, 진행될 때마다 손맛을 준다:
//   · NPC 머리 위 ! (새 이야기) ? (보고) · 말을 걸면 이야기가 한 글자씩 펼쳐진다
//   · 추적 표시(미니맵 아래) · 나침반(화면 밖 목적지 화살표) · 미니맵/지도 표식
//   · 수집물은 쓰러진 몬스터에게서 날아와 가방 대신 일지에 쌓인다
//   · 새 장이 열리면 이야기책처럼 장 제목이 뜨고, 새 장소에 들어서면 한 줄 이야기가 흐른다
//   · 마지막 퀘스트를 마치면 하르벤이 다시 뛰고 새벽이 온다(엔딩)
// ─────────────────────────────────────────────────────────────────────────────
using util::Rgb;
using ui::Align;
using ui::ButtonKind;
using QuestData::QuestRow;
using QuestData::ObjKind;
using QuestData::QuestKind;

namespace
{
	std::string Fmt(const char* f, ...)
	{
		char buf[512];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	Color KindColor(QuestKind k)
	{
		switch (k)
		{
		// 메인 이야기 = 새벽빛, 부가 = 들녘의 풀빛, 반복 = 흐린 하늘빛, 직업 = 옛길 청록
		case QuestKind::Main:  return ui::theme::kDawn;
		case QuestKind::Side:  return Rgb(0x9CC47E);
		case QuestKind::Daily: return ui::theme::kInfo;
		case QuestKind::Class: return ui::theme::kOldway;
		}
		return WHITE;
	}

	const char* KindName(QuestKind k)
	{
		switch (k)
		{
		case QuestKind::Main:  return "메인 이야기";
		case QuestKind::Side:  return "부가 이야기";
		case QuestKind::Daily: return "반복 의뢰";
		case QuestKind::Class: return "직업의 길";
		}
		return "";
	}

	const char* KindGlyph(QuestKind k)
	{
		switch (k)
		{
		case QuestKind::Main:  return "◆";
		case QuestKind::Side:  return "●";
		case QuestKind::Daily: return "◎";
		case QuestKind::Class: return "◈";
		}
		return "·";
	}

	std::string NpcName(uint16_t id)
	{
		const auto* npc = WorldData::FindNpc(id);
		return npc ? npc->name : "";
	}

	std::string ChapterTag(const QuestRow& q)
	{
		if (q.kind != QuestKind::Main || q.chapter < 1 || q.chapter > QuestData::kChapterCount)
			return KindName(q.kind);
		return Fmt("제%d장 · %s", q.chapter, QuestData::kChapters[q.chapter].title);
	}

	// 몇 시간 · 분 뒤
	std::string DurationText(double sec)
	{
		const int s = static_cast<int>(std::max(0.0, sec));
		if (s >= 3600) return Fmt("%d시간 %d분", s / 3600, (s % 3600) / 60);
		if (s >= 60) return Fmt("%d분", s / 60);
		return Fmt("%d초", s);
	}

	// 체크 표시(글꼴에 ✓가 없어 선으로 그린다)
	void Check(Vector2 c, float s, Color col)
	{
		DrawLineEx(Vector2{ c.x - s * 0.5f, c.y }, Vector2{ c.x - s * 0.1f, c.y + s * 0.4f }, std::max(1.5f, s * 0.22f), col);
		DrawLineEx(Vector2{ c.x - s * 0.1f, c.y + s * 0.4f }, Vector2{ c.x + s * 0.55f, c.y - s * 0.45f }, std::max(1.5f, s * 0.22f), col);
	}

	// 그로시(은화)
	void Coin(Vector2 c, float r)
	{
		DrawCircleV(Vector2{ c.x + 0.5f, c.y + 1.2f }, r, Color{ 0, 0, 0, 140 });
		DrawCircleV(c, r, Color{ 128, 132, 138, 255 });
		DrawCircleV(c, r - 1.2f, Color{ 196, 199, 202, 255 });
		DrawCircleV(Vector2{ c.x - r * 0.18f, c.y - r * 0.2f }, r * 0.5f, Color{ 232, 232, 226, 255 });
	}

	void Section(const char* title, float x, float y, float w)
	{
		ui::SectionHeader(Vector2{ x, y }, w, title);
	}

	// 목표 종류 아이콘(작은 벡터 그림)
	void ObjIcon(Rectangle r, const QuestData::Objective& o, Color tint)
	{
		const Vector2 c{ r.x + r.width * 0.5f, r.y + r.height * 0.5f };
		const float s = r.width * 0.5f;
		switch (o.kind)
		{
		case ObjKind::Collect:
			icons::DrawQuestItem(static_cast<uint8_t>(o.icon), r, tint);
			break;
		case ObjKind::Kill:
		case ObjKind::KillIn:
			DrawLineEx(Vector2{ c.x - s * 0.7f, c.y + s * 0.7f }, Vector2{ c.x + s * 0.7f, c.y - s * 0.7f }, 2.f, tint);
			DrawLineEx(Vector2{ c.x + s * 0.7f, c.y + s * 0.7f }, Vector2{ c.x - s * 0.7f, c.y - s * 0.7f }, 2.f, tint);
			DrawLineEx(Vector2{ c.x - s * 0.6f, c.y + s * 0.2f }, Vector2{ c.x - s * 0.2f, c.y + s * 0.6f }, 2.f, tint);
			DrawLineEx(Vector2{ c.x + s * 0.6f, c.y + s * 0.2f }, Vector2{ c.x + s * 0.2f, c.y + s * 0.6f }, 2.f, tint);
			break;
		case ObjKind::Boss:
			DrawCircleV(Vector2{ c.x, c.y - s * 0.1f }, s * 0.7f, tint);
			DrawRectangleV(Vector2{ c.x - s * 0.4f, c.y + s * 0.3f }, Vector2{ s * 0.8f, s * 0.45f }, tint);
			DrawCircleV(Vector2{ c.x - s * 0.28f, c.y - s * 0.15f }, s * 0.18f, Color{ 20, 12, 12, 255 });
			DrawCircleV(Vector2{ c.x + s * 0.28f, c.y - s * 0.15f }, s * 0.18f, Color{ 20, 12, 12, 255 });
			break;
		case ObjKind::Talk:
			DrawRectangleRounded(Rectangle{ c.x - s * 0.8f, c.y - s * 0.6f, s * 1.6f, s * 1.0f }, 0.5f, 6, tint);
			DrawTriangle(Vector2{ c.x - s * 0.4f, c.y + s * 0.35f }, Vector2{ c.x - s * 0.55f, c.y + s * 0.8f }, Vector2{ c.x - s * 0.05f, c.y + s * 0.35f }, tint);
			for (int i = -1; i <= 1; ++i) DrawCircleV(Vector2{ c.x + i * s * 0.4f, c.y - s * 0.1f }, s * 0.12f, Color{ 30, 24, 20, 255 });
			break;
		case ObjKind::Explore:
			DrawCircleV(Vector2{ c.x, c.y - s * 0.25f }, s * 0.55f, tint);
			DrawTriangle(Vector2{ c.x - s * 0.48f, c.y - s * 0.05f }, Vector2{ c.x, c.y + s * 0.85f }, Vector2{ c.x + s * 0.48f, c.y - s * 0.05f }, tint);
			DrawCircleV(Vector2{ c.x, c.y - s * 0.25f }, s * 0.22f, Color{ 30, 24, 20, 255 });
			break;
		case ObjKind::Equip:
		{
			const Vector2 a{ c.x - s * 0.7f, c.y - s * 0.7f }, b{ c.x + s * 0.7f, c.y - s * 0.7f }, d{ c.x, c.y + s * 0.85f };
			DrawTriangle(a, d, b, tint);
			DrawRectangleV(Vector2{ a.x, a.y }, Vector2{ s * 1.4f, s * 0.5f }, tint);
			break;
		}
		default:
			break;
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 상태 조회
// ─────────────────────────────────────────────────────────────────────────────
const Game::QuestView* Game::ActiveQuest(uint16_t id) const
{
	for (const auto& v : m_quests)
		if (v.id == id) return &v;
	return nullptr;
}

bool Game::QuestDone(uint16_t id) const
{
	return QuestData::TestBit(m_questDone.data(), id);
}

bool Game::QuestReady(const QuestView& v) const
{
	const QuestRow* q = QuestData::Find(v.id);
	return q && QuestData::ObjectivesDone(*q, v.progress.data());
}

Game::QuestAvail Game::CanTakeQuest(const QuestRow& q) const
{
	if (ActiveQuest(q.id)) return QuestAvail::No;
	if (QuestData::IsRepeatable(q))
	{
		const auto it = m_questCooldownUntil.find(q.id);
		if (it != m_questCooldownUntil.end() && it->second > m_now) return QuestAvail::Cooldown;
	}
	else if (QuestDone(q.id))
		return QuestAvail::No;
	if (q.prereq != 0 && !QuestDone(q.prereq)) return QuestAvail::No;
	if (!QuestData::ClassCanTake(q, m_classId)) return QuestAvail::No;
	if (m_level < q.minLevel) return QuestAvail::LowLevel;
	return QuestAvail::Yes;
}

Game::QuestAvail Game::CanTakeQuestId(uint16_t id) const
{
	const QuestRow* q = QuestData::Find(id);
	return q ? CanTakeQuest(*q) : QuestAvail::No;
}

Game::QuestMark Game::NpcQuestMark(uint16_t npcId) const
{
	if (!m_questsKnown) return QuestMark::None;
	QuestMark best = QuestMark::None;
	auto raise = [&](QuestMark m) { if (static_cast<int>(m) > static_cast<int>(best)) best = m; };
	for (const auto& v : m_quests)
	{
		const QuestRow* q = QuestData::Find(v.id);
		if (!q) continue;
		const bool ready = QuestData::ObjectivesDone(*q, v.progress.data());
		if (q->turnIn == npcId && ready) return QuestMark::Ready;
		if (q->turnIn == npcId) raise(QuestMark::Active);
		// 이 NPC에게 말을 거는 목표(누렁이 쓰다듬기 등)
		for (int i = 0; i < QuestData::kMaxObjs; ++i)
			if (q->objs[i].kind == ObjKind::Talk && q->objs[i].target == npcId && v.progress[static_cast<size_t>(i)] < q->objs[i].count)
				raise(QuestMark::Talk);
	}
	for (const auto& q : QuestData::kQuests)
	{
		if (q.giver != npcId) continue;
		const QuestAvail a = CanTakeQuest(q);
		if (a == QuestAvail::Yes) raise(QuestMark::Available);
		else if (a == QuestAvail::LowLevel && (q.kind == QuestKind::Main || q.minLevel <= m_level + 5)) raise(QuestMark::Low);
	}
	return best;
}

std::string Game::ObjectiveLabel(const QuestData::Objective& o) const
{
	if (o.label) return o.label;
	switch (o.kind)
	{
	case ObjKind::Kill:    return texts::MonsterName(o.target) + " 처치";
	case ObjKind::KillIn:  return texts::RegionName(o.zone) + "의 몬스터 처치";
	case ObjKind::Collect: return "수집";
	case ObjKind::Talk:    return texts::Josa(NpcName(o.target), "과", "와") + " 이야기";
	case ObjKind::Explore: { const auto* p = QuestData::FindPoi(o.target); return std::string(p ? p->name : "?") + " 찾아가기"; }
	case ObjKind::Boss:    return texts::MonsterName(o.target) + " 토벌";
	case ObjKind::Equip:   return Fmt("장비 %d부위 착용", o.count);
	default: break;
	}
	return "";
}

const Game::QuestView* Game::FocusQuest() const
{
	if (m_trackedQuest != 0)
		if (const QuestView* v = ActiveQuest(m_trackedQuest)) return v;
	const QuestView* best = nullptr;
	for (const auto& v : m_quests)
	{
		const QuestRow* q = QuestData::Find(v.id);
		if (!q) continue;
		if (q->kind == QuestKind::Main) return &v;
		if (!best) best = &v;
	}
	return best;
}

bool Game::IsQuestTarget(const Entity& e) const
{
	if (e.kind != EntityKind::Monster || !e.row || e.dead) return false;
	const int zone = WorldData::RegionIdAt(e.tx, e.ty);
	for (const auto& v : m_quests)
	{
		const QuestRow* q = QuestData::Find(v.id);
		if (!q) continue;
		for (int i = 0; i < QuestData::kMaxObjs; ++i)
		{
			const auto& o = q->objs[i];
			if (v.progress[static_cast<size_t>(i)] >= o.count) continue;
			if ((o.kind == ObjKind::Kill || o.kind == ObjKind::Collect || o.kind == ObjKind::Boss) && o.target == e.row->id
				&& (o.zone == QuestData::kAnyZone || o.zone == zone))
				return true;
		}
	}
	return false;
}

bool Game::QuestWantsRegion(int zoneId) const
{
	for (const auto& v : m_quests)
	{
		const QuestRow* q = QuestData::Find(v.id);
		if (!q || QuestData::ObjectivesDone(*q, v.progress.data())) continue;
		for (int i = 0; i < QuestData::kMaxObjs; ++i)
		{
			const auto& o = q->objs[i];
			if (o.kind == ObjKind::None || v.progress[static_cast<size_t>(i)] >= o.count) continue;
			if (o.kind == ObjKind::Explore)
			{
				if (const auto* p = QuestData::FindPoi(o.target); p && WorldData::RegionIdAt(p->x, p->y) == zoneId) return true;
			}
			else if (o.zone == zoneId && o.kind != ObjKind::Talk && o.kind != ObjKind::Equip)
				return true;
		}
	}
	return false;
}

uint16_t Game::AutoQuestForNpc(uint16_t npcId) const
{
	// 보고할 퀘스트가 먼저(보상의 순간), 그다음 이 NPC가 주는 메인 이야기
	for (const auto& v : m_quests)
	{
		const QuestRow* q = QuestData::Find(v.id);
		if (q && q->turnIn == npcId && QuestData::ObjectivesDone(*q, v.progress.data())) return v.id;
	}
	for (const auto& q : QuestData::kQuests)
		if (q.giver == npcId && q.kind == QuestKind::Main && CanTakeQuest(q) == QuestAvail::Yes) return q.id;
	return 0;
}

bool Game::QuestDestination(QuestDest& out) const
{
	out = QuestDest{};
	const QuestView* v = FocusQuest();
	Vector2 dest{};
	std::string name;
	bool npc = false, poi = false;
	if (!v)
	{
		// 진행 중인 게 없으면 다음 메인 이야기를 주는 사람에게
		const QuestRow* next = QuestData::NextMain([&](uint16_t id) { return QuestDone(id); });
		if (!next || CanTakeQuest(*next) != QuestAvail::Yes) return false;
		dest = NpcTile(next->giver);
		name = NpcName(next->giver);
		npc = true;
		out.quest = next->id;
	}
	else
	{
		const QuestRow* q = QuestData::Find(v->id);
		if (!q) return false;
		out.quest = v->id;
		if (QuestData::ObjectivesDone(*q, v->progress.data()))
		{
			if (q->turnIn == 0) return false;
			dest = NpcTile(q->turnIn);
			name = NpcName(q->turnIn) + "에게 보고";
			npc = true;
		}
		else
		{
			bool found = false;
			for (int i = 0; i < QuestData::kMaxObjs && !found; ++i)
			{
				const auto& o = q->objs[i];
				if (o.kind == ObjKind::None || v->progress[static_cast<size_t>(i)] >= o.count) continue;
				switch (o.kind)
				{
				case ObjKind::Explore:
					if (const auto* p = QuestData::FindPoi(o.target))
					{
						dest = Vector2{ static_cast<float>(p->x), static_cast<float>(p->y) };
						name = p->name;
						poi = found = true;
					}
					break;
				case ObjKind::Talk:
					dest = NpcTile(o.target);
					name = NpcName(o.target);
					npc = found = true;
					break;
				case ObjKind::Boss:
					for (const auto& reg : WorldData::kRegions)
						if (reg.bossSpecies == o.target && (o.zone == QuestData::kAnyZone || o.zone == reg.zoneId))
						{
							dest = Vector2{ static_cast<float>(reg.bossX), static_cast<float>(reg.bossY) };
							name = texts::MonsterName(o.target);
							found = true;
							break;
						}
					break;
				case ObjKind::Kill:
				case ObjKind::Collect:
				case ObjKind::KillIn:
				{
					const int here = Region();
					if (o.zone != QuestData::kAnyZone && (o.zone != here || InTown()))
					{
						// 그 지역의 사냥터로(데니차 들녘은 남문 밖)
						if (o.zone == WorldData::kTownZoneId)
							dest = Vector2{ static_cast<float>(WorldData::kTownX0 + 21), static_cast<float>(WorldData::kTownY0 + WorldData::kTownSize + 5) };
						else if (const auto* reg = WorldData::FindRegion(o.zone))
							dest = Vector2{ static_cast<float>(reg->landX), static_cast<float>(reg->landY) };
						name = texts::RegionName(o.zone);
						found = true;
					}
					else if (o.kind != ObjKind::KillIn)
					{
						// 그 지역에 있다 — 보이는 대상 중 가장 가까운 것
						int bestD = 1 << 30;
						for (const auto& [id, e] : m_ents)
						{
							if (e.kind != EntityKind::Monster || !e.row || e.dead || e.row->id != o.target) continue;
							const int d = Cheb(m_predX, m_predY, e.tx, e.ty);
							if (d < bestD) { bestD = d; dest = Vector2{ static_cast<float>(e.tx), static_cast<float>(e.ty) }; }
						}
						if (bestD < (1 << 30)) { name = texts::MonsterName(o.target); found = true; }
					}
					break;
				}
				default:
					break;
				}
			}
			if (!found) return false;
		}
	}
	// 마을 안에서 다른 지역으로 가야 하면 길잡이(옛길 이동)를 거친다
	const int destRegion = WorldData::RegionIdAt(static_cast<int>(dest.x), static_cast<int>(dest.y));
	if (InTown() && destRegion >= 0 && destRegion != WorldData::kTownZoneId)
	{
		out.tile = NpcTile(3);
		out.label = std::string(WorldData::FindNpc(3)->name) + " → " + texts::RegionName(destRegion);
		out.viaTeleporter = true;
		out.npc = true;
		return true;
	}
	out.tile = dest;
	out.label = name;
	out.npc = npc;
	out.poi = poi;
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 서버 알림
// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleQuestList(const QUEST_LIST_INF_PACKET& p)
{
	m_quests.clear();
	const int n = std::min<int>(p.activeCount, kQuestListActiveMax);
	for (int i = 0; i < n; ++i)
	{
		if (!QuestData::Find(p.active[i].questId)) continue;
		QuestView v;
		v.id = p.active[i].questId;
		for (int k = 0; k < 3; ++k) v.progress[static_cast<size_t>(k)] = p.active[i].progress[k];
		v.acceptedAt = m_now - 1000.0 + i;
		if (QuestReady(v)) v.readyAt = m_now - 1000.0;
		m_quests.push_back(v);
	}
	std::memcpy(m_questDone.data(), p.done, sizeof(p.done));
	m_questCooldownUntil.clear();
	for (int i = 0; i < std::min<int>(p.cooldownCount, kQuestListCooldownMax); ++i)
		m_questCooldownUntil[p.cooldowns[i].questId] = m_now + p.cooldowns[i].remainSec;
	m_questsKnown = true;
}

void Game::HandleQuestUpdate(const QUEST_UPDATE_INF_PACKET& p)
{
	const QuestRow* q = QuestData::Find(p.questId);
	if (!q) return;
	const Color kc = KindColor(q->kind);
	Entity* me = Self();
	const Vector2 myIso = me ? TileToIso(me->pos) : TileToIso(Vector2{ static_cast<float>(m_predX), static_cast<float>(m_predY) });
	auto findView = [&]() -> QuestView*
	{
		for (auto& v : m_quests) if (v.id == p.questId) return &v;
		return nullptr;
	};

	switch (static_cast<QuestEvent>(p.event))
	{
	case QuestEvent::Accepted:
	{
		QuestView* v = findView();
		if (!v) { m_quests.push_back(QuestView{}); v = &m_quests.back(); }
		v->id = p.questId;
		for (int k = 0; k < 3; ++k) v->progress[static_cast<size_t>(k)] = p.progress[k];
		v->acceptedAt = m_now;
		v->readyAt = QuestReady(*v) ? m_now : -10.0;
		sfx::Play(sfx::Id::QuestAccept, 0.8f, 0.02f);
		AddBanner("새 퀘스트 · " + std::string(q->name), kc, 3.5f);
		AddChat("[퀘스트] " + std::string(q->name) + " — " + q->summary, util::Shade(kc, 0.95f));
		fx::Fx().Ring(myIso, 8.f, 46.f, 0.6f, util::WithA(kc, 0.8f), 3.f);
		// 새 장의 첫 이야기면 장 제목(이야기책 한 장을 넘기듯)
		if (q->kind == QuestKind::Main)
		{
			const QuestRow* pre = q->prereq ? QuestData::Find(q->prereq) : nullptr;
			if (!pre || pre->chapter != q->chapter)
			{
				m_chapterCard = q->chapter;
				m_chapterCardAt = m_now + 0.5;
				m_chapterBellPlayed = false;
				(void)art::Chapter(q->chapter);   // 카드가 뜨기 전에 미리 풀어 둔다(뜨는 순간 멈칫하지 않게)
			}
		}
		// 받으면 대화창을 닫는다 — 장 제목 · 추적 표시 · 나침반이 다음 걸음을 안내한다
		if (m_dialogQuest == p.questId)
		{
			m_dialogQuest = 0;
			m_dialogNpc = 0;
			m_shopMode = ShopMode::None;
		}
		break;
	}
	case QuestEvent::Progress:
	{
		QuestView* v = findView();
		if (!v) { m_quests.push_back(QuestView{}); v = &m_quests.back(); v->id = p.questId; v->acceptedAt = m_now; }
		const bool wasReady = QuestReady(*v);
		const int obj = p.objIndex < 3 ? p.objIndex : 0;
		const uint16_t before = v->progress[static_cast<size_t>(obj)];
		for (int k = 0; k < 3; ++k) v->progress[static_cast<size_t>(k)] = p.progress[k];
		const auto& o = q->objs[obj];
		const uint16_t now = v->progress[static_cast<size_t>(obj)];
		const bool up = now > before;
		const bool objDone = now >= o.count && before < o.count;
		if (up) v->bumpAt[static_cast<size_t>(obj)] = m_now;
		const std::string label = ObjectiveLabel(o);
		const std::string count = o.count > 1 ? Fmt(" %d/%d", now, o.count) : "";
		switch (o.kind)
		{
		case ObjKind::Collect:
			if (p.x >= 0)
			{
				// 쓰러진 자리에서 수집물이 날아온다(도착하면 글자 · 소리)
				m_questFlies.push_back(QuestFly{ Vector2{ static_cast<float>(p.x), static_cast<float>(p.y) }, m_now + 0.25, p.questId, obj });
				sfx::Play(sfx::Id::QuestFly, 0.5f, 0.06f);
			}
			else
				fx::Fx().FloatText(myIso, actors::PlayerHeight() + 16.f, "+1 " + label + count, Rgb(0xFFE08A), 17.f, 1.6f);
			break;
		case ObjKind::Explore:
			if (p.questId != QuestData::kFinalQuest)
			{
				const auto* poi = QuestData::FindPoi(o.target);
				m_discoveries.push_back(Discovery{ o.target, m_now, false });
				sfx::Play(sfx::Id::Discover, 0.75f, 0.f);
				if (poi)
				{
					const Vector2 iso = TileToIso(Vector2{ static_cast<float>(poi->x), static_cast<float>(poi->y) });
					fx::Fx().Beam(iso, 40.f, 420.f, 1.6f, Rgb(0xFFE8A0, 200));
					fx::Fx().Ring(iso, 20.f, 150.f, 1.2f, Rgb(0xFFE8A0, 200), 4.f);
				}
			}
			break;
		case ObjKind::Boss:
			AddBanner(texts::MonsterName(o.target) + " 토벌 인정", ui::theme::kEmber, 4.f);
			break;
		case ObjKind::Talk:
			fx::Fx().FloatText(myIso, actors::PlayerHeight() + 16.f, label, Rgb(0xFFE08A), 17.f, 1.6f);
			break;
		default:
			if (up || o.kind == ObjKind::Equip)
				fx::Fx().FloatText(myIso, actors::PlayerHeight() + 16.f, label + count, objDone ? Rgb(0xFFE070) : Rgb(0xF0E0B0), objDone ? 18.f : 15.f, 1.5f);
			break;
		}
		if (o.kind != ObjKind::Collect || p.x < 0)
		{
			if (objDone && o.kind != ObjKind::Explore) sfx::Play(sfx::Id::QuestObjDone, 0.6f, 0.02f);
			else if (up && o.kind != ObjKind::Explore) sfx::Play(sfx::Id::QuestTick, 0.45f, 0.05f);
		}
		const bool ready = QuestReady(*v);
		if (!wasReady && ready && p.questId != QuestData::kFinalQuest)
		{
			v->readyAt = m_now;
			sfx::Play(sfx::Id::QuestReady, 0.75f, 0.f);
			const std::string who = q->turnIn ? NpcName(q->turnIn) : std::string();
			AddBanner(std::string(q->name) + " — 목표 완료", ui::theme::kDawnHi, 3.5f);
			AddChat(q->turnIn ? "[퀘스트] " + std::string(q->name) + " — " + texts::Josa(who, "과", "와") + " 이야기하면 보상을 받습니다." :
				"[퀘스트] " + std::string(q->name) + " — 퀘스트 일지(" + kb::Label(kb::Act::QuestLog) + ")에서 보상을 받으세요.", Rgb(0xFFE070));
		}
		else if (wasReady && !ready)
			v->readyAt = -10.0;
		break;
	}
	case QuestEvent::Completed:
	{
		m_quests.erase(std::remove_if(m_quests.begin(), m_quests.end(), [&](const QuestView& v) { return v.id == p.questId; }), m_quests.end());
		if (QuestData::IsRepeatable(*q))
			m_questCooldownUntil[p.questId] = m_now + p.cooldownSec;
		else if (p.questId < 256)
			m_questDone[p.questId >> 3] = static_cast<uint8_t>(m_questDone[p.questId >> 3] | (1u << (p.questId & 7)));
		m_questDoneFx.push_back(QuestDoneFx{ p.questId, m_now, p.exp, p.adena, p.itemId, p.itemCount, p.rarity });
		sfx::Play(sfx::Id::QuestComplete, 0.8f, 0.f);
		fx::Fx().Beam(myIso, 34.f, 300.f, 1.1f, util::WithA(kc, 0.8f));
		fx::Fx().LootSparkle(myIso, kc);
		std::string reward;
		if (p.exp) reward += "경험치 +" + texts::Number(p.exp);
		if (p.adena) reward += (reward.empty() ? "" : " · ") + std::string("그로시 +") + texts::Number(p.adena);
		if (p.itemId) reward += (reward.empty() ? "" : " · ") + texts::ItemName(p.itemId) + (p.itemCount > 1 ? Fmt(" ×%d", p.itemCount) : "");
		AddChat("[퀘스트 완료] " + std::string(q->name) + (reward.empty() ? "" : " — " + reward), Rgb(0xFFE070));
		if (m_trackedQuest == p.questId) m_trackedQuest = 0;
		if (p.questId == QuestData::kFinalQuest)
		{
			m_endingAt = m_now + 0.4;
			m_endingStage = 0;
		}
		// 보고한 그 자리에서 다음 이야기가 이어지면, 완료 연출이 지나간 뒤 펼친다
		if (m_dialogQuest == p.questId)
		{
			m_dialogQuest = 0;
			m_dialogTextAt = m_now;
			if (m_dialogNpc != 0)
			{
				m_dialogAutoQuest = AutoQuestForNpc(m_dialogNpc);
				m_dialogAutoAt = m_now + 2.2;
			}
		}
		break;
	}
	case QuestEvent::Abandoned:
		m_quests.erase(std::remove_if(m_quests.begin(), m_quests.end(), [&](const QuestView& v) { return v.id == p.questId; }), m_quests.end());
		AddCenter("퀘스트를 포기했습니다: " + std::string(q->name), Rgb(0xC8C0B0));
		if (m_trackedQuest == p.questId) m_trackedQuest = 0;
		break;
	}
}

void Game::HandleQuestResult(const QUEST_RESULT_ACK_PACKET& p)
{
	const auto r = static_cast<QuestResult>(p.result);
	if (r == QuestResult::Ok)
		return;
	std::string text;
	switch (r)
	{
	case QuestResult::Unknown:       text = "진행 중인 퀘스트가 아닙니다."; break;
	case QuestResult::TooFar:        text = "조금 더 가까이 가서 말을 거세요."; break;
	case QuestResult::NotAvailable:  text = "지금은 받을 수 없는 퀘스트입니다."; break;
	case QuestResult::LevelTooLow:   text = Fmt("Lv.%u부터 받을 수 있습니다.", p.param); break;
	case QuestResult::WrongClass:    text = "다른 직업의 퀘스트입니다."; break;
	case QuestResult::LogFull:       text = Fmt("진행 중인 퀘스트가 너무 많습니다(최대 %d개).", QuestData::kMaxActive); break;
	case QuestResult::NotReady:      text = "아직 목표를 다 이루지 못했습니다."; break;
	case QuestResult::InventoryFull: text = "가방에 빈 칸이 없어 보상을 받을 수 없습니다."; break;
	case QuestResult::Cooldown:      text = DurationText(p.param) + " 뒤에 다시 받을 수 있습니다."; m_questCooldownUntil[p.questId] = m_now + p.param; break;
	case QuestResult::Busy:          text = "지금은 할 수 없습니다."; break;
	default:                         text = "요청을 처리하지 못했습니다."; break;
	}
	AddCenter(text, Rgb(0xFF9A8A));
	sfx::Play(sfx::Id::Error, 0.35f);
}

void Game::SendQuest(QuestAction action, uint16_t questId)
{
	if (m_now - m_questSentAt < 0.25) return;   // 연타 방지(서버도 상태로 막는다)
	m_questSentAt = m_now;
	if (action == QuestAction::Accept && static_cast<int>(m_quests.size()) >= QuestData::kMaxActive)
	{
		AddCenter(Fmt("진행 중인 퀘스트가 너무 많습니다(최대 %d개).", QuestData::kMaxActive), Rgb(0xFF9A8A));
		sfx::Play(sfx::Id::Error, 0.35f);
		return;
	}
	m_net.SendQuest(action, questId);
	sfx::Play(sfx::Id::Click, 0.5f);
}

void Game::OpenQuestInDialog(uint16_t questId)
{
	m_dialogQuest = questId;
	m_dialogTextAt = m_now;
	m_shopMode = ShopMode::None;
}

// ─────────────────────────────────────────────────────────────────────────────
// 매 프레임
// ─────────────────────────────────────────────────────────────────────────────
void Game::UpdateQuests(float dt)
{
	(void)dt;
	m_questDestValid = m_questsKnown && QuestDestination(m_questDest);

	// 날아온 수집물 도착
	Entity* me = Self();
	for (auto it = m_questFlies.begin(); it != m_questFlies.end();)
	{
		if (m_now < it->at + 0.65) { ++it; continue; }
		const QuestRow* q = QuestData::Find(it->quest);
		const QuestView* v = ActiveQuest(it->quest);
		if (q && me)
		{
			const auto& o = q->objs[it->obj];
			const int n = v ? v->progress[static_cast<size_t>(it->obj)] : o.count;
			const bool done = n >= o.count;
			fx::Fx().FloatText(TileToIso(me->pos), actors::PlayerHeight() + 18.f, Fmt("+1 %s %d/%d", ObjectiveLabel(o).c_str(), n, o.count),
				done ? Rgb(0xFFE070) : Rgb(0xF8E8B8), done ? 18.f : 16.f, 1.6f);
			fx::Fx().LootSparkle(TileToIso(me->pos), Rgb(0xFFE8A0));
			sfx::Play(done ? sfx::Id::QuestObjDone : sfx::Id::QuestTick, done ? 0.6f : 0.5f, 0.05f);
		}
		it = m_questFlies.erase(it);
	}
	while (!m_questDoneFx.empty() && m_now - m_questDoneFx.front().at > 4.2)
		m_questDoneFx.pop_front();
	while (!m_discoveries.empty() && m_now - m_discoveries.front().at > 6.0)
		m_discoveries.pop_front();
	if (m_chapterCard != 0 && m_now > m_chapterCardAt + 6.0)
		m_chapterCard = 0;
	if (m_abandonArm != 0 && m_now - m_abandonArmAt > 3.0)
		m_abandonArm = 0;
	if (m_dialogAutoQuest != 0 && m_now >= m_dialogAutoAt)
	{
		if (m_dialogNpc != 0 && m_dialogQuest == 0 && m_shopMode == ShopMode::None && CanTakeQuestId(m_dialogAutoQuest) == QuestAvail::Yes)
		{
			OpenQuestInDialog(m_dialogAutoQuest);
			sfx::Play(sfx::Id::Open, 0.5f);
		}
		m_dialogAutoQuest = 0;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 머리 위 표식 · 목표 · 보상
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuestMark(Vector2 head, QuestMark mark, float scale)
{
	if (mark == QuestMark::None) return;
	const float bob = std::sin(m_time * 3.2f) * 3.f * scale;
	const Vector2 c{ head.x, head.y - 14.f * scale + bob };
	if (mark == QuestMark::Talk)
	{
		const Rectangle b{ c.x - 15.f * scale, c.y - 10.f * scale, 30.f * scale, 20.f * scale };
		ui::Plate(Rectangle{ b.x + 1, b.y + 2, b.width, b.height }, Color{ 0, 0, 0, 100 }, BLANK, 4.f * scale, 0.f);
		ui::Plate(b, Color{ 16, 19, 23, 235 }, ui::theme::kDawn, 4.f * scale, 1.f);
		DrawTriangle(Vector2{ c.x - 4.f * scale, b.y + b.height - 1 }, Vector2{ c.x - 7.f * scale, b.y + b.height + 6.f * scale }, Vector2{ c.x + 3.f * scale, b.y + b.height - 1 }, Color{ 16, 19, 23, 235 });
		for (int i = -1; i <= 1; ++i)
		{
			const float a = 0.5f + 0.5f * std::sin(m_time * 6.f - i * 0.9f);
			DrawCircleV(Vector2{ c.x + i * 7.f * scale, c.y }, 2.1f * scale, util::Mix(ui::theme::kDawnDeep, ui::theme::kDawnHi, a));
		}
		return;
	}
	const bool bright = mark == QuestMark::Ready || mark == QuestMark::Available;
	const Color col = bright ? ui::theme::kDawnHi : Rgb(0x96908A);
	const char* glyph = (mark == QuestMark::Ready || mark == QuestMark::Active) ? "?" : "!";
	if (bright)
	{
		const float pulse = 0.5f + 0.5f * std::sin(m_time * 4.f);
		DrawCircleGradient(static_cast<int>(c.x), static_cast<int>(c.y), 22.f * scale, util::WithA(col, 0.20f + 0.10f * pulse), util::WithA(col, 0.f));
		if (mark == QuestMark::Ready)
			DrawRing(c, (16.f + 2.f * pulse) * scale, (17.5f + 2.f * pulse) * scale, 0.f, 360.f, 40, util::WithA(col, 0.45f));
	}
	ui::TextStyle st;
	st.size = 28.f * scale;
	st.color = col;
	st.outline = 2.2f;
	st.outlineColor = Color{ 20, 12, 4, 235 };
	st.face = ui::Face::Bold;
	ui::Fonts().DrawCentered(glyph, c, st);
}

float Game::DrawQuestObjectives(float x, float y, float w, const QuestRow& q, const QuestView* v, float size)
{
	const float y0 = y;
	for (int i = 0; i < QuestData::kMaxObjs; ++i)
	{
		const auto& o = q.objs[i];
		if (o.kind == ObjKind::None) continue;
		const int n = v ? v->progress[static_cast<size_t>(i)] : 0;
		const bool done = v && n >= o.count;
		const float bump = v ? static_cast<float>(std::max(0.0, 0.6 - (m_now - v->bumpAt[static_cast<size_t>(i)])) / 0.6) : 0.f;
		const Color tint = done ? ui::theme::kGood : util::Mix(ui::theme::kText, ui::theme::kDawnHi, bump);
		ObjIcon(Rectangle{ x, y + 1.f, size + 4.f, size + 4.f }, o, done ? ui::theme::kGood : Rgb(0xC8B896));
		const float lx = x + size + 12.f;
		ui::Text(ObjectiveLabel(o), Vector2{ lx, y }, size, tint);
		std::string where;
		if (o.zone != QuestData::kAnyZone && o.kind != ObjKind::KillIn && o.kind != ObjKind::Boss)
			where = texts::RegionName(o.zone);
		else if (o.kind == ObjKind::Boss && o.zone != QuestData::kAnyZone)
			where = texts::RegionName(o.zone);
		if (!where.empty())
		{
			const float lw = ui::Fonts().Measure(ObjectiveLabel(o), size).x;
			ui::Text("· " + where, Vector2{ lx + lw + 6.f, y + 1.f }, size - 2.f, ui::theme::kTextDim);
		}
		if (done)
			Check(Vector2{ x + w - 10.f, y + size * 0.5f + 1.f }, size * 0.8f, ui::theme::kGood);
		else if (v && o.count > 1)
		{
			ui::TextStyle ns; ns.size = size + 1.f; ns.color = util::Mix(ui::theme::kTextHi, ui::theme::kDawnHi, bump); ns.face = ui::Face::Number;
			const std::string cnt = Fmt("%d / %d", n, o.count);
			ui::Fonts().Draw(cnt, Vector2{ x + w - ui::Fonts().Measure(cnt, ns.size, 0.f, ui::Face::Number).x, y - 1.f }, ns);
		}
		if (v && o.count > 1)
		{
			const Rectangle bar{ lx, y + size + 3.f, w - (lx - x), 2.f };
			DrawRectangleRec(bar, Color{ 0, 0, 0, 130 });
			DrawRectangleRec(Rectangle{ bar.x, bar.y, std::round(bar.width * std::min(1.f, static_cast<float>(n) / o.count)), bar.height }, done ? ui::theme::kGood : util::Mix(ui::theme::kDawn, ui::theme::kDawnHi, bump));
		}
		y += size + (v && o.count > 1 ? 12.f : 8.f);
	}
	return y - y0;
}

float Game::DrawQuestRewards(float x, float y, float w, const QuestRow& q, bool stacked)
{
	float cx = x;
	const float h = 40.f;
	const float y0 = y;
	const uint32_t exp = QuestData::RewardExp(q, static_cast<uint8_t>(m_level));
	if (exp > 0)
	{
		const std::string t = texts::Number(exp);
		const float lw = ui::Fonts().Measure("경험치", 12.f, 0.f, ui::Face::Body).x;
		const float tw = lw + ui::Fonts().Measure(t, 15.f, 0.f, ui::Face::Number).x + 26.f;
		const Rectangle chip{ cx, y + 8.f, tw, 24.f };
		ui::Plate(chip, ui::theme::kSurface2, ui::theme::kLine, 3.f, 1.f);
		ui::Text("경험치", Vector2{ chip.x + 9.f, chip.y + 5.f }, 12.f, ui::theme::kTextDim);
		ui::TextStyle ns; ns.size = 15.f; ns.color = ui::theme::kExp; ns.face = ui::Face::Number;
		ui::Fonts().Draw(t, Vector2{ chip.x + 15.f + lw, chip.y + 4.f }, ns);
		cx += tw + 8.f;
	}
	if (q.reward.adena > 0)
	{
		const std::string t = texts::Number(q.reward.adena);
		const float tw = ui::Fonts().Measure(t, 15.f, 0.f, ui::Face::Number).x + 34.f;
		const Rectangle chip{ cx, y + 8.f, tw, 24.f };
		ui::Plate(chip, ui::theme::kSurface2, ui::theme::kLine, 3.f, 1.f);
		Coin(Vector2{ chip.x + 13.f, chip.y + 12.f }, 5.5f);
		ui::TextStyle ns; ns.size = 15.f; ns.color = ui::theme::kTextHi; ns.face = ui::Face::Number;
		ui::Fonts().Draw(t, Vector2{ chip.x + 24.f, chip.y + 4.f }, ns);
		cx += tw + 8.f;
	}
	const uint16_t item = QuestData::RewardItem(q, m_classId);
	if (stacked && (exp > 0 || q.reward.adena > 0))
	{
		// 둘째 줄에 물건
		cx = x;
		y += 34.f;
	}
	if (const ItemTableRow* row = item ? ItemTableData::Find(item) : nullptr)
	{
		const bool gear = row->type == ItemType::eEquipment;
		const Rarity r = gear ? static_cast<Rarity>(std::max(static_cast<int>(ItemRules::FloorRarity(*row)), static_cast<int>(q.reward.rarity))) : Rarity::Common;
		const Color rc = gear ? icons::RarityColor(r) : ui::theme::kBorder;
		const Rectangle slot{ cx, y, h, h };
		ui::SlotFrame(slot, ui::Hovered(slot), false, rc);
		if (gear && r >= Rarity::Rare)
			DrawCircleGradient(static_cast<int>(slot.x + h * 0.5f), static_cast<int>(slot.y + h * 0.5f), h * 0.6f, util::WithA(rc, 0.3f), util::WithA(rc, 0.f));
		icons::Draw(item, Rectangle{ slot.x + 3, slot.y + 3, h - 6, h - 6 });
		if (q.reward.count > 1)
		{
			ui::TextStyle cs; cs.size = 13.f; cs.color = ui::theme::kTextHi; cs.outline = 1.2f; cs.face = ui::Face::Number;
			const std::string cnt = Fmt("%d", q.reward.count);
			ui::Fonts().Draw(cnt, Vector2{ slot.x + h - 3.f - ui::Fonts().Measure(cnt, cs.size, 0.f, ui::Face::Number).x, slot.y + h - 15.f }, cs);
		}
		const float nx = cx + h + 8.f;
		if (nx < x + w)
		{
			ui::TextStyle is; is.size = 14.f; is.color = gear ? rc : ui::theme::kText; is.face = ui::Face::Strong;
			ui::Fonts().Draw(texts::ItemName(item), Vector2{ nx, y + 3.f }, is);
			ui::Text(gear ? std::string(ItemRules::RarityName(r)) + " 이상 · 가끔 더 좋은 것" : Fmt("%d개", q.reward.count), Vector2{ nx, y + 22.f }, 12.f, ui::theme::kTextDim);
		}
		if (ui::Hovered(slot))
			DrawItemTooltip(item, 0, q.reward.count, false, false);
		y += h;
	}
	else
		y += 32.f;
	return y - y0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 추적 표시(미니맵 아래)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuestTracker()
{
	const float W = m_uiW;
	const float w = 258.f;
	const float x = W - 14.f - w;
	float y = 256.f;
	m_trackerBottom = y;
	wm::HudScope hs("hud.quest", "퀘스트 추적", Rectangle{ x, y, w, 220.f });
	if (!m_questsKnown || m_invOpen || m_questLogOpen)
		return;

	// 메인 이야기 → 받은 순서
	std::vector<const QuestView*> list;
	for (const auto& v : m_quests) list.push_back(&v);
	std::stable_sort(list.begin(), list.end(), [](const QuestView* a, const QuestView* b)
	{
		const QuestRow* qa = QuestData::Find(a->id);
		const QuestRow* qb = QuestData::Find(b->id);
		const int ka = qa ? static_cast<int>(qa->kind) : 9, kb = qb ? static_cast<int>(qb->kind) : 9;
		if (ka != kb) return ka < kb;
		return a->acceptedAt < b->acceptedAt;
	});

	// 메인 이야기가 없으면 다음 이야기 안내
	std::string hint;
	bool hintBright = false;
	bool mainActive = false;
	for (const auto* v : list)
		if (const QuestRow* q = QuestData::Find(v->id); q && q->kind == QuestKind::Main) mainActive = true;
	if (!mainActive)
	{
		const QuestRow* next = QuestData::NextMain([&](uint16_t id) { return QuestDone(id); });
		if (next)
		{
			const QuestAvail a = CanTakeQuest(*next);
			if (a == QuestAvail::Yes) { hint = "새 이야기 — " + NpcName(next->giver); hintBright = true; }
			else if (a == QuestAvail::LowLevel) hint = Fmt("다음 이야기는 Lv.%d부터", next->minLevel);
		}
	}
	if (list.empty() && hint.empty())
		return;

	// 머리글
	{
		ui::TextStyle hs; hs.size = 13.f; hs.color = ui::theme::kTextHi; hs.face = ui::Face::Strong; hs.spacing = 0.5f; hs.shadow = 1.f; hs.shadowColor = Color{ 0, 0, 0, 170 };
		const float hw = ui::Fonts().Draw("퀘스트", Vector2{ x + 2.f, y }, hs);
		const std::string key = kb::Label(kb::Act::QuestLog);
		const float kw = std::max(15.f, std::round(ui::Fonts().Measure(key, ui::type::kMicro, 0.f, ui::Face::Strong).x + 8.f));
		ui::Keycap(Rectangle{ x + w - 37.f - kw, y + 1.f, kw, 14.f }, key);
		ui::Text("일지", Vector2{ x + w - 33.f, y }, 12.f, ui::theme::kTextDim, Align::Left, true);
		ui::Rule(x + hw + 10.f, x + w - 45.f - kw, y + 8.f, util::WithA(ui::theme::kLineStrong, 0.8f));
	}
	y += 22.f;

	const QuestView* focus = FocusQuest();
	int shown = 0;
	for (const QuestView* v : list)
	{
		if (shown >= 5) break;
		const QuestRow* q = QuestData::Find(v->id);
		if (!q) continue;
		++shown;
		const bool ready = QuestReady(*v);
		const Color kc = KindColor(q->kind);
		int lines = 0;
		if (!ready)
			for (const auto& o : q->objs) if (o.kind != ObjKind::None) ++lines;
		const float h = 24.f + (ready ? 19.f : 18.f * lines) + 6.f;
		const Rectangle b{ x, y, w, h };
		const bool hover = ui::Hovered(b);
		const bool isFocus = v == focus;
		// 막 받았으면 오른쪽에서 미끄러져 들어온다
		const float in = util::EaseOutCubic(util::Clamp01(static_cast<float>((m_now - v->acceptedAt) / 0.35)));
		const float ox = (1.f - in) * 60.f;
		const Rectangle bb{ b.x + ox, b.y, b.width, b.height };
		ui::Plate(bb, Color{ 12, 15, 18, static_cast<unsigned char>(hover ? 205 : 160) }, BLANK, 3.f, 0.f);
		DrawRectangleRec(Rectangle{ std::round(bb.x), std::round(bb.y + 4), isFocus ? 2.f : 1.f, std::round(bb.height - 8) }, util::WithA(kc, isFocus ? 1.f : 0.6f));
		if (ready)
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 4.f);
			ui::Plate(bb, BLANK, util::WithA(ui::theme::kDawn, 0.35f + 0.4f * pulse), 3.f, 1.f);
		}
		ui::Block(b);
		if (ui::Clicked(b))
		{
			m_trackedQuest = (m_trackedQuest == v->id) ? 0 : v->id;
			sfx::Play(sfx::Id::Click, 0.5f);
		}
		if (ui::DoubleClicked(b))
		{
			m_questLogOpen = true;
			m_questLogSel = v->id;
			sfx::Play(sfx::Id::Open, 0.6f);
		}
		float ly = bb.y + 5.f;
		ui::Text(KindGlyph(q->kind), Vector2{ bb.x + 10.f, ly + 1.f }, 11.f, kc);
		{
			ui::TextStyle ns; ns.size = 14.f; ns.color = ready ? ui::theme::kDawnHi : ui::theme::kTextHi; ns.face = ui::Face::Strong;
			ui::Fonts().Draw(q->name, Vector2{ bb.x + 26.f, ly - 1.f }, ns);
		}
		if (isFocus)
			ui::Text("◎", Vector2{ bb.x + w - 10.f, ly }, 12.f, util::WithA(kc, 0.9f), Align::Right);
		ly += 21.f;
		if (ready)
		{
			const std::string t = q->turnIn ? "→ " + NpcName(q->turnIn) + "에게 보고" : ("→ 일지(" + kb::Label(kb::Act::QuestLog) + ")에서 보상 받기");
			ui::Text(t, Vector2{ bb.x + 26.f, ly }, 12.5f, ui::theme::kDawn);
		}
		else
		{
			for (int i = 0; i < QuestData::kMaxObjs; ++i)
			{
				const auto& o = q->objs[i];
				if (o.kind == ObjKind::None) continue;
				const int n = v->progress[static_cast<size_t>(i)];
				const bool done = n >= o.count;
				const float bump = static_cast<float>(std::max(0.0, 0.6 - (m_now - v->bumpAt[static_cast<size_t>(i)])) / 0.6);
				const Color c = done ? ui::theme::kGood : util::Mix(ui::theme::kText, ui::theme::kDawnHi, bump);
				std::string label = ObjectiveLabel(o);
				// 너무 길면 줄인다
				while (ui::Fonts().Measure(label, 12.5f).x > w - 92.f && label.size() > 4) ui::PopUtf8(label);
				if (label != ObjectiveLabel(o)) label += "…";
				ui::Text(label, Vector2{ bb.x + 26.f + bump * 2.f, ly }, 12.5f, c);
				if (done)
					Check(Vector2{ bb.x + w - 16.f, ly + 7.f }, 10.f, ui::theme::kGood);
				else if (o.count > 1)
				{
					ui::TextStyle ns; ns.size = 14.f + bump * 2.f; ns.color = util::Mix(ui::theme::kTextHi, ui::theme::kDawnHi, bump); ns.face = ui::Face::Number;
					const std::string cnt = Fmt("%d/%d", n, o.count);
					ui::Fonts().Draw(cnt, Vector2{ bb.x + w - 10.f - ui::Fonts().Measure(cnt, ns.size, 0.f, ui::Face::Number).x, ly - 1.f - bump * 2.f }, ns);
				}
				ly += 18.f;
			}
		}
		y += h + 5.f;
	}
	if (static_cast<int>(list.size()) > shown)
	{
		ui::Text(Fmt("+%d개 더 — L", static_cast<int>(list.size()) - shown), Vector2{ x + w - 6.f, y }, 12.f, ui::theme::kTextDim, Align::Right, true);
		y += 18.f;
	}
	if (!hint.empty())
	{
		const Rectangle b{ x, y, w, 28.f };
		const float pulse = hintBright ? 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 3.5f) : 0.f;
		ui::Plate(b, Color{ 12, 15, 18, 170 }, hintBright ? util::WithA(ui::theme::kDawn, 0.3f + 0.5f * pulse) : BLANK, 3.f, 1.f);
		{
			ui::TextStyle gs; gs.size = 15.f; gs.color = hintBright ? ui::theme::kDawnHi : ui::theme::kTextDim; gs.face = ui::Face::Bold;
			ui::Fonts().DrawCentered(hintBright ? "!" : "…", Vector2{ b.x + 14.f, b.y + b.height * 0.5f }, gs);
		}
		ui::Text(hint, Vector2{ b.x + 26.f, b.y + 7.f }, 13.f, hintBright ? util::Mix(ui::theme::kDawnHi, ui::theme::kTextHi, pulse * 0.4f) : ui::theme::kTextDim);
		y += 34.f;
	}
	m_trackerBottom = y;
}

// ─────────────────────────────────────────────────────────────────────────────
// NPC 대화창 — 퀘스트 이야기(제안 · 진행 중 · 보고)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuestDialog(const WorldData::NpcRow& npc)
{
	const QuestRow* q = QuestData::Find(m_dialogQuest);
	if (!q) { m_dialogQuest = 0; return; }
	const QuestView* v = ActiveQuest(q->id);
	const bool ready = v && QuestReady(*v);
	const QuestAvail avail = v ? QuestAvail::No : CanTakeQuest(*q);
	if (!v && avail == QuestAvail::No) { m_dialogQuest = 0; return; }   // 이미 끝났거나 받을 수 없게 됐다
	enum class Mode { Offer, During, Done };
	const Mode mode = !v ? Mode::Offer : ((ready && q->turnIn == npc.id) ? Mode::Done : Mode::During);

	const float W = m_uiW;
	const float rh = 334.f;
	const Rectangle r = wm::Window("npc", Rectangle{ W * 0.5f - 350.f, std::max(12.f, HudTop() - 14.f - rh), 700.f, rh }, wm::kMove);   // 전투 HUD 위
	bool open = true;
	ui::Window(r, npc.name, &open);
	if (!open) { CloseNpcWindows(); return; }
	DrawNpcPortrait(Rectangle{ r.x + 16, r.y + 46, 120, 164 }, npc);
	// 초상화 아래: 이 이야기의 자리(장 · 종류)
	{
		const Color kc = KindColor(q->kind);
		ui::Text(KindGlyph(q->kind), Vector2{ r.x + 76.f, r.y + 220.f }, 16.f, kc, Align::Center);
		ui::Text(KindName(q->kind), Vector2{ r.x + 76.f, r.y + 242.f }, 12.f, kc, Align::Center);
		if (q->kind == QuestKind::Main)
			ui::Text(Fmt("제%d장", q->chapter), Vector2{ r.x + 76.f, r.y + 260.f }, 12.f, ui::theme::kTextDim, Align::Center);
	}

	const float x0 = r.x + 152.f, cw = r.width - 170.f;
	float y = r.y + 44.f;
	ui::Text(ChapterTag(*q), Vector2{ x0, y }, 12.f, KindColor(q->kind));
	y += 17.f;
	{
		ui::TextStyle ts; ts.size = 20.f; ts.color = ui::theme::kTextHi; ts.face = ui::Face::Display; ts.spacing = 0.6f;
		ui::Fonts().Draw(q->name, Vector2{ x0, y }, ts);
	}
	y += 31.f;

	// 이야기 — 한 글자씩(누르면 바로 다)
	const char* text = mode == Mode::Offer ? q->offer : (mode == Mode::Done ? q->done : q->during);
	std::vector<int> cps;
	ui::DecodeUtf8(text, cps);
	const int shown = std::min<int>(static_cast<int>(cps.size()), static_cast<int>((m_now - m_dialogTextAt) * 46.0));
	std::string visible;
	for (int i = 0; i < shown; ++i) ui::AppendUtf8(visible, cps[i]);
	const Rectangle tr{ x0, y, cw, 86.f };
	{
		ui::FaceScope serif(ui::Face::Serif);
		ui::TextWrapped(visible, tr, 15.f, ui::theme::kTextHi, 7.f);
	}
	const bool typing = shown < static_cast<int>(cps.size());
	if (typing && ui::Clicked(tr))
		m_dialogTextAt = m_now - 60.0;
	if (typing && m_now - m_lastVoiceAt > 0.11)
	{
		m_lastVoiceAt = m_now;
		sfx::PlayPitched(sfx::Id::Voice, 0.1f, npc.kind == WorldData::NpcKind::eChild ? 1.7f : (npc.kind == WorldData::NpcKind::eElder || npc.kind == WorldData::NpcKind::eVillager ? 0.85f : 1.15f));
	}
	y += 92.f;

	// 두 칸: 왼쪽 목표 · 오른쪽 보상(진행 중이면 보고할 곳)
	const float leftW = cw * 0.55f;
	const float rx = x0 + leftW + 14.f, rw = cw - leftW - 14.f;
	const float colY = y;
	Section("목표", x0, colY, leftW);
	if (mode == Mode::Done)
		ui::Text("모든 목표를 이루었습니다.", Vector2{ x0 + 4.f, colY + 22.f }, 14.f, ui::theme::kGood);
	else if (QuestData::ObjCount(*q) == 0)
		ui::Text(NpcName(q->turnIn) + "에게 이야기를 전한다", Vector2{ x0 + 4.f, colY + 22.f }, 14.f, ui::theme::kText);
	else
		DrawQuestObjectives(x0 + 4.f, colY + 22.f, leftW - 8.f, *q, v, 14.f);
	if (mode != Mode::During)
	{
		Section("보상", rx, colY, rw);
		DrawQuestRewards(rx + 2.f, colY + 20.f, rw - 4.f, *q, true);
	}
	else
	{
		Section("보고", rx, colY, rw);
		ui::TextWrapped(q->turnIn ? NpcName(q->turnIn) + (q->turnIn != npc.id ? "에게 돌아가세요" : "에게 다시 오세요") : std::string("목표를 이루면 그 자리에서 끝납니다"),
			Rectangle{ rx + 4.f, colY + 22.f, rw - 8.f, 60.f }, 14.f, ui::theme::kText, 4.f);
	}

	// 버튼
	float bx = x0;
	const float by = r.y + r.height - 50.f;
	auto button = [&](float w, const std::string& label, ButtonKind kind = ButtonKind::Normal, bool enabled = true)
	{
		const bool clicked = ui::Button(Rectangle{ bx, by, w, 38 }, label, kind, enabled);
		bx += w + 8.f;
		return clicked;
	};
	switch (mode)
	{
	case Mode::Offer:
		if (avail == QuestAvail::Yes)
		{
			if (button(120, "수락", ButtonKind::Primary)) SendQuest(QuestAction::Accept, q->id);
		}
		else if (avail == QuestAvail::LowLevel)
			(void)button(140, Fmt("Lv.%d부터", q->minLevel), ButtonKind::Normal, false);
		else if (avail == QuestAvail::Cooldown)
		{
			const auto it = m_questCooldownUntil.find(q->id);
			(void)button(170, DurationText(it != m_questCooldownUntil.end() ? it->second - m_now : 0.0) + " 뒤", ButtonKind::Normal, false);
		}
		if (button(96, "나중에")) { m_dialogQuest = 0; m_dialogTextAt = m_now; sfx::Play(sfx::Id::Close, 0.4f); }
		break;
	case Mode::During:
		if (button(110, "알겠소", ButtonKind::Primary)) { m_dialogQuest = 0; m_dialogTextAt = m_now; sfx::Play(sfx::Id::Close, 0.4f); }
		if (button(110, m_trackedQuest == q->id ? "추적 중" : "추적하기")) { m_trackedQuest = m_trackedQuest == q->id ? 0 : q->id; sfx::Play(sfx::Id::Click, 0.5f); }
		break;
	case Mode::Done:
		if (button(140, "보상 받기", ButtonKind::Primary)) SendQuest(QuestAction::Complete, q->id);
		if (button(96, "뒤로")) { m_dialogQuest = 0; m_dialogTextAt = m_now; }
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 퀘스트 일지(L)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuestLog()
{
	const float W = m_uiW, H = m_uiH;
	const float rh = std::min(540.f, H - 150.f);
	const Rectangle r = wm::Window("quest", Rectangle{ W * 0.5f - 390.f, std::max(12.f, H * 0.5f - rh * 0.5f - 30.f), 780.f, rh }, wm::kMove);
	ui::Window(r, "퀘스트 일지", &m_questLogOpen);
	if (!m_questLogOpen) { sfx::Play(sfx::Id::Close, 0.5f); return; }

	// ── 왼쪽: 목록 ──
	const Rectangle list{ r.x + 12.f, r.y + 44.f, 262.f, r.height - 56.f };
	ui::Plate(list, Color{ 11, 13, 16, 200 }, ui::theme::kLine, 3.f, 1.f);
	struct Row { const QuestRow* q; const QuestView* v; bool header; const char* title; bool past; };
	std::vector<Row> rows;
	const QuestKind order[] = { QuestKind::Main, QuestKind::Side, QuestKind::Daily, QuestKind::Class };
	for (QuestKind k : order)
	{
		bool any = false;
		for (const auto& v : m_quests)
			if (const QuestRow* q = QuestData::Find(v.id); q && q->kind == k)
			{
				if (!any) { rows.push_back(Row{ nullptr, nullptr, true, KindName(k), false }); any = true; }
				rows.push_back(Row{ q, &v, false, nullptr, false });
			}
	}
	{
		bool any = false;
		for (const auto& q : QuestData::kQuests)
			if (q.kind == QuestKind::Main && QuestDone(q.id))
			{
				if (!any) { rows.push_back(Row{ nullptr, nullptr, true, "지난 이야기", true }); any = true; }
				rows.push_back(Row{ &q, nullptr, false, nullptr, true });
			}
	}
	// 고른 것이 없으면 추적 중인 것
	bool selValid = false;
	for (const auto& row : rows) if (row.q && row.q->id == m_questLogSel) selValid = true;
	if (!selValid)
	{
		const QuestView* f = FocusQuest();
		m_questLogSel = f ? f->id : 0;
		if (!m_questLogSel)
			for (const auto& row : rows) if (row.q) { m_questLogSel = row.q->id; break; }
	}
	const float rowH = 26.f;
	float contentH = 0.f;
	for (const auto& row : rows) contentH += row.header ? 24.f : rowH;
	if (ui::Hovered(list))
		m_questLogScroll -= GetMouseWheelMove() * 40.f;
	m_questLogScroll = std::clamp(m_questLogScroll, 0.f, std::max(0.f, contentH - list.height + 12.f));
	ui::BeginClip(Rectangle{ list.x, list.y, list.width, list.height });
	float y = list.y + 6.f - m_questLogScroll;
	for (const auto& row : rows)
	{
		if (row.header)
		{
			ui::SectionHeader(Vector2{ list.x + 10.f, y + 5.f }, list.width - 20.f, row.title);
			y += 24.f;
			continue;
		}
		const Rectangle rr{ list.x + 4.f, y, list.width - 8.f, rowH - 2.f };
		const bool sel = row.q->id == m_questLogSel;
		const bool hover = ui::Hovered(rr) && ui::Hovered(list);
		if (sel || hover)
			ui::Plate(rr, sel ? ui::theme::kSurface3 : ui::theme::kSurface2, BLANK, 2.f, 0.f);
		if (sel)
			DrawRectangleRec(Rectangle{ std::round(rr.x), std::round(rr.y + 3.f), 2.f, std::round(rr.height - 6.f) }, ui::theme::kDawn);
		const Color kc = row.past ? ui::theme::kTextDim : KindColor(row.q->kind);
		ui::Text(KindGlyph(row.q->kind), Vector2{ rr.x + 10.f, rr.y + 6.f }, 11.f, kc);
		std::string name = row.q->name;
		ui::Text(name, Vector2{ rr.x + 26.f, rr.y + 5.f }, ui::type::kBodyS + 0.5f, row.past ? ui::theme::kTextDim : (sel ? ui::theme::kTextHi : ui::theme::kText));
		if (row.v)
		{
			if (QuestReady(*row.v)) Check(Vector2{ rr.x + rr.width - 14.f, rr.y + 12.f }, 10.f, ui::theme::kDawnHi);
			else
			{
				int done = 0, total = 0;
				for (int i = 0; i < QuestData::kMaxObjs; ++i)
					if (row.q->objs[i].kind != ObjKind::None) { ++total; if (row.v->progress[static_cast<size_t>(i)] >= row.q->objs[i].count) ++done; }
				ui::Text(Fmt("%d/%d", done, total), Vector2{ rr.x + rr.width - 8.f, rr.y + 5.f }, 12.f, ui::theme::kTextDim, Align::Right);
			}
			if (row.q->id == (FocusQuest() ? FocusQuest()->id : 0))
				ui::Text("◎", Vector2{ rr.x + rr.width - 36.f, rr.y + 5.f }, 12.f, kc, Align::Right);
		}
		if (hover && ui::Clicked(rr)) { m_questLogSel = row.q->id; m_abandonArm = 0; sfx::Play(sfx::Id::Click, 0.4f); }
		y += rowH;
	}
	ui::EndClip();
	if (rows.empty())
		ui::TextWrapped("아직 받은 이야기가 없습니다. 마을 사람들 머리 위의 ! 를 찾아 말을 걸어 보세요.", Rectangle{ list.x + 14.f, list.y + 14.f, list.width - 28.f, 120.f }, 14.f, ui::theme::kTextDim, 5.f);

	// ── 오른쪽: 자세히 ──
	const QuestRow* q = QuestData::Find(m_questLogSel);
	const float x0 = r.x + 292.f, cw = r.width - 306.f;
	float dy = r.y + 48.f;
	if (!q)
	{
		ui::FaceScope serif(ui::Face::Serif);
		ui::TextWrapped("하르벤이 멈춘 뒤로, 새벽은 날마다 조금씩 늦게 왔다.\n\n데니차 광장의 촌장 고란이 당신을 기다립니다.",
			Rectangle{ x0, dy + 20.f, cw, 200.f }, 16.f, ui::theme::kTextHi, 8.f);
		return;
	}
	const QuestView* v = ActiveQuest(q->id);
	const Color kc = KindColor(q->kind);
	ui::Text(ChapterTag(*q), Vector2{ x0, dy }, 12.f, kc);
	dy += 18.f;
	{
		ui::TextStyle ts; ts.size = 21.f; ts.color = ui::theme::kTextHi; ts.face = ui::Face::Display; ts.spacing = 0.6f;
		ui::Fonts().Draw(q->name, Vector2{ x0, dy }, ts);
	}
	dy += 33.f;
	if (!v)
	{
		// 지난 이야기 — 이야기책처럼 다시 읽기
		ui::Text(q->summary, Vector2{ x0, dy }, ui::type::kBodyS, ui::theme::kTextDim);
		dy += 26.f;
		{
			ui::FaceScope serif(ui::Face::Serif);
			dy += ui::TextWrapped(q->done, Rectangle{ x0, dy, cw, 200.f }, 15.f, ui::theme::kTextHi, 8.f) + 16.f;
		}
		ui::Text(Fmt("주는 사람 · %s", NpcName(q->giver).c_str()), Vector2{ x0, dy }, 12.f, ui::theme::kTextDim);
		return;
	}
	dy += ui::TextWrapped(q->summary, Rectangle{ x0, dy, cw, 60.f }, ui::type::kBody, ui::theme::kTitle, 6.f) + 10.f;
	{
		ui::FaceScope serif(ui::Face::Serif);
		dy += ui::TextWrapped(q->offer, Rectangle{ x0, dy, cw, 140.f }, 13.5f, ui::theme::kTextDim, 6.f) + 14.f;
	}
	Section("목표", x0, dy, cw);
	dy += 22.f;
	if (QuestData::ObjCount(*q) == 0)
	{
		ui::Text(NpcName(q->turnIn) + "에게 이야기를 전한다", Vector2{ x0 + 4.f, dy }, 14.f, ui::theme::kText);
		dy += 22.f;
	}
	else
		dy += DrawQuestObjectives(x0 + 4.f, dy, cw - 8.f, *q, v, 14.f);
	dy += 6.f;
	Section("보상", x0, dy, cw);
	dy += 22.f;
	dy += DrawQuestRewards(x0 + 4.f, dy, cw - 8.f, *q) + 12.f;
	const bool ready = QuestReady(*v);
	ui::Text(q->turnIn ? "보고 · " + NpcName(q->turnIn) : std::string("목표를 이루면 그 자리에서 끝납니다"), Vector2{ x0, dy }, ui::type::kCaption + 0.5f, ready ? ui::theme::kDawnHi : ui::theme::kTextDim);

	// 버튼
	float bx = x0;
	const float by = r.y + r.height - 52.f;
	auto button = [&](float w, const std::string& label, ButtonKind kind = ButtonKind::Normal, bool enabled = true)
	{
		const bool clicked = ui::Button(Rectangle{ bx, by, w, 38 }, label, kind, enabled);
		bx += w + 8.f;
		return clicked;
	};
	const bool tracked = FocusQuest() && FocusQuest()->id == q->id;
	if (button(128, tracked ? "추적 중" : "추적하기", tracked ? ButtonKind::Primary : ButtonKind::Normal))
	{
		m_trackedQuest = tracked && m_trackedQuest == q->id ? 0 : q->id;
		sfx::Play(sfx::Id::Click, 0.5f);
	}
	if (ready && q->turnIn == 0 && button(128, "보상 받기", ButtonKind::Primary))
		SendQuest(QuestAction::Complete, q->id);
	const bool armed = m_abandonArm == q->id;
	if (button(armed ? 150.f : 96.f, armed ? "정말 포기할까요?" : "포기", ButtonKind::Danger))
	{
		if (armed) { SendQuest(QuestAction::Abandon, q->id); m_abandonArm = 0; }
		else { m_abandonArm = q->id; m_abandonArmAt = m_now; sfx::Play(sfx::Id::Click, 0.4f); }
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 월드 · 나침반 · 날아오는 수집물
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawQuestWorld(bool groundPass)
{
	if (!m_questDestValid || !m_questDest.poi) return;
	const QuestData::Poi* poi = nullptr;
	for (const auto& p : QuestData::kPois)
		if (p.x == static_cast<int>(m_questDest.tile.x) && p.y == static_cast<int>(m_questDest.tile.y)) poi = &p;
	if (!poi) return;
	const Vector2 iso = TileToIso(m_questDest.tile);
	const float t = m_time;
	const Color gold = ui::theme::kDawnHi;
	if (groundPass)
	{
		// 이 안에 들어서면 된다 — 천천히 도는 점선 고리 + 은은한 바닥 빛
		const float rx = poi->radius * 45.f, ry = poi->radius * 22.5f;
		const float pulse = 0.5f + 0.5f * std::sin(t * 2.f);
		gfx::EllipseGrad(iso, rx, ry, util::WithA(gold, 0.10f + 0.05f * pulse), util::WithA(gold, 0.f));
		for (int k = 0; k < 24; ++k)
		{
			const float a = t * 0.25f + k * util::kPi * 2.f / 24.f;
			gfx::EllipseArc(iso, rx, ry, a, a + 0.16f, 2.5f, util::WithA(gold, 0.55f));
		}
		gfx::EllipseArc(iso, rx * 0.25f, ry * 0.25f, 0.f, util::kPi * 2.f, 2.f, util::WithA(gold, 0.5f + 0.3f * pulse));
	}
	else
	{
		// 멀리서도 보이는 빛기둥
		gfx::SetDepth(m_questDest.tile.x + m_questDest.tile.y);
		gfx::Additive(true);
		const float flick = 0.85f + 0.15f * std::sin(t * 5.3f);
		gfx::LineGrad(iso, Vector2{ iso.x, iso.y - 420.f }, 30.f, 6.f, util::WithA(gold, 0.35f * flick), util::WithA(gold, 0.f));
		gfx::LineGrad(iso, Vector2{ iso.x, iso.y - 300.f }, 10.f, 2.f, util::WithA(Rgb(0xFFFFF0), 0.55f * flick), util::WithA(gold, 0.f));
		for (int k = 0; k < 6; ++k)
		{
			const float u = std::fmod(t * 0.35f + k / 6.f, 1.f);
			gfx::Circle(Vector2{ iso.x + std::sin(t * 1.3f + k * 2.1f) * 12.f, iso.y - u * 260.f }, 2.5f * (1.f - u), util::WithA(gold, 0.8f * (1.f - u)));
		}
		gfx::Additive(false);
		gfx::ResetMod();
	}
}

void Game::DrawQuestCompass(const Camera2D& cam)
{
	if (!m_questDestValid || m_dead) return;
	const float W = static_cast<float>(m_screenW), H = static_cast<float>(m_screenH);
	const Vector2 sp = GetWorldToScreen2D(TileToIso(m_questDest.tile), cam);
	const Rectangle inner{ 90.f, 90.f, W - 180.f, H - 250.f };
	const int dist = Cheb(m_predX, m_predY, static_cast<int>(m_questDest.tile.x), static_cast<int>(m_questDest.tile.y));
	const float t = static_cast<float>(m_now);
	const Color gold = ui::theme::kDawnHi;
	if (CheckCollisionPointRec(sp, inner))
	{
		// 화면 안: NPC는 머리 위 ! ? 가 있으니 장소·사냥터만 떠 있는 표식
		if (m_questDest.npc || dist <= 2) return;
		const float bob = std::sin(t * 3.f) * 5.f;
		const Vector2 c{ sp.x, sp.y - 70.f + bob };
		DrawTriangle(Vector2{ c.x - 11.f, c.y - 8.f }, Vector2{ c.x, c.y + 10.f }, Vector2{ c.x + 11.f, c.y - 8.f }, Color{ 40, 24, 6, 220 });
		DrawTriangle(Vector2{ c.x - 8.f, c.y - 6.f }, Vector2{ c.x, c.y + 6.f }, Vector2{ c.x + 8.f, c.y - 6.f }, gold);
		ui::TextStyle st;
		st.size = 13.5f;
		st.color = ui::theme::kTextHi;
		st.outline = 1.4f;
		st.outlineColor = Color{ 0, 0, 0, 200 };
		st.face = ui::Face::Strong;
		ui::Fonts().DrawCentered(m_questDest.label, Vector2{ c.x, c.y - 20.f }, st);
		return;
	}
	// 화면 밖: 가장자리 화살표(가리키는 쪽으로) + 이름 · 거리
	const Vector2 center{ W * 0.5f, H * 0.5f - 40.f };
	Vector2 d = util::Norm(util::Sub(sp, center));
	if (util::Len(d) < 0.5f) d = Vector2{ 0.f, -1.f };
	float k = 1e9f;
	if (d.x > 1e-4f) k = std::min(k, (inner.x + inner.width - center.x) / d.x);
	if (d.x < -1e-4f) k = std::min(k, (inner.x - center.x) / d.x);
	if (d.y > 1e-4f) k = std::min(k, (inner.y + inner.height - center.y) / d.y);
	if (d.y < -1e-4f) k = std::min(k, (inner.y - center.y) / d.y);
	const Vector2 p = util::Add(center, util::Mul(d, k));
	const float pulse = 0.5f + 0.5f * std::sin(t * 4.f);
	const Vector2 n{ -d.y, d.x };
	const Vector2 tip = util::Add(p, util::Mul(d, 14.f + pulse * 4.f));
	const Vector2 b1 = util::Add(util::Add(p, util::Mul(d, -6.f)), util::Mul(n, 11.f));
	const Vector2 b2 = util::Add(util::Add(p, util::Mul(d, -6.f)), util::Mul(n, -11.f));
	DrawCircleGradient(static_cast<int>(p.x), static_cast<int>(p.y), 26.f, util::WithA(gold, 0.25f + 0.15f * pulse), util::WithA(gold, 0.f));
	DrawTriangle(tip, b2, b1, Color{ 40, 24, 6, 230 });
	DrawTriangle(util::Add(tip, util::Mul(d, -3.f)), util::Add(b2, util::Mul(util::Sub(p, b2), 0.25f)), util::Add(b1, util::Mul(util::Sub(p, b1), 0.25f)), gold);
	const std::string label = m_questDest.label + Fmt(" · %d칸", dist);
	const float tw = ui::Fonts().Measure(label, 12.5f).x;
	Vector2 lp = util::Add(p, util::Mul(d, -34.f));
	lp.x = std::clamp(lp.x, tw * 0.5f + 12.f, W - tw * 0.5f - 12.f);
	const Rectangle lb{ std::round(lp.x - tw * 0.5f - 9.f), std::round(lp.y - 11.f), std::round(tw + 18.f), 22.f };
	ui::Plate(lb, Color{ 14, 17, 20, 210 }, util::WithA(ui::theme::kDawn, 0.6f), 3.f, 1.f);
	ui::Text(label, Vector2{ lp.x, lb.y + 4.f }, 12.5f, ui::theme::kTextHi, Align::Center);
}

void Game::DrawQuestOverlay(const Camera2D& cam)
{
	Entity* me = Self();
	if (!me) return;
	const Vector2 to = GetWorldToScreen2D(util::Add(TileToIso(me->pos), Vector2{ 0.f, -actors::PlayerHeight() * 0.6f }), cam);
	for (const auto& f : m_questFlies)
	{
		const float u = static_cast<float>((m_now - f.at) / 0.65);
		if (u < 0.f || u > 1.f) continue;
		const QuestRow* q = QuestData::Find(f.quest);
		if (!q) continue;
		const Vector2 from = GetWorldToScreen2D(util::Add(TileToIso(f.from), Vector2{ 0.f, -24.f }), cam);
		const float e = util::EaseInOutCubic(u);
		Vector2 pos = util::LerpV(from, to, e);
		pos.y -= std::sin(util::kPi * u) * 90.f * cam.zoom;
		const float size = (34.f - 10.f * u) * std::max(0.7f, cam.zoom);
		// 꼬리 반짝임
		for (int k = 1; k <= 5; ++k)
		{
			const float uu = std::max(0.f, u - k * 0.04f);
			Vector2 tp = util::LerpV(from, to, util::EaseInOutCubic(uu));
			tp.y -= std::sin(util::kPi * uu) * 90.f * cam.zoom;
			DrawCircleV(tp, 4.f * (1.f - k / 6.f), util::WithA(Rgb(0xFFE8A0), 0.6f * (1.f - k / 6.f)));
		}
		DrawCircleGradient(static_cast<int>(pos.x), static_cast<int>(pos.y), size * 0.9f, util::WithA(Rgb(0xFFE8A0), 0.45f), util::WithA(Rgb(0xFFE8A0), 0.f));
		icons::DrawQuestItem(static_cast<uint8_t>(q->objs[f.obj].icon), Rectangle{ pos.x - size * 0.5f, pos.y - size * 0.5f, size, size });
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 장 제목 · 퀘스트 완료 · 새 장소 · 엔딩
// ─────────────────────────────────────────────────────────────────────────────
float Game::DrawQuestStackCards(float y0)
{
	const float W = m_uiW;
	float y = y0;
	// 글자: 계열(face)을 골라 쓴다 — 제목은 Display, 이야기 한 줄은 Serif, 라벨은 Strong
	auto centered = [&](const std::string& s, float cx, float cy, float size, Color c, float outline = 0.f, ui::Face face = ui::Face::Display, float spacing = 0.f)
	{
		ui::TextStyle st;
		st.size = size;
		st.color = c;
		st.outline = outline;
		st.outlineColor = util::MulA(Color{ 0, 0, 0, 200 }, c.a / 255.f);
		st.shadow = size >= 24.f ? 2.f : 1.f;
		st.shadowColor = util::MulA(Color{ 0, 0, 0, 190 }, c.a / 255.f);
		st.face = face;
		st.spacing = spacing;
		ui::Fonts().DrawCentered(s, Vector2{ cx + spacing * 0.5f, cy }, st);
	};
	// 지평선과 샛별(로고와 같은 표식)
	auto ornament = [&](float cx, float cy, float halfW, Color c)
	{
		DrawRectangleGradientH(static_cast<int>(cx - halfW), static_cast<int>(cy), static_cast<int>(halfW - 11.f), 1, util::WithA(c, 0.f), c);
		DrawRectangleGradientH(static_cast<int>(cx + 11.f), static_cast<int>(cy), static_cast<int>(halfW - 11.f), 1, c, util::WithA(c, 0.f));
		const Vector2 m{ cx, cy + 0.5f };
		DrawTriangle(Vector2{ m.x, m.y - 6.f }, Vector2{ m.x - 4.f, m.y }, Vector2{ m.x + 4.f, m.y }, c);
		DrawTriangle(Vector2{ m.x - 4.f, m.y }, Vector2{ m.x, m.y + 6.f }, Vector2{ m.x + 4.f, m.y }, c);
	};
	auto band = [&](float top, float h, float a)
	{
		DrawRectangleGradientH(static_cast<int>(W * 0.5f - 360.f), static_cast<int>(top), 360, static_cast<int>(h), util::WithA(BLACK, 0.f), util::WithA(Color{ 10, 12, 15, 255 }, 0.82f * a));
		DrawRectangleGradientH(static_cast<int>(W * 0.5f), static_cast<int>(top), 360, static_cast<int>(h), util::WithA(Color{ 10, 12, 15, 255 }, 0.82f * a), util::WithA(BLACK, 0.f));
	};

	// ── 새 장소(탐험) — 이야기책 한 줄 ──
	if (!m_discoveries.empty())
	{
		const Discovery& d = m_discoveries.front();
		const float t = static_cast<float>(m_now - d.at);
		const auto* poi = QuestData::FindPoi(d.poi);
		if (poi && t >= 0.f)
		{
			const float a = util::Clamp01(t / 0.6f) * util::Clamp01((5.6f - t) / 0.8f);
			const float h = 104.f;
			band(y, h, a);
			centered("새로운 장소", W * 0.5f, y + 14.f, 12.f, util::WithA(ui::theme::kTextDim, a), 0.f, ui::Face::Strong, 3.f);
			const float rise = (1.f - util::EaseOutCubic(util::Clamp01(t / 0.8f))) * 10.f;
			centered(poi->name, W * 0.5f, y + 42.f + rise, 30.f, util::WithA(Color{ 238, 229, 208, 255 }, a), 0.f, ui::Face::Display, 2.f);
			ornament(W * 0.5f, y + 63.f, 220.f, util::WithA(ui::theme::kDawn, a));
			centered(poi->flavor, W * 0.5f, y + 84.f, 15.f, util::WithA(ui::theme::kText, a * util::Clamp01((t - 0.7f) / 0.8f)), 0.f, ui::Face::Serif);
			y += h + 8.f;
		}
	}

	// ── 퀘스트 완료 — 이름과 보상이 차례로 튀어나온다 ──
	if (!m_questDoneFx.empty())
	{
		const QuestDoneFx& f = m_questDoneFx.front();
		const float t = static_cast<float>(m_now - f.at);
		const QuestRow* q = QuestData::Find(f.quest);
		if (q && t >= 0.f && f.quest != QuestData::kFinalQuest)
		{
			const float a = util::Clamp01(t / 0.25f) * util::Clamp01((4.1f - t) / 0.5f);
			const float h = 110.f;
			band(y, h, a);
			const Color kc = KindColor(q->kind);
			centered("퀘스트 완료", W * 0.5f, y + 14.f, 12.f, util::WithA(ui::theme::kDawn, a), 0.f, ui::Face::Strong, 3.f);
			ornament(W * 0.5f, y + 27.f, 170.f, util::WithA(kc, a));
			const float pop = util::EaseOutBack(util::Clamp01(t / 0.35f), 2.2f);
			centered(q->name, W * 0.5f, y + 48.f, 25.f * (0.6f + 0.4f * pop), util::WithA(Color{ 238, 229, 208, 255 }, a), 0.f, ui::Face::Display, 1.f);
			struct Chip { std::string text; Color color; uint16_t item; };
			std::vector<Chip> chips;
			if (f.exp) chips.push_back(Chip{ "경험치 +" + texts::Number(f.exp), ui::theme::kExp, 0 });
			if (f.adena) chips.push_back(Chip{ "그로시 +" + texts::Number(f.adena), ui::theme::kTextHi, 0 });
			if (f.item)
			{
				const ItemTableRow* row = ItemTableData::Find(f.item);
				const bool gear = row && row->type == ItemType::eEquipment;
				chips.push_back(Chip{ texts::ItemName(f.item) + (f.count > 1 ? Fmt(" ×%d", f.count) : "") + (gear ? std::string(" · ") + ItemRules::RarityName(static_cast<Rarity>(std::min<int>(f.rarity, 5))) : std::string()),
					gear ? icons::RarityColor(static_cast<Rarity>(std::min<int>(f.rarity, 5))) : Rgb(0xE8E4DA), f.item });
			}
			float total = 0.f;
			for (const auto& c : chips) total += ui::Fonts().Measure(c.text, 15.f).x + (c.item ? 40.f : 20.f) + 10.f;
			float cx = W * 0.5f - total * 0.5f;
			for (size_t i = 0; i < chips.size(); ++i)
			{
				const float ct = t - 0.35f - i * 0.18f;
				if (ct < 0.f) break;
				const float cp = util::EaseOutBack(util::Clamp01(ct / 0.3f), 2.5f);
				const auto& c = chips[i];
				const float cw = ui::Fonts().Measure(c.text, 15.f).x + (c.item ? 40.f : 20.f);
				const Rectangle r{ cx, y + 70.f + (1.f - cp) * 8.f, cw, 28.f };
				ui::Plate(r, util::WithA(Color{ 16, 19, 23, 255 }, 0.9f * a), util::WithA(c.item ? c.color : ui::theme::kLine, 0.8f * a), 3.f, 1.f);
				float tx = r.x + 10.f;
				if (c.item)
				{
					icons::Draw(c.item, Rectangle{ r.x + 5.f, r.y + 1.f, 26.f, 26.f }, util::WithA(WHITE, a));
					tx += 26.f;
				}
				ui::TextStyle cs; cs.size = 14.f; cs.color = util::WithA(c.color, a); cs.face = ui::Face::Strong;
				ui::Fonts().Draw(c.text, Vector2{ tx, r.y + 6.f }, cs);
				cx += cw + 10.f;
			}
			y += h + 8.f;
		}
	}
	return y - y0;
}

void Game::DrawQuestScreenFx()
{
	const float W = m_uiW, H = m_uiH;
	// 글자: 계열(face)을 골라 쓴다 — 제목은 Display, 이야기 한 줄은 Serif, 라벨은 Strong
	auto centered = [&](const std::string& s, float cx, float cy, float size, Color c, float outline = 0.f, ui::Face face = ui::Face::Display, float spacing = 0.f)
	{
		ui::TextStyle st;
		st.size = size;
		st.color = c;
		st.outline = outline;
		st.outlineColor = util::MulA(Color{ 0, 0, 0, 200 }, c.a / 255.f);
		st.shadow = size >= 24.f ? 2.f : 1.f;
		st.shadowColor = util::MulA(Color{ 0, 0, 0, 190 }, c.a / 255.f);
		st.face = face;
		st.spacing = spacing;
		ui::Fonts().DrawCentered(s, Vector2{ cx + spacing * 0.5f, cy }, st);
	};
	// 지평선과 샛별(로고와 같은 표식)
	auto ornament = [&](float cx, float cy, float halfW, Color c)
	{
		DrawRectangleGradientH(static_cast<int>(cx - halfW), static_cast<int>(cy), static_cast<int>(halfW - 11.f), 1, util::WithA(c, 0.f), c);
		DrawRectangleGradientH(static_cast<int>(cx + 11.f), static_cast<int>(cy), static_cast<int>(halfW - 11.f), 1, c, util::WithA(c, 0.f));
		const Vector2 m{ cx, cy + 0.5f };
		DrawTriangle(Vector2{ m.x, m.y - 6.f }, Vector2{ m.x - 4.f, m.y }, Vector2{ m.x + 4.f, m.y }, c);
		DrawTriangle(Vector2{ m.x - 4.f, m.y }, Vector2{ m.x, m.y + 6.f }, Vector2{ m.x + 4.f, m.y }, c);
	};

	// ── 장 제목 카드 ──
	if (m_chapterCard > 0 && m_chapterCard <= QuestData::kChapterCount && m_now >= m_chapterCardAt)
	{
		const float t = static_cast<float>(m_now - m_chapterCardAt);
		if (!m_chapterBellPlayed) { m_chapterBellPlayed = true; sfx::Play(sfx::Id::ChapterBell, 0.8f, 0.f); }
		const float a = util::Clamp01(t / 0.9f) * util::Clamp01((5.2f - t) / 1.1f);
		const auto& ch = QuestData::kChapters[m_chapterCard];
		DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, util::WithA(BLACK, 0.72f * a));
		const float cy = H * 0.42f;
		// 장 그림: 화면을 가로지르는 넓은 띠(영화 화면비)로 천천히 다가온다. 위아래 가장자리는 어둠에 녹인다.
		if (const Texture2D* plate = art::Chapter(m_chapterCard))
		{
			struct PlateLook { float focusY; Vector2 glint; Color glintColor; };
			static const PlateLook kLook[] = {
				{ 0.50f, Vector2{ 0.f, 0.f },        BLANK },                          // (0 없음)
				{ 0.52f, Vector2{ 0.363f, 0.069f },  Color{ 255, 226, 170, 255 } },    // 1 데니차 — 샛별
				{ 0.45f, Vector2{ 0.184f, 0.098f },  Color{ 255, 226, 170, 255 } },    // 2 올벤 숲 — 샛별
				{ 0.38f, Vector2{ 0.f, 0.f },        BLANK },                          // 3 캄파나 — 종탑
				{ 0.42f, Vector2{ 0.f, 0.f },        BLANK },                          // 4 서약의 언덕
				{ 0.56f, Vector2{ 0.498f, 0.355f },  Color{ 255, 150, 80, 255 } },     // 5 칼둔 균열 — 불씨(글자 위로 올라가게)
			};
			const PlateLook& look = kLook[std::min<int>(m_chapterCard, 5)];
			const float bandH = std::round(std::min(H * 0.64f, W * 0.40f));
			const Rectangle band{ 0.f, std::round(cy - bandH * 0.54f), W, bandH };
			const float zoom = 1.02f + 0.06f * util::EaseOutCubic(util::Clamp01(t / 6.5f));
			const Vector2 focus{ 0.5f, look.focusY };
			art::DrawCover(*plate, band, util::WithA(WHITE, a), focus, zoom);
			if (look.glintColor.a > 0)
			{
				const Vector2 g = art::CoverPoint(*plate, band, focus, zoom, look.glint);
				if (g.y > band.y && g.y < band.y + band.height)
					art::Glint(g, 4.5f * H / 720.f, look.glintColor, art::StarPulse(t, 3.f) * a);
			}
			// 가장자리: 띠 안쪽은 어둠으로 녹이고, 바깥쪽도 한 번 더 가라앉혀 경계선이 보이지 않게 한다
			const int f = static_cast<int>(bandH * 0.24f);
			const int o = static_cast<int>(H * 0.1f);
			const int top = static_cast<int>(band.y), bottom = static_cast<int>(band.y + band.height);
			DrawRectangleGradientV(0, top, static_cast<int>(std::ceil(m_uiW)), f, util::WithA(BLACK, 0.9f * a), util::WithA(BLACK, 0.f));
			DrawRectangleGradientV(0, bottom - f, static_cast<int>(std::ceil(m_uiW)), f, util::WithA(BLACK, 0.f), util::WithA(BLACK, 0.9f * a));
			DrawRectangleGradientV(0, top - o, static_cast<int>(std::ceil(m_uiW)), o, util::WithA(BLACK, 0.f), util::WithA(BLACK, 0.75f * a));
			DrawRectangleGradientV(0, bottom, static_cast<int>(std::ceil(m_uiW)), o, util::WithA(BLACK, 0.75f * a), util::WithA(BLACK, 0.f));
			// 글자 자리(가운데)는 조금 더 가라앉힌다
			DrawRectangleGradientV(0, static_cast<int>(cy - 110.f), static_cast<int>(std::ceil(m_uiW)), 110, util::WithA(BLACK, 0.f), util::WithA(BLACK, 0.5f * a));
			DrawRectangleGradientV(0, static_cast<int>(cy), static_cast<int>(std::ceil(m_uiW)), 130, util::WithA(BLACK, 0.5f * a), util::WithA(BLACK, 0.f));
		}
		else
		{
			DrawRectangleGradientV(0, static_cast<int>(cy - 120.f), static_cast<int>(W), 120, util::WithA(BLACK, 0.f), util::WithA(BLACK, 0.35f * a));
			DrawRectangleGradientV(0, static_cast<int>(cy), static_cast<int>(W), 140, util::WithA(BLACK, 0.35f * a), util::WithA(BLACK, 0.f));
		}
		const float rise = (1.f - util::EaseOutCubic(util::Clamp01(t / 1.2f))) * 16.f;
		centered(Fmt("제%d장", m_chapterCard), W * 0.5f, cy - 60.f + rise * 0.5f, 17.f, util::WithA(ui::theme::kDawn, a), 0.f, ui::Face::Display, 6.f);
		ornament(W * 0.5f, cy - 40.f, 200.f, util::WithA(ui::theme::kDawn, a * 0.9f));
		centered(ch.title, W * 0.5f, cy - 4.f + rise, 46.f, util::WithA(Color{ 240, 231, 210, 255 }, a), 0.f, ui::Face::Display, 4.f);
		centered(ch.place, W * 0.5f, cy + 40.f, 15.f, util::WithA(ui::theme::kTextDim, a * util::Clamp01((t - 0.5f) / 0.8f)), 0.f, ui::Face::Strong, 4.f);
		centered(ch.line, W * 0.5f, cy + 76.f, 17.f, util::WithA(ui::theme::kText, a * util::Clamp01((t - 1.1f) / 0.9f)), 0.f, ui::Face::Serif);
	}

	// ── 엔딩: 하르벤이 다시 뛴다 ──
	if (m_endingAt > 0.0 && m_now >= m_endingAt)
	{
		const float t = static_cast<float>(m_now - m_endingAt);
		if (t > 16.f) return;   // 글은 끝났다(새벽빛은 Frame에서 잠시 더 머문다)
		const float beats[] = { 0.f, 1.25f, 2.5f };
		for (int i = 0; i < 3; ++i)
			if (m_endingStage == i && t >= beats[i])
			{
				m_endingStage = i + 1;
				sfx::Play(sfx::Id::Heartbeat, 1.f - i * 0.1f, 0.f);
				fx::Fx().Flash(Rgb(0xFFE8B0), 0.18f + i * 0.06f, 0.5f);
				m_feelTrauma = std::max(m_feelTrauma, 0.35f);
			}
		if (m_endingStage == 3 && t >= 3.8f) { m_endingStage = 4; sfx::Play(sfx::Id::Discover, 0.9f, 0.f); sfx::Play(sfx::Id::QuestComplete, 0.7f, 0.f); fx::Fx().Flash(Rgb(0xFFF0C8), 0.6f, 1.2f); }
		if (m_endingStage == 4 && t >= 10.5f) { m_endingStage = 5; sfx::Play(sfx::Id::ChapterBell, 0.7f, 0.f); }

		// 어둠 → 심장 박동(가운데서 번지는 빛) → 새벽
		const float dark = util::Clamp01(t / 0.8f) * (1.f - util::Clamp01((t - 3.8f) / 2.5f)) * 0.78f;
		DrawRectangleRec(Rectangle{ 0, 0, m_uiW, m_uiH }, util::WithA(BLACK, dark));
		for (int i = 0; i < 3; ++i)
		{
			const float u = (t - beats[i]) / 1.1f;
			if (u < 0.f || u > 1.f) continue;
			DrawCircleGradient(static_cast<int>(W * 0.5f), static_cast<int>(H * 0.45f), 60.f + u * W * 0.6f, util::WithA(Rgb(0xFFD890), 0.35f * (1.f - u)), util::WithA(Rgb(0xFF9A40), 0.f));
		}
		const float dawn = util::Clamp01((t - 3.8f) / 2.f) * (1.f - util::Clamp01((t - 13.f) / 3.f));
		if (dawn > 0.f)
		{
			DrawRectangleGradientV(0, static_cast<int>(H * 0.35f), static_cast<int>(W), static_cast<int>(H * 0.65f + 2.f), util::WithA(Rgb(0xFFC870), 0.f), util::WithA(Rgb(0xFFB060), 0.35f * dawn));
			for (int k = 0; k < 9; ++k)
			{
				const float ang = -util::kPi * 0.5f + (k - 4) * 0.2f + std::sin(t * 0.3f + k) * 0.03f;
				const Vector2 o{ W * 0.5f, H * 1.05f };
				const Vector2 e{ o.x + std::cos(ang) * H * 1.2f, o.y + std::sin(ang) * H * 1.2f };
				DrawLineEx(o, e, 40.f + (k % 3) * 20.f, util::WithA(Rgb(0xFFE0A0), 0.05f * dawn));
			}
		}
		const float cy = H * 0.4f;
		if (t < 3.6f)
		{
			if (t > 0.2f) centered("쿵.", W * 0.5f - 60.f, cy, 34.f, util::WithA(Rgb(0xF2DEB8), util::Clamp01((t - 0.2f) / 0.3f) * util::Clamp01((3.6f - t) / 0.6f)), 0.f, ui::Face::Serif);
			if (t > 1.45f) centered("…쿵.", W * 0.5f + 50.f, cy + 30.f, 34.f, util::WithA(Rgb(0xF2DEB8), util::Clamp01((t - 1.45f) / 0.3f) * util::Clamp01((3.6f - t) / 0.6f)), 0.f, ui::Face::Serif);
		}
		else if (t < 10.4f)
		{
			const float a = util::Clamp01((t - 4.f) / 1.f) * util::Clamp01((10.4f - t) / 1.f);
			centered("하르벤이 다시 뛴다", W * 0.5f, cy - 10.f, 42.f, util::WithA(Color{ 250, 240, 216, 255 }, a), 0.f, ui::Face::Display, 4.f);
			centered("속삭임은 노래가 되고, 긴 밤 끝에 — 새벽이 왔다.", W * 0.5f, cy + 40.f, 19.f, util::WithA(Rgb(0xF6E6C2), a * util::Clamp01((t - 5.2f) / 1.f)), 0.f, ui::Face::Serif);
		}
		else
		{
			const float a = util::Clamp01((t - 10.6f) / 1.f) * util::Clamp01((16.f - t) / 1.4f);
			ui::Logo(Vector2{ W * 0.5f, cy - 20.f }, 0.8f, a, false);
			centered("멈춘 심장 · 끝", W * 0.5f, cy + 58.f, 18.f, util::WithA(Rgb(0xF2DEB8), a), 0.f, ui::Face::Serif, 3.f);
			centered("그리고 모험은 계속됩니다", W * 0.5f, cy + 86.f, 14.f, util::WithA(ui::theme::kText, a), 0.f, ui::Face::Body);
		}
	}
}
