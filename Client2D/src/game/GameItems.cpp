#include "game/Game.h"
#include "ui/Wm.h"
#include "game/Texts.h"
#include "render/Icons.h"
#include "render/Actors.h"
#include "render/Draw.h"
#include "fx/Effects.h"
#include "audio/Sfx.h"
#include "ui/Font.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <utility>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 아이템 성장 UI(V2) — 장비창(부위 8칸 + 캐릭터 미리보기) · 소지품(희귀도 테두리 ·
// 검색 · 정리 · ▲갈아입기 표시) · 툴팁(옵션 · 특수 효과 · 세트 · 비교) · 획득 연출 ·
// 다른 플레이어의 착용 외형.
//
// 능력치 비교는 서버와 같은 함수(ItemRules::Accumulate + StatRule::Compute)로 계산하므로
// 툴팁의 ▲/▼ 숫자가 입었을 때 서버가 보내 줄 숫자와 같다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	using util::Rgb;
	using ui::Align;
	using ui::ButtonKind;

	std::string Fmt(const char* f, ...)
	{
		char buf[512];
		va_list ap;
		va_start(ap, f);
		std::vsnprintf(buf, sizeof(buf), f, ap);
		va_end(ap);
		return buf;
	}

	// 그로시(은화)
	void Coin(Vector2 c, float r)
	{
		DrawCircleV(Vector2{ c.x + 0.5f, c.y + 1.2f }, r, Color{ 0, 0, 0, 140 });
		DrawCircleV(c, r, Color{ 128, 132, 138, 255 });
		DrawCircleV(c, r - 1.2f, Color{ 196, 199, 202, 255 });
		DrawCircleV(Vector2{ c.x - r * 0.18f, c.y - r * 0.2f }, r * 0.5f, Color{ 232, 232, 226, 255 });
	}

	// 장비창 칸 배치: 왼쪽 위→아래(투구 · 갑옷 · 장갑 · 신발), 오른쪽(무기 · 망토 · 목걸이 · 반지)
	constexpr EquipSlot kLeftCol[4] = { EquipSlot::Helmet, EquipSlot::Armor, EquipSlot::Gloves, EquipSlot::Boots };
	constexpr EquipSlot kRightCol[4] = { EquipSlot::Weapon, EquipSlot::Cloak, EquipSlot::Necklace, EquipSlot::Ring };

	bool Contains(const std::string& hay, const std::string& needle)
	{
		return !needle.empty() && hay.find(needle) != std::string::npos;
	}

	// 검색어가 이름·희귀도·부위·옵션·특수 효과·세트 중 하나에 들어 있나
	bool MatchesSearch(const InvSlot& s, const std::string& q)
	{
		if (q.empty()) return true;
		const ItemTableRow* row = ItemTableData::Find(s.itemId);
		if (!row) return false;
		if (Contains(row->displayName, q)) return true;
		if (row->type != ItemType::eEquipment) return Contains("소모품", q) || (Contains("물약", q) && row->effect == ItemEffect::eHeal);
		const ItemInstance inst = ItemInstance::Unpack(s.extra);
		const Rarity r = ItemRules::EffectiveRarity(*row, inst);
		if (Contains(ItemRules::RarityName(r), q)) return true;
		const EquipSlot part = ItemTableData::SlotOf(*row);
		if (Contains(texts::SlotName(part), q) || (part == EquipSlot::Weapon && Contains(texts::WeaponKindName(row->weaponKind), q))) return true;
		for (const auto& o : inst.opts)
			if (o.kind != ItemOption::None && Contains(texts::OptionText(o), q)) return true;
		if (inst.proc != ItemProc::None && Contains(ItemRules::Proc(inst.proc).name, q)) return true;
		if (const auto* set = ItemRules::FindSet(row->setId); set && Contains(set->name, q)) return true;
		return false;
	}

	std::string ClassMaskText(uint8_t mask)
	{
		std::string out;
		for (int c = 0; c < static_cast<int>(ClassId::Count); ++c)
		{
			if (!(mask & (1u << c))) continue;
			if (!out.empty()) out += "·";
			out += ClassTable::Get(static_cast<ClassId>(c)).name;
		}
		return out;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 계산
// ─────────────────────────────────────────────────────────────────────────────
Rarity Game::SlotRarity(const InvSlot& s) const
{
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	if (!row || row->type != ItemType::eEquipment || s.Empty()) return Rarity::Common;
	return ItemRules::EffectiveRarity(*row, s.extra);
}

bool Game::CanWear(const ItemTableRow& row, std::string* why) const
{
	if (row.type != ItemType::eEquipment)
		return false;
	if (!ItemTableData::ClassCanUse(row, static_cast<uint8_t>(m_classId)))
	{
		if (why) *why = ClassMaskText(row.classMask) + " 전용입니다";
		return false;
	}
	if (row.reqLevel > m_level)
	{
		if (why) *why = Fmt("Lv.%d부터 착용할 수 있습니다", row.reqLevel);
		return false;
	}
	return true;
}

DerivedStats Game::StatsIf(int invSlot, bool unequip) const
{
	EquippedPiece pieces[static_cast<size_t>(EquipSlot::Count) + 1];
	int n = 0;
	const InvSlot* wear = nullptr;
	EquipSlot replacePart = EquipSlot::None;
	if (invSlot >= 0 && invSlot < MAX_INVENTORY_SLOTS && !m_inv.slots[invSlot].Empty())
	{
		wear = &m_inv.slots[invSlot];
		replacePart = ItemTableData::SlotOf(wear->itemId);
	}
	for (int i = 0; i < MAX_INVENTORY_SLOTS && n < static_cast<int>(EquipSlot::Count); ++i)
	{
		const InvSlot& s = m_inv.slots[i];
		if (s.Empty() || !s.equipped) continue;
		if (unequip && i == invSlot) continue;
		if (!unequip && wear && !wear->equipped && ItemTableData::SlotOf(s.itemId) == replacePart) continue;   // 같은 부위는 벗는다
		pieces[n++] = EquippedPiece{ ItemTableData::Find(s.itemId), s.extra, s.enchant };
	}
	if (!unequip && wear && !wear->equipped && replacePart != EquipSlot::None)
		pieces[n++] = EquippedPiece{ ItemTableData::Find(wear->itemId), wear->extra, wear->enchant };
	GearBonus g{};
	GearExtras ex{};
	ItemRules::Accumulate(pieces, n, g, ex);
	BaseStats total{};
	for (int i = 0; i < 6; ++i)
		total.v[i] = static_cast<int16_t>(m_baseStats.v[i] + m_allocStats.v[i]);
	return StatRule::Compute(m_classId, static_cast<uint8_t>(m_level), total, g);
}

int Game::UpgradeScore(int invSlot) const
{
	if (invSlot < 0 || invSlot >= MAX_INVENTORY_SLOTS || !m_statsKnown) return 0;
	const InvSlot& s = m_inv.slots[invSlot];
	if (s.Empty() || s.equipped) return 0;
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	if (!row || !CanWear(*row)) return 0;
	const DerivedStats a = StatsIf(-1), b = StatsIf(invSlot);
	const bool caster = m_classId == ClassId::Engraver;
	int score = 0;
	score += ((caster ? b.magicAttack : b.attack) - (caster ? a.magicAttack : a.attack)) * 10;
	score += ((caster ? b.attack : b.magicAttack) - (caster ? a.attack : a.magicAttack)) * 1;
	score += (b.defense - a.defense) * 5 + (b.magicDefense - a.magicDefense) * 3;
	score += (b.maxHp - a.maxHp) + (b.maxMp - a.maxMp) / 2;
	score += (b.critChance - a.critChance) / 2 + (b.critDamage - a.critDamage) * 2;
	score += (b.attackSpeed - a.attackSpeed) * 4 + (b.moveSpeed - a.moveSpeed) * 3 + (b.cooldownReduction - a.cooldownReduction) * 6;
	score += (b.penetration - a.penetration) * 3 + (b.statusResist - a.statusResist) * 2;
	score += (b.hpRegen - a.hpRegen) * 2 + (b.mpRegen - a.mpRegen) * 2 + (b.evasion - a.evasion) * 2;
	// 특수 효과가 있는 장비는 덤으로(숫자에 다 드러나지 않는다)
	if (ItemInstance::Unpack(s.extra).proc != ItemProc::None) score += 25;
	return score;
}

GearExtras Game::MyGearExtras() const
{
	EquippedPiece pieces[static_cast<size_t>(EquipSlot::Count)];
	int n = 0;
	for (const auto& s : m_inv.slots)
		if (!s.Empty() && s.equipped && n < static_cast<int>(EquipSlot::Count))
			pieces[n++] = EquippedPiece{ ItemTableData::Find(s.itemId), s.extra, s.enchant };
	GearBonus g{};
	GearExtras ex{};
	ItemRules::Accumulate(pieces, n, g, ex);
	return ex;
}

actors::PlayerLook Game::LookOf(const Entity& e) const
{
	actors::PlayerLook look = actors::LookForPlayer(e.nameHash, e.itemId, e.enchant, e.classId);
	uint16_t items[8]{};
	uint8_t rarity[8]{};
	if (e.id == m_selfId)
	{
		for (int part = 1; part <= 8; ++part)
		{
			const int i = m_inv.EquippedIn(static_cast<EquipSlot>(part));
			if (i < 0) continue;
			items[part - 1] = m_inv.slots[i].itemId;
			rarity[part - 1] = static_cast<uint8_t>(SlotRarity(m_inv.slots[i]));
		}
	}
	else if (e.gearKnown)
	{
		for (int i = 0; i < 8; ++i) { items[i] = e.gear[i]; rarity[i] = e.gearRarity[i]; }
	}
	else
	{
		items[0] = e.itemId;
		rarity[0] = e.weaponGrade;
	}
	actors::ApplyGear(look, items, rarity, 8);
	return look;
}

// ─────────────────────────────────────────────────────────────────────────────
// 네트워크
// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleItemDetail(const ITEM_DETAIL_INF_PACKET& p)
{
	if (p.slotIndex >= MAX_INVENTORY_SLOTS) return;
	InvSlot& s = m_inv.slots[p.slotIndex];
	if (s.itemId != p.itemId) return;   // 순서가 어긋났다면 다음 슬롯 알림이 바로잡는다
	s.extra = p.extra;
}

void Game::HandlePlayerLook(const PLAYER_LOOK_NFY_PACKET& p)
{
	Entity* e = Find(p.id);
	if (!e) return;
	for (int i = 0; i < kLookSlots && i < 8; ++i)
	{
		e->gear[i] = p.items[i];
		e->gearRarity[i] = p.rarity[i];
	}
	e->gearKnown = true;
	e->itemId = p.items[0];
	e->enchant = p.enchant;
	e->weaponGrade = p.rarity[0];
}

void Game::TickLoot()
{
	// 슬롯 알림 묶음이 가라앉으면(교환은 두 칸, 정리는 수십 칸이 연달아 온다) 처음 상태와 비교해
	// 새로 생긴 것만 알린다: 소모품은 종류별 총 개수, 장비는 (종류, 인스턴스 값) 묶음이 늘었을 때.
	if (m_invBaselineAt < 0.0 || m_now - m_invBaselineAt < 0.25)
		return;
	m_invBaselineAt = -1.0;

	std::unordered_map<uint16_t, int> countBefore, countNow;
	std::vector<std::pair<uint16_t, uint64_t>> gearBefore;
	for (const auto& s : m_invBaseline)
	{
		if (s.Empty()) continue;
		if (ItemTableData::SlotOf(s.itemId) != EquipSlot::None) gearBefore.emplace_back(s.itemId, s.extra);
		else countBefore[s.itemId] += s.count;
	}
	std::vector<int> newGear;
	for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
	{
		const InvSlot& s = m_inv.slots[i];
		if (s.Empty()) continue;
		if (ItemTableData::SlotOf(s.itemId) == EquipSlot::None) { countNow[s.itemId] += s.count; continue; }
		const auto it = std::find(gearBefore.begin(), gearBefore.end(), std::make_pair(s.itemId, s.extra));
		if (it != gearBefore.end()) gearBefore.erase(it);
		else newGear.push_back(i);
	}
	for (const auto& [id, n] : countNow)
	{
		const int gained = n - countBefore[id];
		if (gained > 0)
			AddToast(id, texts::ItemName(id) + (gained > 1 ? " ×" + std::to_string(gained) : ""), icons::ItemNameColor(id));
	}
	int bestUpgrade = -1, bestScore = 8;
	for (int slot : newGear)
	{
		AnnounceLoot(slot, 1);
		const int sc = UpgradeScore(slot);
		if (sc > bestScore) { bestScore = sc; bestUpgrade = slot; }
	}
	// 더 좋은 장비면 한 줄로 알려 준다(가방을 열 이유)
	if (bestUpgrade >= 0)
	{
		const InvSlot& s = m_inv.slots[bestUpgrade];
		AddChat(texts::ItemNameFull(s.itemId, s.enchant, s.extra) + " — 지금 장비보다 좋습니다. (I 소지품에서 ▲)", Rgb(0x8AE88A));
	}
}

void Game::AnnounceLoot(int slot, int gained)
{
	if (slot < 0 || slot >= MAX_INVENTORY_SLOTS) return;
	InvSlot& s = m_inv.slots[slot];
	if (s.Empty()) return;
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	if (!row) return;
	const Rarity r = SlotRarity(s);
	const Color rc = icons::ItemNameColor(s.itemId, s.extra);
	AddToast(s.itemId, texts::ItemNameFull(s.itemId, s.enchant, s.extra) + (gained > 1 ? " ×" + std::to_string(gained) : ""), rc);
	if (row->type != ItemType::eEquipment)
		return;
	s.newAt = static_cast<float>(m_now);

	Entity* me = Self();
	const Vector2 iso = me ? TileToIso(me->pos) : Vector2{ 0, 0 };
	switch (r)
	{
	case Rarity::Rare:
		sfx::Play(sfx::Id::LootRare, 0.55f);
		break;
	case Rarity::Heroic:
		sfx::Play(sfx::Id::LootHeroic, 0.65f);
		AddCenter(std::string("영웅 등급 — ") + row->displayName, rc);
		if (me) fx::Fx().LootSparkle(iso, rc);
		break;
	case Rarity::Legendary:
		sfx::Play(sfx::Id::LootLegendary, 0.8f);
		AddBanner(std::string("◆ 전설 ◆  ") + row->displayName, rc, 4.5f);
		if (me)
		{
			fx::Fx().LootSparkle(iso, rc);
			fx::Fx().Ring(iso, 6.f, 90.f, 0.8f, rc, 4.f);
			fx::Fx().Beam(iso, 34.f, 220.f, 1.2f, util::MulA(rc, 0.8f));
		}
		m_cam.AddTrauma(0.18f);
		break;
	case Rarity::Mythic:
		sfx::Play(sfx::Id::LootMythic, 0.9f);
		AddBanner(std::string("◆ 신화 ◆  ") + row->displayName, rc, 6.f);
		if (me)
		{
			fx::Fx().LootSparkle(iso, rc);
			fx::Fx().Ring(iso, 6.f, 130.f, 1.1f, rc, 5.f);
			fx::Fx().Ring(iso, 6.f, 80.f, 0.8f, Rgb(0xFFE8A0), 3.f);
			fx::Fx().Beam(iso, 46.f, 300.f, 1.6f, util::MulA(rc, 0.9f));
			fx::Fx().Flash(rc, 0.25f, 0.35f);
		}
		m_cam.AddTrauma(0.3f);
		break;
	default:
		break;
	}
}

void Game::TryEquipFromUi(int invSlot, EquipSlot target)
{
	if (invSlot < 0 || invSlot >= MAX_INVENTORY_SLOTS) return;
	const InvSlot& s = m_inv.slots[invSlot];
	if (s.Empty()) return;
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	if (!row || row->type != ItemType::eEquipment) { sfx::Play(sfx::Id::Error, 0.5f); return; }
	const EquipSlot part = ItemTableData::SlotOf(*row);
	if (target != EquipSlot::None && part != target)
	{
		AddCenter(std::string(texts::SlotName(part)) + " — 그 칸에는 입을 수 없습니다.", Rgb(0xFF8A7A));
		sfx::Play(sfx::Id::Error, 0.5f);
		return;
	}
	std::string why;
	if (!CanWear(*row, &why))
	{
		AddCenter(why, Rgb(0xFF8A7A));
		sfx::Play(sfx::Id::Error, 0.5f);
		return;
	}
	if (!s.equipped)
		m_net.SendEquip(static_cast<uint16_t>(invSlot));
}

// ─────────────────────────────────────────────────────────────────────────────
// 툴팁
// ─────────────────────────────────────────────────────────────────────────────
void Game::BuildItemTip(std::vector<ui::TooltipLine>& out, const InvSlot& s, TipMode mode, int invSlot) const
{
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	if (!row) return;
	const Color dim = ui::theme::kTextDim;
	const Color hint{ 150, 140, 120, 255 };
	const bool equip = row->type == ItemType::eEquipment;
	const ItemInstance inst = ItemInstance::Unpack(s.extra);
	const Rarity r = equip ? ItemRules::EffectiveRarity(*row, inst) : Rarity::Common;
	const Color rc = icons::ItemNameColor(s.itemId, s.extra);

	// 이름: 전설 이상은 고운바탕(Display) — 이름 그 자체가 이야기인 물건
	out.push_back({ texts::ItemNameFull(s.itemId, s.enchant, s.extra), rc, r >= Rarity::Legendary ? 18.f : 16.5f, r >= Rarity::Legendary ? ui::Face::Display : ui::Face::Strong });
	if (equip)
	{
		const EquipSlot part = ItemTableData::SlotOf(*row);
		std::string sub = std::string(ItemRules::RarityName(r)) + " " + (part == EquipSlot::Weapon ? texts::WeaponKindName(row->weaponKind) : texts::SlotName(part));
		if (row->classMask != 0) sub += " · " + ClassMaskText(row->classMask) + " 전용";
		out.push_back({ sub, util::Mix(rc, dim, 0.35f), 12.5f });
		out.push_back({ "---", dim });

		const int pct = ItemRules::BasePercent(*row, r);
		const std::string bonusNote = pct > 100 ? Fmt("  (희귀도 +%d%%)", pct - 100) : std::string();
		if (part == EquipSlot::Weapon)
		{
			const int atk = row->attackBonus * pct / 100;
			out.push_back({ Fmt(row->weaponKind == WeaponKind::Staff ? "마법 공격력 +%d" : "공격력 +%d", atk) + bonusNote, ui::theme::kTextHi, 15.f, ui::Face::Strong });
			if (s.enchant > 0) out.push_back({ Fmt("각인 +%d", EnchantRule::AttackBonus(s.enchant)), ui::theme::kOldway, 14.f });
		}
		if (row->armorBonus > 0)
			out.push_back({ Fmt("방어력 +%d", row->armorBonus * pct / 100) + (part != EquipSlot::Weapon ? bonusNote : std::string()), ui::theme::kTextHi, 15.f, ui::Face::Strong });
		if (row->magicArmorBonus > 0)
			out.push_back({ Fmt("마법 방어력 +%d", row->magicArmorBonus * pct / 100), ui::theme::kTextHi, 15.f, ui::Face::Strong });
		if (row->armorBonus == 0 && row->magicArmorBonus == 0 && part != EquipSlot::Weapon && inst.OptionCount() == 0)
			out.push_back({ "기본 성능 없음 — 옵션이 붙어야 빛난다", dim, 13.f });

		if (inst.OptionCount() > 0)
		{
			out.push_back({ "---", dim });
			for (const auto& o : inst.opts)
			{
				if (o.kind == ItemOption::None) continue;
				const bool skill = o.kind == ItemOption::SkillBoost;
				out.push_back({ std::string(skill ? "◈ " : "◇ ") + texts::OptionText(o), skill ? ui::theme::kDawn : Rgb(0x9DBBD8), 14.f });
			}
		}
		if (inst.proc != ItemProc::None)
		{
			const int power = inst.empowered ? 2 : 1;
			out.push_back({ "---", dim });
			out.push_back({ std::string("◆ ") + ItemRules::Proc(inst.proc).name + (inst.empowered ? "  (신화 강화)" : ""), util::Mix(Rgb(ItemRules::Proc(inst.proc).color), ui::theme::kTextHi, 0.25f), 14.5f, ui::Face::Strong });
			out.push_back({ "   " + texts::ProcText(inst.proc, power), ui::theme::kText, 13.f });
		}
		if (const auto* set = ItemRules::FindSet(row->setId))
		{
			int owned = 0;
			for (const auto& sl : m_inv.slots)
				if (!sl.Empty() && sl.equipped)
					if (const ItemTableRow* rr = ItemTableData::Find(sl.itemId); rr && rr->setId == row->setId) ++owned;
			const Color sc = Rgb(set->color);
			out.push_back({ "---", dim });
			out.push_back({ Fmt("%s  (%d/4)", set->name, owned), sc, 14.5f, ui::Face::Strong });
			std::string pieces = "   ";
			for (ItemTableId pid : set->pieces)
			{
				const ItemTableRow* pr = ItemTableData::Find(pid);
				if (!pr) continue;
				if (pieces.size() > 3) pieces += " · ";
				pieces += texts::SlotName(ItemTableData::SlotOf(*pr));
			}
			out.push_back({ pieces, dim, 12.f });
			out.push_back({ "   (2) " + texts::OptionText(set->two[0]) + " · " + texts::OptionText(set->two[1]), owned >= 2 ? util::Mix(sc, ui::theme::kTextHi, 0.3f) : dim, 13.f });
			out.push_back({ "   (4) " + texts::OptionText(set->four[0]) + " · " + texts::OptionText(set->four[1]) + " · " + ItemRules::Proc(set->fourProc).name,
				owned >= 4 ? util::Mix(sc, ui::theme::kTextHi, 0.3f) : dim, 13.f });
		}

		std::string why;
		if (!CanWear(*row, &why))
		{
			out.push_back({ "---", dim });
			out.push_back({ why, ui::theme::kBad, 14.f });
		}
		else if (row->reqLevel > 0)
			out.push_back({ Fmt("착용 레벨 %d", row->reqLevel), dim, 13.f });

		// 비교: 입으면 능력치가 어떻게 바뀌나(서버와 같은 공식)
		if (mode == TipMode::Inventory && !s.equipped && invSlot >= 0 && m_statsKnown && CanWear(*row))
		{
			const DerivedStats a = StatsIf(-1), b = StatsIf(invSlot);
			std::vector<ui::TooltipLine> deltas;
			auto delta = [&](const char* name, int va, int vb, int kind)
			{
				const int d = vb - va;
				if (d == 0) return;
				std::string v;
				if (kind == 2) v = Fmt("%c%d.%d%%", d < 0 ? '-' : '+', std::abs(d) / 10, std::abs(d) % 10);
				else if (kind == 1) v = Fmt("%+d%%", d);
				else v = Fmt("%+d", d);
				deltas.push_back({ std::string(d > 0 ? "▲ " : "▼ ") + name + " " + v, d > 0 ? ui::theme::kGood : ui::theme::kBad, 13.5f });
			};
			const bool caster = m_classId == ClassId::Engraver;
			if (caster) { delta("마법 공격력", a.magicAttack, b.magicAttack, 0); delta("공격력", a.attack, b.attack, 0); }
			else { delta("공격력", a.attack, b.attack, 0); delta("마법 공격력", a.magicAttack, b.magicAttack, 0); }
			delta("방어력", a.defense, b.defense, 0);
			delta("마법 방어력", a.magicDefense, b.magicDefense, 0);
			delta("최대 HP", a.maxHp, b.maxHp, 0);
			delta("최대 MP", a.maxMp, b.maxMp, 0);
			delta("치명타 확률", a.critChance, b.critChance, 2);
			delta("치명타 피해", a.critDamage, b.critDamage, 1);
			delta("공격 속도", a.attackSpeed, b.attackSpeed, 1);
			delta("이동 속도", a.moveSpeed, b.moveSpeed, 1);
			delta("재사용 대기시간 감소", a.cooldownReduction, b.cooldownReduction, 1);
			delta("관통", a.penetration, b.penetration, 0);
			delta("회피", a.evasion, b.evasion, 0);
			delta("상태이상 저항", a.statusResist, b.statusResist, 1);
			delta("HP 회복", a.hpRegen, b.hpRegen, 0);
			delta("MP 회복", a.mpRegen, b.mpRegen, 0);
			out.push_back({ "---", dim });
			if (deltas.empty())
				out.push_back({ "입어도 능력치는 그대로입니다", dim, 13.f });
			else
			{
				out.push_back({ "입으면", dim, 12.f });
				for (auto& d : deltas) out.push_back(std::move(d));
			}
		}
	}
	else
	{
		switch (row->type)
		{
		case ItemType::eConsumable:
			out.push_back({ "소모품", dim, 14.f });
			switch (row->effect)
			{
			case ItemEffect::eHeal: out.push_back({ Fmt("HP %d 회복 (재사용 5초)", row->healAmount), ui::theme::kGood, 14.f }); break;
			case ItemEffect::eReturnTown: out.push_back({ std::string(WorldData::kTownName) + "(으)로 귀환", ui::theme::kTextHi, 14.f }); break;
			case ItemEffect::eTeleportRandom: out.push_back({ "옛길을 밟아 지금 지역의 아무 곳으로 이동", ui::theme::kOldway, 14.f }); break;
			case ItemEffect::eHaste: out.push_back({ Fmt("%d초 동안 이동 속도 증가", row->effectValue), ui::theme::kGood, 14.f }); break;
			case ItemEffect::eEnchantWeapon:
				out.push_back({ row->effectValue ? "무기 각인 강화 (축성: 더 크게 오를 수 있음)" : "무기 각인 강화 (+6까지 안전)", ui::theme::kDawn, 14.f });
				break;
			default: break;
			}
			break;
		case ItemType::eCurrency:
			out.push_back({ "화폐", dim, 14.f });
			break;
		default:
			break;
		}
	}

	if (s.count > 1) out.push_back({ Fmt("수량 %d", s.count), ui::theme::kText, 13.f });
	if (s.equipped && mode != TipMode::Equipped) out.push_back({ "착용 중", ui::theme::kDawn, 13.f });
	if (row->sellPrice > 0 && mode != TipMode::Shop)
	{
		const uint32_t unit = ItemRules::SellPrice(*row, s.extra, s.enchant);
		out.push_back({ Fmt("상점 매입가 %s 그로시", texts::Number(unit).c_str()), mode == TipMode::Sell ? ui::theme::kTextHi : dim, 12.5f });
	}
	if (mode == TipMode::Inventory)
	{
		if (equip && !s.equipped) out.push_back({ "더블클릭 · 우클릭: 착용   끌어서 장비칸에", hint, 12.f });
		else if (equip) out.push_back({ "더블클릭 · 우클릭: 벗기", hint, 12.f });
		else if (row->type == ItemType::eConsumable) out.push_back({ "더블클릭 · 우클릭: 사용   퀵슬롯으로 끌기", hint, 12.f });
	}
	else if (mode == TipMode::Equipped && invSlot >= 0)
		out.push_back({ "더블클릭 · 우클릭: 벗기   끌어서 가방으로", hint, 12.f });
}

void Game::DrawItemTooltip(const InvSlot& s, TipMode mode, int invSlot)
{
	std::vector<ui::TooltipLine> lines;
	BuildItemTip(lines, s, mode, invSlot);
	if (lines.empty()) return;
	const ItemTableRow* row = ItemTableData::Find(s.itemId);
	const bool equip = row && row->type == ItemType::eEquipment;
	const Rarity r = equip ? SlotRarity(s) : Rarity::Common;
	const Color accent = equip && r != Rarity::Common ? icons::RarityColor(r) : Color{ 0, 0, 0, 0 };
	ui::Tooltip(std::move(lines), s.itemId, accent);

	// 비교 툴팁: 같은 부위에 입고 있는 장비
	if (equip && mode == TipMode::Inventory && !s.equipped)
	{
		const int cur = m_inv.EquippedIn(ItemTableData::SlotOf(*row));
		if (cur >= 0 && cur != invSlot)
		{
			std::vector<ui::TooltipLine> cmp;
			BuildItemTip(cmp, m_inv.slots[cur], TipMode::Equipped, -1);
			const Rarity cr = SlotRarity(m_inv.slots[cur]);
			ui::TooltipCompare(std::move(cmp), m_inv.slots[cur].itemId, cr != Rarity::Common ? icons::RarityColor(cr) : Color{ 0, 0, 0, 0 }, "착용 중");
		}
	}
}

void Game::DrawItemTooltip(uint16_t itemId, uint8_t enchant, int count, bool equipped, bool forSell)
{
	InvSlot s;
	s.itemId = itemId;
	s.enchant = enchant;
	s.count = static_cast<uint16_t>(std::max(1, count));
	s.equipped = equipped;
	DrawItemTooltip(s, forSell ? TipMode::Sell : TipMode::Shop, -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// 장비창(부위 8칸 + 캐릭터)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawEquipPanel(Rectangle area)
{
	const float slot = 48.f, gap = 8.f;
	ui::Plate(area, Color{ 11, 13, 16, 200 }, ui::theme::kLine, 3.f, 1.f);

	// 캐릭터 미리보기(지금 입은 그대로 — 투구·갑옷·망토·무기 빛까지)
	const float colH = 4 * slot + 3 * gap;
	const Rectangle pv{ area.x + slot + 14.f, area.y + 6.f, area.width - 2 * slot - 28.f, colH + 4.f };
	{
		const float cx = pv.x + pv.width * 0.5f, feet = pv.y + pv.height - 16.f;
		DrawEllipse(static_cast<int>(cx), static_cast<int>(feet), 40, 12, Color{ 0, 0, 0, 90 });
		const Color cc = Rgb(ClassTable::Get(m_classId).color);
		DrawCircleGradient(static_cast<int>(cx), static_cast<int>(feet - 70), 70.f, util::MulA(cc, 0.12f), util::MulA(cc, 0.f));
		Entity* me = Self();
		if (me)
		{
			ui::BeginClip(Rectangle{ pv.x, pv.y, pv.width, pv.height });
			actors::Pose pose;
			pose.base = Vector2{ cx, feet };
			pose.scale = 1.75f;
			pose.time = m_time;
			pose.faceRight = true;
			pose.seed = me->nameHash;
			gfx::BeginWorld(0.f, 1.75f);
			gfx::ResetMod();
			gfx::SetDepthRaw(-0.5f);
			actors::DrawPlayer(pose, LookOf(*me));
			gfx::Flush();
			ui::EndClip();
		}
	}

	m_equipHover = -1;
	const bool dragItem = ui::Dragging() && (ui::Drag().kind == 1);
	const EquipSlot dragPart = dragItem ? ItemTableData::SlotOf(ui::Drag().itemId) : EquipSlot::None;
	auto drawSlot = [&](EquipSlot part, Rectangle r)
	{
		const int inv = m_inv.EquippedIn(part);
		const bool hover = ui::Hovered(r);
		if (hover) m_equipHover = static_cast<int>(part);
		const InvSlot* s = inv >= 0 ? &m_inv.slots[inv] : nullptr;
		const Rarity rar = s ? SlotRarity(*s) : Rarity::Common;
		Color accent = s ? (rar != Rarity::Common ? icons::RarityColor(rar) : ui::theme::kBorder) : ui::theme::kBorder;
		const bool target = dragItem && dragPart == part;
		if (target)
		{
			const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 8.f);
			accent = util::Mix(ui::theme::kDawn, WHITE, pulse * 0.3f);
		}
		ui::SlotFrame(r, hover || target, false, accent);
		if (s)
		{
			if (rar >= Rarity::Legendary)
			{
				const float pulse = 0.7f + 0.3f * std::sin(static_cast<float>(m_now) * 2.5f);
				DrawCircleGradient(static_cast<int>(r.x + r.width * 0.5f), static_cast<int>(r.y + r.height * 0.5f), r.width * 0.5f, util::MulA(icons::RarityColor(rar), 0.22f * pulse), util::MulA(icons::RarityColor(rar), 0.f));
			}
			const bool dragged = ui::Dragging() && ui::Drag().kind == 3 && ui::Drag().index == inv;
			icons::Draw(s->itemId, Rectangle{ r.x + 4, r.y + 4, r.width - 8, r.height - 8 }, dragged ? Color{ 255, 255, 255, 80 } : WHITE);
			if (s->enchant > 0)
			{
				ui::TextStyle es; es.size = 13.f; es.color = s->enchant >= 7 ? ui::theme::kDawnHi : ui::theme::kOldway; es.outline = 1.2f; es.face = ui::Face::Number;
				ui::Fonts().Draw("+" + std::to_string(s->enchant), Vector2{ r.x + 4, r.y + 2 }, es);
			}
		}
		else
		{
			ui::Text(texts::SlotName(part), Vector2{ r.x + r.width * 0.5f, r.y + r.height * 0.5f - 7.f }, 12.f, ui::theme::kTextMute, Align::Center);
		}

		// ── 입력 ──
		if (hover && s && !ui::Dragging())
			DrawItemTooltip(*s, TipMode::Equipped, inv);
		if (ui::DragReleased() && hover && ui::Drag().kind == 1)
		{
			TryEquipFromUi(ui::Drag().index, part);
			ui::EndDrag();
			return;
		}
		if (!s) return;
		if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) && ui::Pressed(r))
		{
			ChatInsertItemLink(s->itemId, s->enchant, static_cast<uint8_t>(SlotRarity(*s)));
			ui::ConsumeMouse();
			return;
		}
		if (ui::DoubleClicked(r) || ui::Pressed(r, MOUSE_BUTTON_RIGHT))
		{
			m_net.SendUnequip(static_cast<uint16_t>(inv));
			ui::ConsumeMouse();
			m_pressKind = 0;
			return;
		}
		if (ui::Pressed(r))
		{
			m_pressKind = 3; m_pressIndex = inv; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == 3 && m_pressIndex == inv && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ 3, inv, s->itemId });
			m_pressKind = 0;
		}
	};
	for (int k = 0; k < 4; ++k)
	{
		const Rectangle lr{ area.x + 8.f, area.y + 8.f + k * (slot + gap), slot, slot };
		const Rectangle rr{ area.x + area.width - 8.f - slot, area.y + 8.f + k * (slot + gap), slot, slot };
		m_equipRects[static_cast<size_t>(kLeftCol[k])] = lr;
		m_equipRects[static_cast<size_t>(kRightCol[k])] = rr;
		drawSlot(kLeftCol[k], lr);
		drawSlot(kRightCol[k], rr);
	}

	// 요약(공격 · 방어 · 세트)
	float y = area.y + 12.f + colH;
	const bool caster = m_classId == ClassId::Engraver;
	ui::Text(Fmt("%s %d   방어 %d", caster ? "마공" : "공격", caster ? m_stats.magicAttack : m_stats.attack, m_stats.defense),
		Vector2{ area.x + area.width * 0.5f, y }, 13.5f, ui::theme::kTextHi, Align::Center);
	y += 18.f;
	ui::Text(Fmt("HP %d  ·  마방 %d  ·  이속 %d%%", m_stats.maxHp, m_stats.magicDefense, m_stats.moveSpeed),
		Vector2{ area.x + area.width * 0.5f, y }, 12.f, ui::theme::kTextDim, Align::Center);
	y += 18.f;
	for (const auto& set : ItemRules::kSets)
	{
		int owned = 0;
		for (const auto& sl : m_inv.slots)
			if (!sl.Empty() && sl.equipped)
				if (const ItemTableRow* rr = ItemTableData::Find(sl.itemId); rr && rr->setId == set.id) ++owned;
		if (owned < 2) continue;
		ui::Text(Fmt("%s %d/4%s", set.name, owned, owned >= 4 ? " · 완성" : ""), Vector2{ area.x + area.width * 0.5f, y }, 12.f, Rgb(set.color), Align::Center);
		y += 16.f;
	}

	// 장비칸에서 끌어다 캐릭터(가운데)에 놓으면 아무 일도 없다 — 창 밖은 버리기, 가방은 벗기
}

