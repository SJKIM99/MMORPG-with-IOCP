#include "game/Game.h"
#include "render/Post.h"
#include "fx/FxScale.h"
#include "game/KeyBinds.h"
#include "render/Terrain.h"
#include "game/Texts.h"
#include "fx/Effects.h"
#include "fx/Particles.h"
#include "fx/SkillFx.h"
#include "render/Draw.h"
#include "render/Actors.h"
#include "render/Icons.h"
#include "audio/Sfx.h"
#include "audio/Voice.h"
#include "core/Util.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

using util::Rgb;

namespace
{
	template <typename T>
	bool As(const char* data, uint16_t size, T& out)
	{
		if (size < sizeof(T)) return false;
		std::memcpy(&out, data, sizeof(T));
		return true;
	}

	std::string FixedStr(const char* s, size_t n)
	{
		size_t len = 0;
		while (len < n && s[len] != '\0') ++len;
		return std::string(s, len);
	}

	uint32_t HashName(const std::string& s)
	{
		uint32_t h = 2166136261u;
		for (char c : s) { h ^= static_cast<uint8_t>(c); h *= 16777619u; }
		return h;
	}

	int Cheb(int ax, int ay, int bx, int by) { return std::max(std::abs(ax - bx), std::abs(ay - by)); }

	float EntityHeight(const Entity& e)
	{
		if (e.kind == EntityKind::Monster && e.row) return actors::MonsterHeight(*e.row);
		if (e.kind == EntityKind::Item) return 16.f;
		return actors::PlayerHeight();
	}

	void FaceToward(Entity& e, int tx, int ty)
	{
		const int dx = tx - e.tx, dy = ty - e.ty;
		const int sx = dx - dy, sy = dx + dy;
		if (sx != 0) e.faceRight = sx > 0;
		if (sy != 0) e.faceBack = sy < 0;
	}
}

