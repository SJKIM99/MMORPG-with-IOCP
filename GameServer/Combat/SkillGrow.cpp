#include "pch.h"
#include "Combat/SkillGrow.h"
#include "DBThread.h"
#include "User.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	using namespace SkillGrowth;

	bool Trace()
	{
		static const bool on = std::getenv("HARVEN_SKILL_TRACE") != nullptr;
		return on;
	}

	// 이 직업의 자라는 칸(QWERASDF 기본 배치)의 기술 id
	uint16_t SlotSkill(ClassId cls, int slot)
	{
		return SkillDefTable::Default(cls, static_cast<SkillKey>(static_cast<int>(SkillKey::Q) + slot));
	}

	bool OwnsGrowable(const std::shared_ptr<User>& u, uint16_t id)
	{
		if (!Growable(id)) return false;
		const SkillRow* row = SkillTable::Find(id);
		return row != nullptr && row->cls == u->m_classId;
	}

	void Save(const std::shared_ptr<User>& u, uint16_t id)
	{
		const State s = u->GrowState(id);
		DB_SKILL_INFO info;
		info._skillId = id;
		info._level = s.level;
		info._pick1 = s.pick[0];
		info._pick2 = s.pick[1];
		GDBThread->RequestSaveSkill(u->GetPlayerId(), info);
	}

	void SendAck(const std::shared_ptr<User>& u, const SKILL_GROW_REQ_PACKET& req, SkillGrowResult r, uint16_t param, uint16_t skillId)
	{
		auto session = u->GetGameSession();
		if (!session) return;
		SKILL_GROW_ACK_PACKET p{};
		p.size = sizeof(p);
		p.type = static_cast<char>(PacketType::SKILL_GROW_ACK);
		p.seq = req.seq;
		p.op = req.op;
		p.result = static_cast<uint8_t>(r);
		p.param = param;
		p.skill.skillId = skillId;
		p.skill.state = skillId < kIdSpan ? u->m_skillGrow[skillId] : 0;
		p.points = SkillGrow::Points(u);
		session->PostSend(p);
		if (Trace())
			std::fprintf(stderr, "[skillgrow] seq=%u op=%u id=%u result=%u param=%u state=0x%02X points=%u\n",
				req.seq, req.op, skillId, static_cast<unsigned>(r), param, p.skill.state, p.points);
	}
}

namespace SkillGrow
{
	uint16_t Spent(const std::shared_ptr<User>& u)
	{
		uint32_t spent = 0;
		for (int slot = 0; slot < kSlots; ++slot)
		{
			const uint16_t id = SlotSkill(u->m_classId, slot);
			if (id < kIdSpan) spent += CostTo(u->GrowState(id).level);
		}
		return static_cast<uint16_t>(spent);
	}

	uint16_t Points(const std::shared_ptr<User>& u)
	{
		const uint16_t total = PointsAt(u->GetStat()->GetLevel());
		const uint16_t spent = Spent(u);
		return total > spent ? static_cast<uint16_t>(total - spent) : 0;
	}

	bool Load(const std::shared_ptr<User>& u, const std::vector<DB_SKILL_INFO>& rows)
	{
		u->m_skillGrow.fill(0);
		for (const DB_SKILL_INFO& r : rows)
		{
			// 다른 직업의 기록(직업을 바꿨던 캐릭터) · 범위 밖 값은 버린다
			if (!OwnsGrowable(u, r._skillId) || r._level < 1 || r._level > kMaxLevel || r._pick1 > 2 || r._pick2 > 2)
				continue;
			State s;
			s.level = r._level;
			s.pick[0] = r._pick1;
			s.pick[1] = r._pick2;
			u->m_skillGrow[r._skillId] = Pack(s);
		}
		if (Spent(u) > PointsAt(u->GetStat()->GetLevel()))
		{
			// 점수보다 많이 쓴 기록(수동 수정 · 규칙 변경) — 모두 돌려준다
			u->m_skillGrow.fill(0);
			GDBThread->RequestResetSkills(u->GetPlayerId());
			return true;
		}
		return false;
	}