// ─────────────────────────────────────────────────────────────────────────────
// 소지품(장비창 + 가방 30칸 + 검색 · 정리)
// ─────────────────────────────────────────────────────────────────────────────
void Game::DrawInventory()
{
	const float W = m_uiW, H = m_uiH;
	const float slot = 54.f, gap = 4.f;
	const float equipW = 214.f;
	const float gridW = 6 * (slot + gap) - gap;
	const float w = equipW + gridW + 36.f, h = 5 * (slot + gap) + 134.f;
	Rectangle r{ W - w - 14.f, 200.f, w, h };
	if (r.y + r.height > H - 110.f) r.y = std::max(12.f, H - 110.f - r.height);
	if (r.x < 8.f) r.x = 8.f;
	r = wm::Window("inv", r, wm::kMove);
	m_invRect = r;
	ui::Window(r, "소지품", &m_invOpen);
	if (m_enchant.stage == 1)
	{
		const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 6.f);
		ui::Plate(Rectangle{ r.x - 3, r.y - 3, r.width + 6, r.height + 6 }, BLANK, util::WithA(ui::theme::kDawn, 0.45f + 0.45f * pulse), 6.f, 2.f);
		ui::Text("각인할 무기를 고르세요 (Esc 취소)", Vector2{ r.x + r.width * 0.5f, r.y + r.height - 22 }, 13.5f, ui::theme::kDawnHi, Align::Center, true);
	}

	// 장비창
	const Rectangle equipArea{ r.x + 12.f, r.y + 44.f, equipW, h - 100.f };
	DrawEquipPanel(equipArea);

	// 도구줄: 검색 + 정리
	const float gx = r.x + 24.f + equipW, gy = r.y + 44.f;
	const Rectangle searchR{ gx, gy, gridW - 96.f, 30.f };
	ui::InputText(searchR, m_invSearch, "검색: 이름 · 희귀도 · 옵션");
	const bool canSort = m_now - m_sortSentAt > 1.0;
	if (ui::Button(Rectangle{ gx + gridW - 88.f, gy, 88.f, 30.f }, "정리", ButtonKind::Normal, canSort, 15.f))
	{
		m_net.SendItemSort();
		m_sortSentAt = m_now;
		sfx::Play(sfx::Id::Drop, 0.45f, 0.1f);
	}
	const std::string& query = m_invSearch.text;

	// 가방 칸
	const float x0 = gx, y0 = gy + 38.f;
	m_invGridPos = Vector2{ x0, y0 };
	const Rectangle gridArea{ x0 - 4.f, y0 - 4.f, gridW + 8.f, 5 * (slot + gap) + 4.f };
	m_invHover = -1;
	bool droppedOnCell = false;
	for (int i = 0; i < MAX_INVENTORY_SLOTS; ++i)
	{
		const Rectangle s{ x0 + (i % 6) * (slot + gap), y0 + (i / 6) * (slot + gap), slot, slot };
		InvSlot& it = m_inv.slots[i];
		const bool hover = ui::Hovered(s);
		if (hover) m_invHover = i;
		bool selectable = true;
		if (m_enchant.stage == 1)
			selectable = !it.Empty() && ItemTableData::IsWeapon(it.itemId);
		const bool match = it.Empty() || MatchesSearch(it, query);
		const Rarity rar = SlotRarity(it);
		const bool gear = !it.Empty() && ItemTableData::SlotOf(it.itemId) != EquipSlot::None;
		Color accent = ui::theme::kBorder;
		if (gear && rar != Rarity::Common) accent = icons::RarityColor(rar);
		ui::SlotFrame(s, hover && selectable, m_sellSlot == i || m_enchant.targetSlot == i, accent);
		if (!it.Empty())
		{
			const bool dragged = ui::Dragging() && (ui::Drag().kind == 1 || ui::Drag().kind == 3) && ui::Drag().index == i;
			const Color tint = (!selectable || dragged) ? Color{ 255, 255, 255, 80 } : (match ? WHITE : Color{ 255, 255, 255, 55 });
			// 희귀도 바탕 빛
			// 희귀도: 바탕을 아주 옅게 물들인다(빛나게 하지 않는다). 전설 이상만 은은한 숨.
			if (gear && rar >= Rarity::Rare && match)
			{
				const Color rc = icons::RarityColor(rar);
				ui::Plate(Rectangle{ s.x + 1, s.y + 1, s.width - 2, s.height - 2 }, util::MulA(rc, 0.08f), BLANK, 2.f, 0.f);
				if (rar >= Rarity::Legendary)
				{
					const float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(m_now) * 2.0f + i);
					DrawCircleGradient(static_cast<int>(s.x + slot * 0.5f), static_cast<int>(s.y + slot * 0.5f), slot * 0.48f, util::MulA(rc, 0.20f * pulse), util::MulA(rc, 0.f));
				}
			}
			const Color glow = icons::EnchantGlow(it.enchant);
			if (glow.a > 0 && match)
				DrawCircleGradient(static_cast<int>(s.x + slot * 0.5f), static_cast<int>(s.y + slot * 0.5f), slot * 0.5f, util::MulA(glow, 0.8f), util::WithA(glow, 0.f));
			icons::Draw(it.itemId, Rectangle{ s.x + 4, s.y + 4, slot - 8, slot - 8 }, tint);
			if (it.count > 1)
			{
				ui::TextStyle cs; cs.size = 14.f; cs.color = ui::theme::kTextHi; cs.outline = 1.2f; cs.face = ui::Face::Number;
				const std::string cnt = std::to_string(it.count);
				ui::Fonts().Draw(cnt, Vector2{ s.x + slot - 4.f - ui::Fonts().Measure(cnt, cs.size, 0.f, ui::Face::Number).x, s.y + slot - 17.f }, cs);
			}
			if (it.enchant > 0)
			{
				ui::TextStyle es; es.size = 14.f; es.color = it.enchant >= 7 ? ui::theme::kDawnHi : ui::theme::kOldway; es.outline = 1.2f; es.face = ui::Face::Number;
				ui::Fonts().Draw("+" + std::to_string(it.enchant), Vector2{ s.x + 5, s.y + 3 }, es);
			}
			if (it.equipped)
			{
				// 착용 표시: 오른쪽 위 모서리의 새벽빛 삼각형
				DrawTriangle(Vector2{ s.x + slot - 13.f, s.y + 1.f }, Vector2{ s.x + slot - 1.f, s.y + 13.f }, Vector2{ s.x + slot - 1.f, s.y + 1.f }, ui::theme::kDawn);
			}
			// 갈아입을 만한 장비(▲)
			if (gear && !it.equipped && m_enchant.stage == 0 && UpgradeScore(i) > 8)
			{
				const float bob = std::sin(static_cast<float>(m_now) * 4.f) * 1.5f;
				ui::Text("▲", Vector2{ s.x + 5, s.y + slot - 18 + bob }, 12.f, ui::theme::kGood, Align::Left, true);
			}
			// 방금 들어온 장비: 반짝임(마우스를 올리면 사라진다)
			if (it.newAt > 0.f && m_now - it.newAt < 60.0)
			{
				const float tw = 0.5f + 0.5f * std::sin(static_cast<float>(m_now) * 7.f + i);
				const Color sc = gear ? icons::RarityColor(rar) : Rgb(0xFFF2B0);
				DrawPoly(Vector2{ s.x + slot - 9, s.y + 12 }, 4, 3.f + 2.5f * tw, static_cast<float>(m_now) * 90.f, util::MulA(util::Shade(sc, 1.3f), 0.6f + 0.4f * tw));
				if (hover) it.newAt = -100.f;
			}
			if (hover && !ui::Dragging())
				DrawItemTooltip(it, m_shopMode == ShopMode::Sell ? TipMode::Sell : TipMode::Inventory, i);
		}

		// ── 입력 ──
		if (m_enchant.stage == 1)
		{
			if (ui::Clicked(s) && selectable)
			{
				m_enchant.targetSlot = i;
				m_enchant.itemId = it.itemId;
				m_enchant.before = it.enchant;
				m_enchant.stage = 2;
				sfx::Play(sfx::Id::Click);
			}
			continue;
		}
		// 장비창에서 끌어온 장비: 빈 칸이면 그 칸으로 옮기며 벗고, 아니면 제자리에서 벗는다
		if (ui::DragReleased() && hover && ui::Drag().kind == 3)
		{
			const int from = ui::Drag().index;
			if (it.Empty())
			{
				m_net.SendSwap(static_cast<uint16_t>(from), static_cast<uint16_t>(i));
				m_net.SendUnequip(static_cast<uint16_t>(i));
			}
			else
				m_net.SendUnequip(static_cast<uint16_t>(from));
			ui::EndDrag();
			droppedOnCell = true;
			continue;
		}
		if (it.Empty())
		{
			if (ui::DragReleased() && hover && ui::Drag().kind == 1 && ui::Drag().index != i)
			{
				m_net.SendSwap(static_cast<uint16_t>(ui::Drag().index), static_cast<uint16_t>(i));
				ui::EndDrag();
			}
			continue;
		}
		if (m_shopMode == ShopMode::Sell)
		{
			if (ui::Clicked(s))
			{
				const ItemTableRow* row = ItemTableData::Find(it.itemId);
				if (row && row->sellPrice > 0 && !it.equipped) { m_sellSlot = i; m_sellCount = it.count; sfx::Play(sfx::Id::Click, 0.6f); }
				else { AddCenter(it.equipped ? "착용 중인 장비는 팔 수 없습니다." : "그 물건은 팔 수 없습니다.", Rgb(0xFF8A7A)); sfx::Play(sfx::Id::Error, 0.5f); }
			}
			continue;
		}
		// Shift+클릭: 채팅에 아이템 고리(Phase 3-B) — [이름 +n]이 들어가고 받는 쪽은 툴팁으로 본다
		if ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) && ui::Pressed(s))
		{
			ChatInsertItemLink(it.itemId, it.enchant, static_cast<uint8_t>(SlotRarity(it)));
			ui::ConsumeMouse();
			m_pressKind = 0;
			continue;
		}
		if (ui::DoubleClicked(s) || ui::Pressed(s, MOUSE_BUTTON_RIGHT))
		{
			const ItemTableRow* row = ItemTableData::Find(it.itemId);
			if (row && row->type == ItemType::eEquipment && !it.equipped)
				TryEquipFromUi(i, EquipSlot::None);
			else
				UseSlot(i);
			ui::ConsumeMouse();
			m_pressKind = 0;
			continue;
		}
		if (ui::Pressed(s))
		{
			m_pressKind = 1; m_pressIndex = i; m_pressPos = ui::Mouse();
			ui::ConsumeMouse();
		}
		if (m_pressKind == 1 && m_pressIndex == i && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !ui::Dragging()
			&& util::Len(util::Sub(ui::Mouse(), m_pressPos)) > 6.f)
		{
			ui::BeginDrag(ui::DragPayload{ 1, i, it.itemId });
			m_pressKind = 0;
		}
		if (ui::DragReleased() && hover && ui::Drag().kind == 1 && ui::Drag().index != i)
		{
			m_net.SendSwap(static_cast<uint16_t>(ui::Drag().index), static_cast<uint16_t>(i));
			ui::EndDrag();
		}
	}
	// 장비창에서 끌어온 장비를 가방 칸 사이(빈 곳)에 놓아도 벗는다
	if (!droppedOnCell && ui::DragReleased() && ui::Drag().kind == 3 && ui::Hovered(gridArea))
	{
		m_net.SendUnequip(static_cast<uint16_t>(ui::Drag().index));
		ui::EndDrag();
	}
	// 창 안(칸 밖)에 떨어뜨리면 취소
	if (ui::DragReleased() && ui::Hovered(r))
		ui::EndDrag();
	if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && (m_pressKind == 1 || m_pressKind == 3))
		m_pressKind = 0;

	// 아래 정보
	const float fy = r.y + r.height - (m_enchant.stage == 1 ? 58.f : 46.f);
	ui::Divider(r.x + 12, r.x + r.width - 12, fy - 6);
	Coin(Vector2{ r.x + 24, fy + 12 }, 7.f);
	{
		ui::TextStyle ms; ms.size = 17.f; ms.color = ui::theme::kTextHi; ms.face = ui::Face::Number;
		ui::Fonts().Draw(texts::Number(m_inv.adena), Vector2{ r.x + 37, fy + 3 }, ms);
	}
	ui::Text(Fmt("빈 칸 %d/%d", m_inv.FreeSlots(), MAX_INVENTORY_SLOTS), Vector2{ r.x + r.width - 14, fy + 5 }, ui::type::kCaption + 0.5f, ui::theme::kTextDim, Align::Right);
	if (!query.empty())
	{
		int hits = 0;
		for (const auto& sl : m_inv.slots) if (!sl.Empty() && MatchesSearch(sl, query)) ++hits;
		ui::Text(Fmt("검색 %d개", hits), Vector2{ r.x + r.width * 0.5f + 60.f, fy + 5 }, ui::type::kCaption + 0.5f, ui::theme::kInfo, Align::Center);
	}
	else
		ui::Text("장비는 끌어서 입고 · 가방으로 끌어 벗는다", Vector2{ r.x + r.width * 0.5f + 60.f, fy + 6 }, 12.f, ui::theme::kTextMute, Align::Center);
}