void Game::OnPacket(const char* data, uint16_t size)
{
	const auto type = static_cast<PacketType>(static_cast<uint8_t>(data[2]));
	switch (type)
	{
	case PacketType::USER_LOGIN_ACK:
	{
		USER_LOGIN_ACK_PACKET p{};
		if (As(data, size, p)) HandleLoginAck(p);
		break;
	}
	case PacketType::USER_LOGIN_FAIL_ACK:
		m_net.Disconnect();
		m_scene = Scene::Login;
		m_loginError = "로그인에 실패했습니다. 잠시 후 다시 시도하세요.";
		sfx::Play(sfx::Id::Error);
		break;

	case PacketType::SUBJECT_ADD_NFY:
	{
		SUBJECT_ADD_NFY_PACKET p{};
		if (As(data, size, p)) HandleSubjectAdd(p);
		break;
	}
	case PacketType::SUBJECT_MOVE_NFY:
	{
		SUBJECT_MOVE_NFY_PACKET p{};
		if (As(data, size, p)) HandleSubjectMove(p);
		break;
	}
	case PacketType::SUBJECT_REMOVE_NFY:
	{
		SUBJECT_REMOVE_NFY_PACKET p{};
		if (As(data, size, p)) HandleSubjectRemove(p);
		break;
	}
	case PacketType::SUBJECT_DIE_NFY:
	{
		SUBJECT_DIE_NFY_PACKET p{};
		if (As(data, size, p)) HandleSubjectDie(p);
		break;
	}
	case PacketType::SUBJECT_RESPAWN_NFY:
	{
		SUBJECT_RESPAWN_NFY_PACKET p{};
		if (As(data, size, p)) HandleSubjectRespawn(p);
		break;
	}
	case PacketType::PLAYER_ATTACK_NFY:
	{
		PLAYER_ATTACK_NFY_PACKET p{};
		if (!As(data, size, p)) break;
		if (Entity* a = Find(p.attacker_id); a && p.attacker_id != m_selfId)
		{
			a->attackT = 0.f;
			PlayAt(static_cast<int>(sfx::Id::Swing), a->pos, 0.5f, 0.1f);
		}
		break;
	}
	case PacketType::SUBJECT_EQUIP_CHANGE_NFY:
	{
		SUBJECT_EQUIP_CHANGE_NFY_PACKET p{};
		if (!As(data, size, p)) break;
		if (Entity* e = Find(p.id))
		{
			e->itemId = p.itemId;
			if (p.itemId == 0) e->enchant = 0;
		}
		break;
	}
	case PacketType::USER_HEAL_INF:
	{
		USER_HEAL_INF_PACKET p{};
		if (!As(data, size, p)) break;
		m_hp = std::max(0, p.hp);
		if (Entity* me = Self()) me->hp = m_hp;
		break;
	}
	case PacketType::USER_STAT_CHANGE_INF:
	{
		USER_STAT_CHANGE_INF_PACKET p{};
		if (!As(data, size, p)) break;
		m_level = p.level;
		m_hp = p.hp;
		m_maxhp = p.maxhp;
		m_exp = p.exp;
		m_expToNext = LevelTable::ExpToNext(p.level);
		if (Entity* me = Self()) { me->hp = m_hp; me->maxhp = m_maxhp; me->level = m_level; }
		break;
	}
	case PacketType::SYSTEM_MESSAGE_INF:
	{
		SYSTEM_MESSAGE_INF_PACKET p{};
		if (As(data, size, p)) HandleSystemMessage(p);
		break;
	}
	case PacketType::SC_CHAT:
	{
		SC_CHAT_PACKET p{};
		if (!As(data, size, p)) break;
		std::string name;
		if (p.sender_id == m_selfId) name = m_playerName;
		else if (Entity* e = Find(p.sender_id)) name = e->name;
		else name = "???";
		HandleChat(p.sender_id, name, FixedStr(p.mess, CHAT_SIZE), static_cast<uint8_t>(ChatChannel::Normal));
		break;
	}

	// ── 인벤토리(레거시 ACK는 성공/실패 피드백에만 쓴다. 슬롯 내용은 ITEM_SLOT_EX_INF가 원본) ──
	case PacketType::ITEM_EQUIP_ACK:
	{
		ITEM_EQUIP_ACK_PACKET p{};
		if (!As(data, size, p)) break;
		if (p.success)
		{
			sfx::Play(sfx::Id::Equip);
			if (Entity* me = Self()) me->itemId = p.slot.itemId;
		}
		else sfx::Play(sfx::Id::Error, 0.5f);
		break;
	}
	case PacketType::ITEM_UNEQUIP_ACK:
	{
		ITEM_UNEQUIP_ACK_PACKET p{};
		if (!As(data, size, p)) break;
		if (p.success) { sfx::Play(sfx::Id::Equip, 0.6f, 0.1f); if (Entity* me = Self()) me->itemId = 0; }
		break;
	}
	case PacketType::ITEM_SWAP_ACK:
	{
		ITEM_SWAP_ACK_PACKET p{};
		if (As(data, size, p) && p.success) sfx::Play(sfx::Id::Drop, 0.4f, 0.1f);
		break;
	}
	case PacketType::ITEM_DISCARD_ACK:
	{
		ITEM_DISCARD_ACK_PACKET p{};
		if (!As(data, size, p)) break;
		sfx::Play(p.success ? sfx::Id::Drop : sfx::Id::Error, 0.7f);
		break;
	}
	case PacketType::ITEM_PICKUP_ACK:
	{
		ITEM_PICKUP_ACK_PACKET p{};
		if (As(data, size, p) && p.success) sfx::Play(sfx::Id::Pickup, 0.7f);
		break;
	}
	case PacketType::ITEM_USE_ACK:
	{
		ITEM_USE_ACK_PACKET p{};
		if (As(data, size, p) && !p.success)
		{
			AddCenter("지금은 사용할 수 없습니다.", Rgb(0xC8C8C8));
			sfx::Play(sfx::Id::Error, 0.5f);
		}
		break;
	}
	case PacketType::ITEM_LIST_ACK:
	case PacketType::ITEM_ACQUIRE_INF:
	case PacketType::USER_ATTACK_ACK:
	case PacketType::SUBJECT_ATTACK_NFY:
		break;  // 확장 세션은 *_EX / SUBJECT_DAMAGE_NFY로 같은 정보를 더 자세히 받는다

	// ── 확장 프로토콜 ──
	case PacketType::USER_INFO_EX_INF:
	{
		USER_INFO_EX_INF_PACKET p{};
		if (As(data, size, p)) HandleInfoEx(p);
		break;
	}
	case PacketType::SUBJECT_INFO_EX_NFY:
	{
		SUBJECT_INFO_EX_NFY_PACKET p{};
		if (As(data, size, p)) HandleSubjectInfoEx(p);
		break;
	}
	case PacketType::USER_MOVE_EX_ACK:
	{
		USER_MOVE_EX_ACK_PACKET p{};
		if (As(data, size, p)) HandleMoveAck(p);
		break;
	}
	case PacketType::SERVER_CONFIG_INF:
	{
		SERVER_CONFIG_INF_PACKET p{};
		if (As(data, size, p)) HandleServerConfig(p);
		break;
	}
	case PacketType::ITEM_LIST_EX_ACK:
	{
		ITEM_LIST_EX_ACK_PACKET p{};
		if (!As(data, size, p)) break;
		for (auto& s : m_inv.slots) s = InvSlot{};
		m_invBaselineAt = -1.0;   // 전체 스냅샷 — 획득 판정 기준을 버린다
		const int n = std::min<int>(p.slotCount, MAX_INVENTORY_SLOTS);
		for (int i = 0; i < n; ++i)
			HandleSlot(p.items[i], false);
		break;
	}
	case PacketType::ITEM_SLOT_EX_INF:
	{
		ITEM_SLOT_EX_INF_PACKET p{};
		if (As(data, size, p)) HandleSlot(p.slot, true);
		break;
	}
	case PacketType::ADENA_INF:
	{
		ADENA_INF_PACKET p{};
		if (!As(data, size, p)) break;
		m_inv.adena = p.adena;
		const auto reason = static_cast<AdenaReason>(p.reason);
		if (reason == AdenaReason::Pickup && p.delta > 0)
		{
			sfx::Play(sfx::Id::Coin, 0.9f);
			if (Entity* me = Self())
			{
				fx::Fx().FloatText(TileToIso(me->pos), actors::PlayerHeight() + 10.f, "+" + texts::Number(p.delta) + " 그로시", Rgb(0xE8E4DA), 19.f);
				fx::Fx().CoinBurst(TileToIso(me->pos), std::clamp(p.delta / 20, 3, 12));
			}
		}
		else if (reason == AdenaReason::Teleport && p.delta < 0)
			AddChat("옛길 이동 비용 " + texts::Number(-p.delta) + " 그로시를 치렀습니다.", Rgb(0xC8B070));
		else if (reason == AdenaReason::Quest && p.delta > 0)
		{
			sfx::Play(sfx::Id::Coin, 0.8f);
			if (Entity* me = Self())
				fx::Fx().CoinBurst(TileToIso(me->pos), std::clamp(p.delta / 60, 4, 16));
		}
		break;
	}
	case PacketType::ENCHANT_RESULT_ACK:
	{
		ENCHANT_RESULT_ACK_PACKET p{};
		if (!As(data, size, p)) break;
		const auto result = static_cast<EnchantResult>(p.result);
		if (result == EnchantResult::Invalid)
		{
			m_enchant = EnchantFlow{};
			AddCenter("강화할 수 없습니다.", Rgb(0xFF8A7A));
			sfx::Play(sfx::Id::Error);
			break;
		}
		m_enchant.resultKnown = true;
		m_enchant.result = result;
		m_enchant.before = p.enchantBefore;
		m_enchant.after = p.enchantAfter;
		m_enchant.itemId = p.itemId;
		break;
	}
	case PacketType::SHOP_RESULT_ACK:
	{
		SHOP_RESULT_ACK_PACKET p{};
		if (!As(data, size, p)) break;
		const auto kind = static_cast<ShopKind>(p.kind);
		const auto result = static_cast<ShopResult>(p.result);
		if (result == ShopResult::Duplicate)
			break;
		if (result != ShopResult::Success)
		{
			AddCenter(texts::ShopResultText(kind, result), Rgb(0xFF8A7A));
			sfx::Play(sfx::Id::Error);
			break;
		}
		if (kind == ShopKind::Buy)
		{
			sfx::Play(sfx::Id::Buy);
			AddChat(texts::ItemName(p.itemId) + " ×" + std::to_string(p.count) + " 구매 (" + texts::Number(-p.adenaDelta) + " 그로시)", Rgb(0xC8E0A0));
		}
		else if (kind == ShopKind::Sell)
		{
			sfx::Play(sfx::Id::Coin);
			AddChat(texts::ItemName(p.itemId) + " ×" + std::to_string(p.count) + " 판매 (+" + texts::Number(p.adenaDelta) + " 그로시)", Rgb(0xC8E0A0));
			m_sellSlot = -1;
		}
		else
		{
			CloseNpcWindows();
			ClearGoal();
		}
		break;
	}
	case PacketType::SUBJECT_DAMAGE_NFY:
	{
		SUBJECT_DAMAGE_NFY_PACKET p{};
		if (As(data, size, p)) QueueDamage(p);
		break;
	}
	// ── 전투 V2 ──
	case PacketType::USER_STATS_INF:
	{
		USER_STATS_INF_PACKET p{};
		if (As(data, size, p)) HandleStats(p);
		break;
	}
	case PacketType::USER_RESOURCE_INF:
	{
		USER_RESOURCE_INF_PACKET p{};
		if (As(data, size, p)) HandleResource(p);
		break;
	}
	case PacketType::SKILL_RESULT_ACK:
	{
		SKILL_RESULT_ACK_PACKET p{};
		if (As(data, size, p)) HandleSkillResult(p);
		break;
	}
	case PacketType::SKILL_CAST_NFY:
	{
		SKILL_CAST_NFY_PACKET p{};
		if (As(data, size, p)) HandleSkillCast(p);
		break;
	}
	// Phase 2-B 기술 구조 V3
	case PacketType::SKILL_EX_NFY:
	{
		SKILL_EX_NFY_PACKET p{};
		if (As(data, size, p)) HandleSkillEx(p);
		break;
	}
	case PacketType::SUBJECT_FORCED_MOVE_NFY:
	{
		SUBJECT_FORCED_MOVE_NFY_PACKET p{};
		if (As(data, size, p)) HandleForcedMove(p);
		break;
	}
	case PacketType::COMBAT_GAUGE_INF:
	{
		COMBAT_GAUGE_INF_PACKET p{};
		if (As(data, size, p)) HandleCombatGauge(p);
		break;
	}
	case PacketType::MONSTER_GAUGE_NFY:
	{
		MONSTER_GAUGE_NFY_PACKET p{};
		if (As(data, size, p)) HandleMonsterGauge(p);
		break;
	}
	// Phase 2-D 기술 성장
	case PacketType::SKILL_TREE_INF:
	{
		SKILL_TREE_INF_PACKET p{};
		if (As(data, size, p)) HandleSkillTree(p);
		break;
	}
	case PacketType::SKILL_GROW_ACK:
	{
		SKILL_GROW_ACK_PACKET p{};
		if (As(data, size, p)) HandleSkillGrowAck(p);
		break;
	}
	case PacketType::STATUS_NFY:
	{
		STATUS_NFY_PACKET p{};
		if (As(data, size, p)) HandleStatus(p);
		break;
	}
	case PacketType::MONSTER_TELEGRAPH_NFY:
	{
		MONSTER_TELEGRAPH_NFY_PACKET p{};
		if (As(data, size, p)) HandleTelegraph(p);
		break;
	}
	// ── 살아 있는 마을 ──
	case PacketType::NPC_LIST_NFY:
	{
		NPC_LIST_NFY_PACKET p{};
		if (As(data, size, p)) HandleNpcList(p);
		break;
	}
	case PacketType::NPC_STATE_NFY:
	{
		NPC_STATE_NFY_PACKET p{};
		if (As(data, size, p)) HandleNpcState(p);
		break;
	}
	case PacketType::NPC_BARK_NFY:
	{
		NPC_BARK_NFY_PACKET p{};
		if (As(data, size, p)) HandleNpcBark(p);
		break;
	}
	case PacketType::NPC_TALK_ACK:
	{
		NPC_TALK_ACK_PACKET p{};
		if (As(data, size, p)) HandleNpcTalkAck(p);
		break;
	}
	case PacketType::NPC_SERVICE_ACK:
	{
		NPC_SERVICE_ACK_PACKET p{};
		if (As(data, size, p)) HandleNpcServiceAck(p);
		break;
	}
	case PacketType::EFFECT_NFY:
	{
		EFFECT_NFY_PACKET p{};
		if (As(data, size, p)) HandleEffect(p);
		break;
	}
	case PacketType::ITEM_DETAIL_INF:
	{
		ITEM_DETAIL_INF_PACKET p{};
		if (As(data, size, p)) HandleItemDetail(p);
		break;
	}
	case PacketType::PLAYER_LOOK_NFY:
	{
		PLAYER_LOOK_NFY_PACKET p{};
		if (As(data, size, p)) HandlePlayerLook(p);
		break;
	}
	case PacketType::QUEST_LIST_INF:
	{
		QUEST_LIST_INF_PACKET p{};
		if (As(data, size, p)) HandleQuestList(p);
		break;
	}
	case PacketType::QUEST_UPDATE_INF:
	{
		QUEST_UPDATE_INF_PACKET p{};
		if (As(data, size, p)) HandleQuestUpdate(p);
		break;
	}
	case PacketType::QUEST_RESULT_ACK:
	{
		QUEST_RESULT_ACK_PACKET p{};
		if (As(data, size, p)) HandleQuestResult(p);
		break;
	}
	case PacketType::WORLD_NOTICE_INF:
	{
		WORLD_NOTICE_INF_PACKET p{};
		if (!As(data, size, p)) break;
		const auto code = static_cast<WorldNoticeCode>(p.code);
		const std::string text = texts::WorldNotice(code, p.param1, p.param2, FixedStr(p.name, NAME_SIZE));
		Color c = ui::theme::kDawn;
		switch (code)
		{
		case WorldNoticeCode::BossSpawned: c = ui::theme::kEmber; sfx::Play(sfx::Id::BossRoar, 0.5f); break;
		case WorldNoticeCode::BossKilled: c = ui::theme::kDawn; sfx::Play(sfx::Id::Notice); break;
		case WorldNoticeCode::EnchantSuccess: c = ui::theme::kOldway; sfx::Play(sfx::Id::Notice, 0.7f); break;
		case WorldNoticeCode::PlayerKilled: c = ui::theme::kBad; sfx::Play(sfx::Id::Notice, 0.6f); break;
		case WorldNoticeCode::RareLoot:
			c = icons::RarityColor(static_cast<Rarity>(std::clamp<int>(p.param2, 0, static_cast<int>(Rarity::Count) - 1)));
			sfx::Play(sfx::Id::LootMythic, 0.45f);
			break;
		case WorldNoticeCode::StoryComplete:
			c = Rgb(0xF2DEB8);
			if (FixedStr(p.name, NAME_SIZE) != m_playerName) sfx::Play(sfx::Id::Heartbeat, 0.5f, 0.f);
			break;
		}
		AddBanner(text, c);
		AddChatMsg(chat::Ch::Notice, "", 0, text, c);
		break;
	}
	case PacketType::SC_CHAT_EX:
	{
		SC_CHAT_EX_PACKET p{};
		if (!As(data, size, p)) break;
		HandleChat(p.senderId, FixedStr(p.senderName, NAME_SIZE), FixedStr(p.mess, CHAT_SIZE), p.channel);
		break;
	}
	default:
		break;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
void Game::HandleLoginAck(const USER_LOGIN_ACK_PACKET& p)
{
	ResetWorld();
	m_selfId = p.id;
	m_hasSelf = true;
	m_skillReady = {};
	m_skillCdTotal = {};
	m_skillFlash = {};
	m_readyPrev = {};
	m_delayedHits.clear();
	m_deferredDies.clear();
	m_chainHops.clear();
	m_queuedSkill = -1;
	m_bufferSlot = -1;
	m_dashWaitSeq = 0;
	m_actionLockUntil = 0.0;
	m_comboHits = 0;
	m_statsKnown = false;
	// 기술 구조 V3 예측 상태
	m_heldSkill = 0; m_heldHot = -1;
	m_comboStage = 0; m_comboAt = -10.0;
	m_chainSkill = 0; m_chainStage = 0;
	m_toggleSkill = 0;
	m_identity = 0; m_identityFull = false; m_identityActiveUntil = 0.0;
	m_timelines.clear();
	m_bounceMarks.clear();
	m_expectDashSkill = 0;
	m_awakeTotal = AwakeTotal{};
	m_cine = Cinematic{};
	InitNpcs();
	m_level = p.level;
	m_hp = p.hp;
	m_maxhp = p.maxhp;
	m_exp = p.exp;
	m_expToNext = LevelTable::ExpToNext(p.level);
	m_attack = LevelTable::BaseAttack(p.level);
	m_mover.Reset(p.x, p.y);
	SyncMover();
	m_hpShown = m_hpLag = m_maxhp > 0 ? static_cast<float>(m_hp) / m_maxhp : 1.f;

	Entity me;
	me.id = p.id;
	me.kind = EntityKind::Player;
	me.name = m_playerName;
	me.nameHash = HashName(m_playerName);
	me.tx = p.x; me.ty = p.y;
	motion::RemoteSnap(me, Vector2{ static_cast<float>(p.x), static_cast<float>(p.y) });
	me.hp = p.hp; me.maxhp = p.maxhp; me.level = p.level;
	me.spawnT = 0.f;
	m_ents[p.id] = me;

	m_scene = Scene::Game;
	m_cam.SnapTo(TileToIso(me.pos, 0.6f));
	LoadControls();
	fx::Fx().TeleportIn(TileToIso(me.pos));
	sfx::Play(sfx::Id::Teleport, 0.6f);
	AddChat(std::string(WorldData::kTownName) + "에 오신 것을 환영합니다, " + m_playerName + "님!", Rgb(0xFFE08A));
	{
		using kb::Act;
		auto L = [](Act a) { return kb::Label(a); };
		AddChat("이동 좌클릭 · 공격 우클릭/" + L(Act::BasicAttack) + " · 기술 " + L(Act::Skill1) + L(Act::Skill2) + L(Act::Skill3) + L(Act::Skill4) + "·"
			+ L(Act::Skill5) + L(Act::Skill6) + L(Act::Skill7) + L(Act::Skill8) + " · 각성기 " + L(Act::Awaken) + " · 대화/줍기 " + L(Act::Interact)
			+ " · 물약 " + L(Act::Potion) + " · 도움말 " + L(Act::Help) + " · 조작 설정 Esc", Rgb(0xB8B0A0));
	}
}

void Game::HandleSubjectAdd(const SUBJECT_ADD_NFY_PACKET& p)
{
	if (p.id == m_selfId)
		return;
	const bool existed = m_ents.count(p.id) != 0;
	Entity& e = m_ents[p.id];
	e.id = p.id;
	e.tx = p.x;
	e.ty = p.y;
	e.removing = false;
	e.removeT = 0.f;
	if (!existed)
	{
		motion::RemoteSnap(e, Vector2{ static_cast<float>(p.x), static_cast<float>(p.y) });
		e.spawnT = 0.f;
		e.faceRight = util::RandI(0, 1) == 1;
	}
	else
		motion::RemoteStep(e, p.x, p.y, 0.25f);
	e.dead = false;
	e.deadT = 0.f;

	switch (static_cast<EnumCategory>(p.id.GetCategory()))
	{
	case EnumCategory::eUser:
		e.kind = EntityKind::Player;
		e.name = FixedStr(p.name, NAME_SIZE);
		e.nameHash = HashName(e.name);
		e.itemId = p.itemId;
		if (e.maxhp == 0) { e.maxhp = e.hp = 100; }
		break;
	case EnumCategory::eMonster:
	{
		e.kind = EntityKind::Monster;
		e.species = p.id.GetKIND();
		e.row = MonsterTable::Find(e.species);
		if (!e.row)  // 종족 정보가 없는 레거시 몬스터: 선공/비선공만 구분해서 대신 그린다
			e.row = MonsterTable::Find(p.monster_type == AGGRO ? 3 : 1);
		e.aggressive = p.monster_type == AGGRO;
		e.name = e.row ? e.row->name : FixedStr(p.name, NAME_SIZE);
		if (!existed)
		{
			e.maxhp = e.row ? e.row->maxHp : MONSTER_MAX_HP;
			e.hp = e.maxhp;
			e.hpShown = e.hpLag = 1.f;
		}
		e.level = e.row ? e.row->level : 1;
		if (e.IsBoss() && !existed)
		{
			fx::Fx().BossSpawn(TileToIso(e.pos));
			if (m_now - m_bossWarnAt > 20.0)
			{
				m_bossWarnAt = m_now;
				sfx::Play(sfx::Id::BossRoar, 0.8f);
				if (e.row) voice::Play("boss_" + std::to_string(e.row->id) + "_spawn", 1.f, 3);
				m_cam.AddTrauma(0.35f);
				AddCenter("강력한 기운이 느껴집니다... " + e.name, Rgb(0xFF6A5A));
			}
		}
		break;
	}
	case EnumCategory::eItem:
	{
		e.kind = EntityKind::Item;
		e.itemId = p.itemId;
		e.name = texts::ItemName(p.itemId);
		if (!existed)
		{
			// 방금 근처에서 죽은 몬스터가 있으면 거기서 튀어나와 떨어진다
			e.dropT = 0.f;
			e.dropFrom = e.pos;
			float best = 3.5f;
			for (const auto& [oid, o] : m_ents)
			{
				if (o.kind != EntityKind::Monster || !o.dead || o.deadT > 1.2f) continue;
				const float d = static_cast<float>(Cheb(o.tx, o.ty, p.x, p.y));
				if (d < best) { best = d; e.dropFrom = o.pos; }
			}
			const ItemTableRow* row = ItemTableData::Find(p.itemId);
			if (row && row->type == ItemType::eEquipment && row->grade >= ItemGrade::eRare)
			{
				fx::Fx().Beam(TileToIso(e.pos), 34.f, 240.f, 1.6f, icons::GradeColor(row->grade));
				PlayAt(static_cast<int>(sfx::Id::Notice), e.pos, 0.5f);
			}
			else
				PlayAt(static_cast<int>(sfx::Id::Drop), e.pos, 0.4f, 0.15f);
		}
		break;
	}
	default:
		break;
	}
}

void Game::HandleSubjectMove(const SUBJECT_MOVE_NFY_PACKET& p)
{
	if (p.id == m_selfId)
	{
		// 기술 이동(돌진 · 구르기 · 회피 · 달 가르기 · 도약)을 기다리는 중: 순간이동 대신 표의 칸당 시간으로 미끄러진다
		if (m_expectDashSkill != 0 && m_now < m_expectDashUntil)
		{
			const SkillDef* d = SkillDefTable::Find(m_expectDashSkill);
			const int tiles = std::max(std::abs(p.x - m_mover.SrvX()), std::abs(p.y - m_mover.SrvY()));
			const double dur = d ? d->moveMs * tiles / 1000.0 : 0.0;
			m_mover.Dash(p.x, p.y, m_now, dur);
			SyncMover();
			m_expectDashSkill = 0;
			m_dashWaitSeq = 0;
			m_actionLockUntil = std::max(m_actionLockUntil, m_now + dur * 0.85);
			return;
		}
		// 확장 세션의 보통 걸음은 USER_MOVE_EX_ACK로 온다. 여기로 오는 건 존 이동 메아리
		// (예측 경로 위의 칸 → 무시)이거나 순간이동/재배치다.
		if (m_mover.OnServerPosition(p.x, p.y, m_now))
		{
			SyncMover();
			if (Entity* me = Self())
				motion::RemoteSnap(*me, m_mover.Visual());
			m_cam.SnapTo(TileToIso(m_mover.Visual(), 0.6f));
			ClearGoal();
			m_moveHeld = false;
		}
		return;
	}

	Entity* e = Find(p.id);
	if (!e) return;
	FaceToward(*e, p.x, p.y);
	const int fromX = e->tx, fromY = e->ty;
	e->tx = p.x;
	e->ty = p.y;
	// 서버가 실어 준 걸음 시간(ms)대로 보간. 0이면 순간이동 → 스냅.
	float dur = p.move_time / 1000.f;
	bool far = false;
	if (e->kind == EntityKind::Monster && p.move_time == 0 && e->tele.pattern == static_cast<uint8_t>(MonsterPattern::Leap)
		&& e->tele.t >= e->tele.dur - 0.1f && e->tele.t < e->tele.dur + 0.8f && Cheb(fromX, fromY, p.x, p.y) <= 6)
	{
		// 도약 패턴: 포물선으로 뛰어든다
		dur = 0.3f;
		far = true;
		e->hopT = 0.f;
		e->hopH = 34.f + 10.f * Cheb(fromX, fromY, p.x, p.y);
	}
	else if (e->kind == EntityKind::Monster && p.move_time > 0 && p.move_time < 120)
		far = true;   // 넉백·돌진(빠른 걸음)은 밀린 걸음을 기다리지 않고 바로 따라간다
	motion::RemoteStep(*e, p.x, p.y, dur, far);
	static const bool debugRemote = std::getenv("HARVEN_REMOTE_DEBUG") != nullptr;   // 원격 재생 진단(stderr)
	if (e->kind == EntityKind::Player && debugRemote)
		std::fprintf(stderr, "[remote] t=%.4f %d,%d dur=%.3f qn=%d tt=%.3f margin=%.3f\n", m_now, p.x, p.y, dur, e->track.qn, e->track.t, e->track.margin);
}

void Game::HandleMoveAck(const USER_MOVE_EX_ACK_PACKET& p)
{
	if (const double sent = m_mover.SentAt(p.seq); sent >= 0.0)
	{
		m_moveRttLastMs = static_cast<float>((m_now - sent) * 1000.0);
		m_moveRttMs = m_moveRttMs <= 0.f ? m_moveRttLastMs : m_moveRttMs + (m_moveRttLastMs - m_moveRttMs) * 0.15f;
	}
	m_mover.OnAck(p.seq, p.x, p.y, static_cast<MoveResult>(p.result), p.stepMs, m_now);
	SyncMover();
	if (static_cast<MoveResult>(p.result) != MoveResult::Ok)
		m_path.clear();
}

void Game::HandleServerConfig(const SERVER_CONFIG_INF_PACKET& p)
{
	m_viewRange = std::clamp<int>(p.viewRange, 3, 10);
	m_mover.SetConfig(p.baseStepMs, p.diagPermille);
	m_baseSpeedPercent = p.moveSpeedPercent > 0 ? p.moveSpeedPercent : 100u;
	if (m_hasteUntil > m_now && m_baseSpeedPercent > MoveRule::kHasteBonusPercent)
		m_baseSpeedPercent -= MoveRule::kHasteBonusPercent;   // 가속 중 로그인이면 가속분은 따로 더한다
	Terrain::SetViewRange(static_cast<float>(m_viewRange));
}

void Game::HandleSubjectRemove(const SUBJECT_REMOVE_NFY_PACKET& p)
{
	Entity* e = Find(p.id);
	if (!e || p.id == m_selfId) return;
	if (e->kind == EntityKind::Item)
	{
		// 방금 내가 주운 것이면 나에게 날아온다
		if (m_now - m_pickupSentAt < 1.5 && e->tx == m_srvX && e->ty == m_srvY)
		{
			e->flyT = 0.f;
			e->dropFrom = e->pos;
			return;
		}
	}
	if (e->dead) return;   // 쓰러지는 연출은 끝까지
	e->removing = true;
	e->removeT = 0.f;
	if (m_goal.target == p.id) ClearGoal();
}

void Game::HandleSubjectDie(const SUBJECT_DIE_NFY_PACKET& p)
{
	// 투사체·연타가 아직 닿지 않았으면 그 뒤에 쓰러뜨린다(TickCombat)
	if (HasDelayedHitFor(p.id))
	{
		m_deferredDies.push_back(p);
		return;
	}
	Entity* e = Find(p.id);
	if (!e) return;
	e->tele.active = false;
	e->windup = 0.f;
	e->animT = -1.f;
	for (auto& st : e->statuses) st = Entity::StatusFx{};
	e->dead = true;
	e->deadT = 0.f;
	e->hp = 0;
	e->attackT = -1.f;
	const Vector2 iso = TileToIso(e->pos);
	if (e->kind == EntityKind::Monster && e->row)
	{
		const float sc = e->row->scale / 100.f;
		fx::Fx().DeathBurst(iso, EntityHeight(*e), Rgb(e->row->color1), Rgb(e->row->color2), sc, e->IsBoss());
		PlayAt(static_cast<int>(sfx::Id::Kill), e->pos, 0.9f);
		if (e->IsBoss())
		{
			m_cam.AddTrauma(0.7f);
			m_cam.Kick(0.08f);
			fx::Fx().HitStop(0.18f);
			sfx::Play(sfx::Id::Slam, 0.8f);
			voice::Play("boss_" + std::to_string(e->row->id) + "_die", 1.f, 3);
		}
	}
	if (p.id == m_selfId)
	{
		m_dead = true;
		m_deadAt = m_now;
		m_restartSent = false;
		m_hp = 0;
		ClearGoal();
		m_target = ObjID{};
		m_moveHeld = false;
		CloseNpcWindows();
		m_enchant = EnchantFlow{};
		sfx::Play(sfx::Id::Death);
		voice::Play("pc_" + std::to_string(static_cast<int>(m_classId)) + "_die", 0.95f, 2);
		m_cam.AddTrauma(0.6f);
		fx::Fx().Flash(Rgb(0x8A0A0A), 0.5f, 0.6f);
		fx::Fx().HitStop(0.25f);
	}
	else if (e->kind == EntityKind::Player)
		PlayAt(static_cast<int>(sfx::Id::Death), e->pos, 0.6f);
	if (m_target == p.id) m_target = ObjID{};
}

void Game::HandleSubjectRespawn(const SUBJECT_RESPAWN_NFY_PACKET& p)
{
	if (p.id == m_selfId)
	{
		// 부활: 이전 시야 목록은 모두 무효(서버가 새로 ADD를 보낸다)
		Entity keep = m_ents[m_selfId];
		m_ents.clear();
		keep.dead = false;
		keep.deadT = 0.f;
		keep.tx = p.x; keep.ty = p.y;
		motion::RemoteSnap(keep, Vector2{ static_cast<float>(p.x), static_cast<float>(p.y) });
		keep.hp = p.hp;
		keep.spawnT = 0.f;
		m_ents[m_selfId] = keep;
		m_dead = false;
		m_hp = p.hp;
		m_mover.Reset(p.x, p.y);
		SyncMover();
		m_path.clear();
		m_goal = Goal{};
		m_cam.SnapTo(TileToIso(keep.pos, 0.6f));
		fx::Fx().Restart(TileToIso(keep.pos));
		sfx::Play(sfx::Id::Heal);
		AddCenter("다시 일어섰습니다.", Rgb(0xFFF2C8));
		return;
	}

	const bool existed = m_ents.count(p.id) != 0;
	Entity& e = m_ents[p.id];
	e.id = p.id;
	e.tx = p.x; e.ty = p.y;
	motion::RemoteSnap(e, Vector2{ static_cast<float>(p.x), static_cast<float>(p.y) });
	e.dead = false;
	e.deadT = 0.f;
	e.removing = false;
	e.spawnT = 0.f;
	e.hp = p.hp;
	if (p.id.GetCategory() == EnumCategory::eMonster)
	{
		e.kind = EntityKind::Monster;
		e.species = p.id.GetKIND();
		e.row = MonsterTable::Find(e.species);
		if (!e.row) e.row = MonsterTable::Find(p.monster_type == AGGRO ? 3 : 1);
		e.name = e.row ? e.row->name : FixedStr(p.name, NAME_SIZE);
		e.maxhp = e.row ? e.row->maxHp : MONSTER_MAX_HP;
		e.level = e.row ? e.row->level : 1;
		e.hpShown = e.hpLag = e.HpRatio();
		if (e.IsBoss())
			fx::Fx().BossSpawn(TileToIso(e.pos));
	}
	else
	{
		e.kind = EntityKind::Player;
		if (!existed || e.name.empty())
		{
			e.name = FixedStr(p.name, NAME_SIZE);
			e.nameHash = HashName(e.name);
		}
		if (e.maxhp == 0) e.maxhp = p.hp;
	}
	fx::Fx().Dust(TileToIso(e.pos), 3.f);
}

void Game::HandleEffect(const EFFECT_NFY_PACKET& p)
{
	const auto id = static_cast<EffectId>(p.effectId);
	const Vector2 tile{ static_cast<float>(p.x), static_cast<float>(p.y) };
	const Vector2 iso = TileToIso(tile);
	Entity* subj = Find(p.subjectId);
	const bool self = p.subjectId == m_selfId;
	switch (id)
	{
	case EffectId::SkillCross:
		if (self && m_now - m_localSkillFxAt < 0.8) break;   // 내가 이미 즉시 재생했다
		fx::Fx().SkillCross(iso);
		if (subj) subj->castT = 0.f;
		PlayAt(static_cast<int>(sfx::Id::Skill), tile, 0.8f);
		break;
	case EffectId::LevelUp:
		fx::Fx().LevelUp(iso);
		PlayAt(static_cast<int>(sfx::Id::LevelUp), tile, 1.f, 0.f);
		if (self)
		{
			m_cam.AddTrauma(0.3f);
			m_cam.Kick(0.05f);
			voice::Play("pc_" + std::to_string(static_cast<int>(m_classId)) + "_lvl", 0.9f, 2);
			AddBanner("레벨 " + std::to_string(p.param), ui::theme::kDawn, 3.5f);
			fx::Fx().FloatText(iso, actors::PlayerHeight() + 20.f, "LEVEL UP", Rgb(0xF2C47C), 30.f, 1.8f, ui::Face::Latin);
		}
		break;
	case EffectId::PotionHeal:
		fx::Fx().Heal(iso, actors::PlayerHeight());
		PlayAt(static_cast<int>(self ? sfx::Id::Potion : sfx::Id::Heal), tile, 0.8f);
		if (self && p.param > 0)
			fx::Fx().Damage(iso, actors::PlayerHeight() + 4.f, p.param, fx::DamageStyle::Heal);
		break;
	case EffectId::TeleportOut:
		fx::Fx().TeleportOut(iso);
		PlayAt(static_cast<int>(sfx::Id::Teleport), tile, 0.9f, 0.f);
		break;
	case EffectId::TeleportIn:
		fx::Fx().TeleportIn(iso);
		if (self) sfx::Play(sfx::Id::Teleport, 0.5f, 0.f);
		break;
	case EffectId::HasteOn:
		fx::Fx().Haste(iso);
		PlayAt(static_cast<int>(sfx::Id::Haste), tile, 0.9f, 0.f);
		break;
	case EffectId::EnchantSuccess:
	case EffectId::EnchantFail:
		if (self) break;   // 내 강화는 강화 창에서 연출을 맞춰 보여 준다
		fx::Fx().EnchantResult(iso, id == EffectId::EnchantSuccess);
		PlayAt(static_cast<int>(id == EffectId::EnchantSuccess ? sfx::Id::EnchantSuccess : sfx::Id::EnchantFail), tile, 0.7f, 0.f);
		break;
	case EffectId::BossSlamCast:
	{
		const int radius = std::max(1, p.param);
		fx::Fx().Telegraph(iso, radius, 1.2f, Rgb(0xFF3A2A));
		if (subj) subj->castT = 0.f;
		PlayAt(static_cast<int>(sfx::Id::SlamWarn), tile, 1.f, 0.f);
		if (Cheb(m_predX, m_predY, p.x, p.y) <= radius)
			AddCenter("바닥의 붉은 표식에서 벗어나세요!", Rgb(0xE8705E));
		break;
	}
	case EffectId::BossSlamImpact:
	{
		fx::Fx().BossSlamImpact(iso, std::max(1, p.param));
		PlayAt(static_cast<int>(sfx::Id::Slam), tile, 1.2f, 0.f);
		const int d = Cheb(m_predX, m_predY, p.x, p.y);
		m_cam.AddTrauma(d <= 6 ? 0.55f : 0.2f);
		m_cam.Kick(0.04f);
		if (subj) subj->attackT = 0.f;
		break;
	}
	case EffectId::PlayerKill:
		fx::Fx().PlayerKill(iso);
		PlayAt(static_cast<int>(sfx::Id::HitHeavy), tile, 0.9f);
		break;
	case EffectId::Restart:
		if (!self) fx::Fx().Restart(iso);
		break;
	case EffectId::BossSpawn:
		fx::Fx().BossSpawn(iso);
		PlayAt(static_cast<int>(sfx::Id::BossRoar), tile, 1.f);
		break;
	// ── V2 ──
	case EffectId::RuneExplosion:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::Blink), tile, 0.8f, 0.1f);
		PlayAt(static_cast<int>(sfx::Id::HitHeavy), tile, 0.6f, 0.1f);
		if (self) m_cam.AddTrauma(0.15f);
		break;
	case EffectId::SteamBurst:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::SteamBurst), tile, 0.9f, 0.05f);
		break;
	case EffectId::Counter:
		skillfx::OnServerEffect(id, subj ? TileToIso(subj->pos) : iso, p.param);
		if (subj) { subj->animSkill = 1; subj->animT = 0.f; subj->animDur = 0.3f; }
		break;
	case EffectId::WallSlam:
		skillfx::OnServerEffect(id, iso, p.param);
		if (Cheb(m_predX, m_predY, p.x, p.y) <= 6) m_cam.AddTrauma(0.12f);
		break;
	case EffectId::PoisonBurst:
		skillfx::OnServerEffect(id, subj ? TileToIso(subj->pos) : iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::PoisonBurst), tile, 0.9f, 0.05f);
		break;
	// ── Phase 2-B 기술 구조 V3 ──
	case EffectId::StaggerBreak:
	case EffectId::OathRelease:
		break;   // 무력화는 MONSTER_GAUGE_NFY, 서약 해방은 SKILL_EX_NFY(고유 기술)가 연출한다
	case EffectId::Resonance:
	{
		fxscale::Scope scope(self);
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::Burst), tile, 0.7f, 0.02f);
		PlayAt(static_cast<int>(sfx::Id::BellSmall), tile, 0.4f, 0.02f);
		if (self) { m_feelStop = std::max(m_feelStop, 0.04f); m_feelTrauma = std::max(m_feelTrauma, 0.2f); }
		break;
	}
	case EffectId::PerfectDodge:
		if (subj)
		{
			fx::Fx().FloatText(TileToIso(subj->pos), actors::PlayerHeight() + 24.f, "완벽 회피", Rgb(0xE8EEF6), self ? 22.f : 16.f, 1.1f, ui::Face::Display);
			if (self)
			{
				post::Desaturate(0.65f, 0.35f);
				sfx::Play(sfx::Id::Perfect, 0.8f, 0.01f);
				m_feelStop = std::max(m_feelStop, 0.06f);
			}
		}
		break;
	case EffectId::CloneStrike:
	{
		fxscale::Scope scope(self);
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::InkSplash), tile, 0.5f, 0.1f);
		break;
	}
	case EffectId::Taunted:
		if (subj && p.param > 0)
			fx::Fx().FloatText(TileToIso(subj->pos), actors::PlayerHeight() + 30.f, "도발 ×" + std::to_string(p.param), Rgb(0xE0604E), self ? 18.f : 14.f, 1.0f);
		break;
	// ── 장비 특수 효과(모양은 skillfx, 소리·숫자는 여기) ──
	case EffectId::ProcThunder:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::Thunder), tile, 0.55f, 0.08f);
		if (self) m_cam.AddTrauma(0.08f);
		break;
	case EffectId::ProcLifesteal:
		skillfx::OnServerEffect(id, subj ? TileToIso(subj->pos) : iso, p.param);
		if (self && p.param > 0)
			fx::Fx().Damage(subj ? TileToIso(subj->pos) : iso, actors::PlayerHeight() + 2.f, p.param, fx::DamageStyle::Heal, 0x11FE00u);
		break;
	case EffectId::ProcFrost:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::Freeze), tile, 0.4f, 0.08f);
		break;
	case EffectId::ProcExecute:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::Execute), tile, 0.35f, 0.08f);
		break;
	case EffectId::ProcEcho:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::ComboSting), tile, 0.35f, 0.12f);
		break;
	case EffectId::ProcGuardian:
		skillfx::OnServerEffect(id, subj ? TileToIso(subj->pos) : iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::GuardUp), tile, 0.6f);
		if (self) AddCenter("서약의 방패 — 철벽 태세", Rgb(0xE8C890));
		break;
	case EffectId::ProcEmber:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::FireRing), tile, 0.25f, 0.1f);
		break;
	case EffectId::ProcVenom:
		skillfx::OnServerEffect(id, iso, p.param);
		PlayAt(static_cast<int>(sfx::Id::PoisonSlash), tile, 0.3f, 0.1f);
		break;
	case EffectId::PriestHeal:
		// 사제의 기도: 금빛 기둥 + 회복 숫자
		fx::Fx().Beam(subj ? TileToIso(subj->pos) : iso, 46.f, 200.f, 0.9f, Rgb(0xFFE8A0, 200));
		fx::Fx().Heal(subj ? TileToIso(subj->pos) : iso, actors::PlayerHeight());
		if (self && p.param > 0)
			fx::Fx().Damage(iso, actors::PlayerHeight() + 4.f, p.param, fx::DamageStyle::Heal);
		if (!self) PlayAt(static_cast<int>(sfx::Id::Heal), tile, 0.6f);
		break;
	default:
		break;
	}
}

