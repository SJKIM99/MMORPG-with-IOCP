#include "game/SkillView.h"

#include <array>
#include <memory>

namespace
{
	constexpr int kCombos = 9;   // 갈래 Ⅰ(0~2) × 갈래 Ⅱ(0~2)

	struct Cache
	{
		std::array<std::unique_ptr<SkillDef>, SkillGrowth::kIdSpan * kCombos> defs;
		std::array<std::unique_ptr<SkillRow>, SkillGrowth::kIdSpan * kCombos> rows;
	};
	Cache& C()
	{
		static Cache c;
		return c;
	}

	int ComboOf(uint8_t flags)
	{
		const SkillGrowth::State s = SkillGrowth::FromNfy(flags);
		return s.pick[0] * 3 + s.pick[1];
	}
}

namespace skillview
{
	const SkillDef* Def(uint16_t id, uint8_t flags)
	{
		const SkillDef* base = SkillDefTable::Find(id);
		if (!base || id >= SkillGrowth::kIdSpan || !SkillGrowth::Growable(id)) return base;
		const int combo = ComboOf(flags);
		if (combo == 0) return base;
		auto& slot = C().defs[static_cast<size_t>(id) * kCombos + combo];
		if (!slot)
		{
			const SkillRow* row = SkillTable::Find(id);
			if (!row) return base;
			slot = std::make_unique<SkillDef>(SkillGrowth::ApplyDef(*base, *row, SkillGrowth::FromNfy(flags)));
		}
		return slot.get();
	}

	const SkillRow* Row(uint16_t id, uint8_t flags)
	{
		const SkillRow* base = SkillTable::Find(id);
		if (!base || id >= SkillGrowth::kIdSpan || !SkillGrowth::Growable(id)) return base;
		const int combo = ComboOf(flags);
		if (combo == 0) return base;
		auto& slot = C().rows[static_cast<size_t>(id) * kCombos + combo];
		if (!slot)
			slot = std::make_unique<SkillRow>(SkillGrowth::ApplyRow(*base, SkillGrowth::FromNfy(flags)));
		return slot.get();
	}

	const SkillGrowth::Variant* Mod(uint16_t id, uint8_t flags, SkillGrowth::Mod m)
	{
		return SkillGrowth::FindMod(id, SkillGrowth::FromNfy(flags), m);
	}

	uint32_t VariantColor(uint16_t id, uint8_t flags)
	{
		const SkillGrowth::State s = SkillGrowth::FromNfy(flags);
		for (int tier = 1; tier >= 0; --tier)
			if (const SkillGrowth::Variant* v = SkillGrowth::ActiveVariant(id, s, tier); v && v->color != 0)
				return v->color;
		return 0;
	}
}