	void SendTree(const std::shared_ptr<User>& u)
	{
		if (!u || !u->IsExtended()) return;
		auto session = u->GetGameSession();
		if (!session) return;
		SKILL_TREE_INF_PACKET p{};
		p.size = sizeof(p);
		p.type = static_cast<char>(PacketType::SKILL_TREE_INF);
		p.total = PointsAt(u->GetStat()->GetLevel());
		p.points = Points(u);
		p.count = 0;
		for (int slot = 0; slot < kSlots; ++slot)
		{
			const uint16_t id = SlotSkill(u->m_classId, slot);
			if (id == 0 || id >= kIdSpan) continue;
			p.skills[p.count].skillId = id;
			p.skills[p.count].state = u->m_skillGrow[id];
			++p.count;
		}
		session->PostSend(p);
	}

	void HandleRequest(const std::shared_ptr<User>& u, const SKILL_GROW_REQ_PACKET& req)
	{
		if (!u) return;
		// 재전송(같은 seq): 다시 처리하지 않고 지금 상태로 결과만
		if (u->m_growSeqSeen && req.seq == u->m_lastGrowSeq)
		{
			SendAck(u, req, SkillGrowResult::Ok, 0, req.skillId < kIdSpan ? req.skillId : 0);
			return;
		}
		const auto op = static_cast<SkillGrowOp>(req.op);
		auto done = [&](SkillGrowResult r, uint16_t param, uint16_t id)
		{
			u->m_lastGrowSeq = req.seq;
			u->m_growSeqSeen = true;
			SendAck(u, req, r, param, id);
		};
		const uint32_t now = GetNowTime();
		if (u->GetStat()->IsDead() || u->IsTransferring() || u->GetSectorX() < 0)
			return done(SkillGrowResult::Busy, 0, 0);
		// 싸우는 중 · 시전 중에는 바꾸지 않는다(시전 중인 기술의 표가 바뀌지 않게 — 예약된 타격은 시전 때 상태를 들고 있다)
		if (now - u->m_lastCombatAt < 5000 || u->m_skx.chargeSkill != 0 || u->m_skx.holdSkill != 0 || u->m_skx.toggleSkill != 0)
			return done(SkillGrowResult::InCombat, static_cast<uint16_t>((5000 - (std::min<uint32_t>)(5000, now - u->m_lastCombatAt)) / 100), 0);

		if (op == SkillGrowOp::ResetAll)
		{
			u->m_skillGrow.fill(0);
			GDBThread->RequestResetSkills(u->GetPlayerId());
			done(SkillGrowResult::Ok, 0, 0);
			SendTree(u);
			return;
		}

		const uint16_t id = req.skillId;
		if (!OwnsGrowable(u, id))
			return done(SkillGrowResult::Unknown, 0, 0);
		const SkillRow* row = SkillTable::Find(id);
		if (u->GetStat()->GetLevel() < row->unlockLevel)
			return done(SkillGrowResult::Locked, row->unlockLevel, id);

		State s = u->GrowState(id);
		switch (op)
		{
		case SkillGrowOp::LevelUp:
		{
			if (s.level >= kMaxLevel) return done(SkillGrowResult::MaxLevel, kMaxLevel, id);
			const uint8_t cost = StepCost(s.level);
			if (Points(u) < cost) return done(SkillGrowResult::NoPoints, cost, id);
			++s.level;
			break;
		}
		case SkillGrowOp::LevelDown:
		{
			if (s.level <= 1) return done(SkillGrowResult::MinLevel, 1, id);
			--s.level;
			// 갈래 레벨 아래로 내려가면 고른 갈래를 끈다(다시 올리면 다시 고른다)
			for (int tier = 0; tier < 2; ++tier)
				if (s.level < kTierLevel[tier]) s.pick[tier] = 0;
			break;
		}
		case SkillGrowOp::Pick:
		{
			if (req.tier > 1 || req.choice > 2) return done(SkillGrowResult::Unknown, 0, id);
			if (s.level < kTierLevel[req.tier]) return done(SkillGrowResult::TierLocked, kTierLevel[req.tier], id);
			s.pick[req.tier] = req.choice;
			break;
		}
		default:
			return done(SkillGrowResult::Unknown, 0, id);
		}
		u->m_skillGrow[id] = Pack(s);
		Save(u, id);
		done(SkillGrowResult::Ok, 0, id);
	}
}