void Game::HandleSlot(const ITEM_SLOT_EX_DATA& s, bool announce)
{
	if (s.slotIndex >= MAX_INVENTORY_SLOTS) return;
	const uint16_t itemId = s.count > 0 ? s.itemId : 0;
	// 획득 판정의 기준점: 이번 변경 묶음의 첫 알림 직전 상태(TickLoot가 잠깐 뒤 비교한다)
	if (announce && m_invBaselineAt < 0.0)
	{
		m_invBaseline = m_inv.slots;
		m_invBaselineAt = m_now;
	}
	InvSlot& slot = m_inv.slots[s.slotIndex];
	const uint16_t oldItem = slot.itemId;
	slot.itemId = itemId;
	slot.count = itemId ? s.count : 0;
	slot.equipped = itemId && s.equipped;
	slot.enchant = itemId ? s.enchant : 0;
	// 인스턴스 값(희귀도·옵션)은 바로 뒤의 ITEM_DETAIL_INF가 채운다
	if (itemId != oldItem || itemId == 0)
	{
		slot.extra = 0;
		slot.newAt = -100.f;
	}

	// 내 무기 외형 갱신
	if (Entity* me = Self())
	{
		const int eq = m_inv.EquippedSlot();
		me->itemId = eq >= 0 ? m_inv.slots[eq].itemId : 0;
		me->enchant = eq >= 0 ? m_inv.slots[eq].enchant : 0;
	}

	// 획득 알림은 TickLoot가 한다(교환·정리로 옮겨진 물건을 새로 얻은 것으로 착각하지 않게)
	// 강화 창이 보던 슬롯이 사라지면 정리
	if (m_enchant.stage == 2 && (s.slotIndex == m_enchant.targetSlot || s.slotIndex == m_enchant.scrollSlot) && oldItem != itemId)
		m_enchant = EnchantFlow{};
	if (m_sellSlot == s.slotIndex && itemId == 0)
		m_sellSlot = -1;
}

void Game::HandleSystemMessage(const SYSTEM_MESSAGE_INF_PACKET& p)
{
	const auto code = static_cast<SystemMessageCode>(p.code);
	bool important = false;
	std::string text = texts::SystemMessage(code, p.param1, p.param2, important);
	if (code == SystemMessageCode::ChatNoTarget && !m_lastWhisperTo.empty())
		text = texts::Josa(m_lastWhisperTo, "은", "는") + " 지금 접속해 있지 않습니다.";
	Color c = Rgb(0xE8D8A8);
	switch (code)
	{
	case SystemMessageCode::HasteStart:
		m_hasteUntil = m_now + p.param1;
		c = Rgb(0x8AFF9A);
		break;
	case SystemMessageCode::HasteEnd:
		m_hasteUntil = 0.0;
		break;
	case SystemMessageCode::BecameChaotic:
	case SystemMessageCode::BecamePurple:
	case SystemMessageCode::ItemDroppedOnDeath:
		c = Rgb(0xFF7A6A);
		break;
	case SystemMessageCode::LevelUp:
		c = Rgb(0xFFD66A);
		break;
	case SystemMessageCode::ItemCooldown:
		c = Rgb(0xB8B8B8);
		sfx::Play(sfx::Id::Error, 0.35f);
		break;
	case SystemMessageCode::TargetTooFar:
	case SystemMessageCode::TargetInvalid:
		c = Rgb(0xB8B8B8);
		break;
	case SystemMessageCode::SafeZoneNoCombat:
		ClearGoal();
		break;
	default:
		break;
	}
	AddChat(text, c);
	if (important) AddCenter(text, c);
}

void Game::HandleInfoEx(const USER_INFO_EX_INF_PACKET& p)
{
	m_level = p.level;
	m_exp = p.exp;
	m_expToNext = p.expToNext;
	m_hp = p.hp;
	m_maxhp = p.maxhp;
	if (!m_statsKnown) m_attack = p.attack;   // V2는 USER_STATS_INF의 전투 능력치를 쓴다
	m_inv.adena = p.adena;
	m_alignment = p.alignment;
	m_pkCount = p.pkCount;
	m_flags = p.flags;
	m_hasteUntil = p.hasteRemainMs > 0 ? m_now + p.hasteRemainMs / 1000.0 : 0.0;
	if (Entity* me = Self())
	{
		me->hp = p.hp; me->maxhp = p.maxhp; me->level = p.level;
		me->alignment = p.alignment; me->flags = p.flags;
	}
}

void Game::HandleSubjectInfoEx(const SUBJECT_INFO_EX_NFY_PACKET& p)
{
	Entity* e = Find(p.id);
	if (!e) return;
	e->level = p.level;
	e->alignment = p.alignment;
	e->flags = p.flags;
	e->enchant = p.enchant;
	if (e->kind == EntityKind::Player)
	{
		e->classId = ClassTable::IsValid(p.classId) ? p.classId : 0;
		e->weaponGrade = p.weaponGrade;
	}
	else if (e->kind == EntityKind::Item)
	{
		// 필드 장비의 희귀도(빛기둥 색). 막 떨어지는 중이면 희귀도에 맞는 소리로 알린다.
		const uint8_t before = e->weaponGrade;
		e->weaponGrade = p.weaponGrade;
		if (e->dropT < 1.f && p.weaponGrade > before && p.weaponGrade >= static_cast<uint8_t>(Rarity::Rare))
		{
			const Vector2 tile{ static_cast<float>(e->tx), static_cast<float>(e->ty) };
			switch (static_cast<Rarity>(p.weaponGrade))
			{
			case Rarity::Rare:      PlayAt(static_cast<int>(sfx::Id::LootRare), tile, 0.45f); break;
			case Rarity::Heroic:    PlayAt(static_cast<int>(sfx::Id::LootHeroic), tile, 0.6f); break;
			case Rarity::Legendary: PlayAt(static_cast<int>(sfx::Id::LootLegendary), tile, 0.75f); m_cam.AddTrauma(0.12f); break;
			default:                PlayAt(static_cast<int>(sfx::Id::LootMythic), tile, 0.85f); m_cam.AddTrauma(0.2f); break;
			}
		}
	}
	if (p.maxhp > 0)
	{
		e->hp = p.hp;
		e->maxhp = p.maxhp;
	}
}

// HandleChat(채팅 받기)은 game/GameChat.cpp(Phase 3-B)
