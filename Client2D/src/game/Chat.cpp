#include "game/Chat.h"
#include "ui/Ui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace chat
{
	const char* ChName(Ch c)
	{
		switch (c)
		{
		case Ch::Normal: return "일반";
		case Ch::World: return "전체";
		case Ch::Trade: return "거래";
		case Ch::Guild: return "결사";
		case Ch::WhisperIn: return "귓속말";
		case Ch::WhisperOut: return "귓속말";
		case Ch::System: return "시스템";
		case Ch::Notice: return "공지";
		default: return "";
		}
	}

	const char* ChPrefix(Ch c)
	{
		switch (c)
		{
		case Ch::World: return "[전체]";
		case Ch::Trade: return "[거래]";
		case Ch::Guild: return "[결사]";
		case Ch::Notice: return "[공지]";
		default: return "";
		}
	}

	Color ChColor(Ch c)
	{
		switch (c)
		{
		case Ch::Normal: return ui::theme::kChatNormal;
		case Ch::World: return ui::theme::kChatWorld;
		case Ch::Trade: return ui::theme::kChatTrade;
		case Ch::Guild: return ui::theme::kChatGuild;
		case Ch::WhisperIn:
		case Ch::WhisperOut: return ui::theme::kChatWhisper;
		case Ch::Notice: return ui::theme::kChatNotice;
		default: return ui::theme::kChatSystem;
		}
	}

	namespace
	{
		std::string Trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && s[a] == ' ') ++a;
			while (b > a && s[b - 1] == ' ') --b;
			return s.substr(a, b - a);
		}
		// 첫 낱말과 나머지
		void SplitWord(const std::string& s, std::string& first, std::string& rest)
		{
			const size_t sp = s.find(' ');
			if (sp == std::string::npos) { first = s; rest.clear(); return; }
			first = s.substr(0, sp);
			rest = Trim(s.substr(sp + 1));
		}
		bool In(const std::string& w, std::initializer_list<const char*> list)
		{
			for (const char* x : list) if (w == x) return true;
			return false;
		}
	}

	Parsed ParseInput(const std::string& raw, const Target& current, const std::string& lastWhisperFrom)
	{
		Parsed p;
		const std::string line = Trim(raw);
		if (line.empty()) return p;
		auto sendOrSwitch = [&](Ch ch, const std::string& to, const std::string& body)
		{
			p.target.ch = ch;
			p.target.whisperTo = to;
			if (body.empty()) p.kind = Parsed::Kind::Switch;
			else { p.kind = Parsed::Kind::Send; p.body = body; }
		};
		if (line[0] == '&')   // 예전 전체 채팅 표시(호환)
		{
			sendOrSwitch(Ch::World, "", Trim(line.substr(1)));
			return p;
		}
		if (line[0] != '/')
		{
			p.kind = Parsed::Kind::Send;
			p.target = current;
			p.body = line;
			return p;
		}
		std::string cmd, rest;
		SplitWord(line, cmd, rest);
		if (In(cmd, { "/귓", "/귓속말", "/w", "/ㄱ" }))
		{
			std::string name, body;
			SplitWord(rest, name, body);
			if (name.empty()) { p.kind = Parsed::Kind::Unknown; p.arg = cmd; return p; }
			sendOrSwitch(Ch::WhisperOut, name, body);
		}
		else if (In(cmd, { "/r", "/답", "/ㄹ" }))
		{
			if (lastWhisperFrom.empty()) { p.kind = Parsed::Kind::Unknown; p.arg = cmd; return p; }
			sendOrSwitch(Ch::WhisperOut, lastWhisperFrom, rest);
		}
		else if (In(cmd, { "/길", "/결사", "/g" })) sendOrSwitch(Ch::Guild, "", rest);
		else if (In(cmd, { "/전", "/전체", "/s" })) sendOrSwitch(Ch::World, "", rest);
		else if (In(cmd, { "/거래", "/t" })) sendOrSwitch(Ch::Trade, "", rest);
		else if (In(cmd, { "/일반", "/n" })) sendOrSwitch(Ch::Normal, "", rest);
		else if (In(cmd, { "/초대" }))
		{
			p.kind = Parsed::Kind::Invite;
			std::string name, tail;
			SplitWord(rest, name, tail);
			p.arg = name;
		}
		else
		{
			p.kind = Parsed::Kind::Unknown;
			p.arg = cmd;
		}
		return p;
	}

	std::vector<CmdHint> CommandHints(std::string_view typed)
	{
		static const CmdHint kAll[] = {
			{ "/귓", "/귓 이름 내용 — 귓속말" },
			{ "/r", "/r 내용 — 받은 귓속말에 답" },
			{ "/일반", "같은 지역 모두에게" },
			{ "/전", "서버 전체 — 10초에 한 번(예전 &도 된다)" },
			{ "/거래", "거래 — 5초에 한 번" },
			{ "/길", "결사원 모두에게(접속 중인)" },
			{ "/초대", "/초대 이름 — 결사 초대(정예 이상)" },
		};
		std::vector<CmdHint> out;
		if (typed.empty() || typed[0] != '/') return out;
		const size_t sp = typed.find(' ');
		if (sp != std::string_view::npos) return out;   // 명령을 다 쳤다
		for (const auto& h : kAll)
			if (std::string_view(h.cmd).substr(0, typed.size()) == typed)
				out.push_back(h);
		return out;
	}

	bool WhisperNamePart(const std::string& text, std::string& cmd, std::string& partial)
	{
		if (text.empty() || text[0] != '/') return false;
		const size_t sp = text.find(' ');
		if (sp == std::string::npos) return false;
		cmd = text.substr(0, sp);
		if (!In(cmd, { "/귓", "/귓속말", "/w", "/ㄱ" })) return false;
		partial = text.substr(sp + 1);
		return partial.find(' ') == std::string::npos;
	}

	bool NamePrefix(std::string_view name, std::string_view partial)
	{
		if (partial.size() > name.size()) return false;
		for (size_t i = 0; i < partial.size(); ++i)
		{
			const unsigned char a = static_cast<unsigned char>(name[i]), b = static_cast<unsigned char>(partial[i]);
			if (a < 0x80 && b < 0x80) { if (std::tolower(a) != std::tolower(b)) return false; }
			else if (a != b) return false;
		}
		return true;
	}

	std::string ItemToken(uint16_t itemId, uint8_t enchant, uint8_t rarity)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "{i:%u:%u:%u}", static_cast<unsigned>(itemId), static_cast<unsigned>(enchant), static_cast<unsigned>(rarity));
		return buf;
	}

	std::vector<Piece> SplitPieces(const std::string& text)
	{
		std::vector<Piece> out;
		size_t i = 0;
		std::string plain;
		while (i < text.size())
		{
			if (text.compare(i, 3, "{i:") == 0)
			{
				const size_t end = text.find('}', i);
				if (end != std::string::npos && end - i <= 24)
				{
					unsigned id = 0, en = 0, ra = 0;
					const std::string tok = text.substr(i, end - i + 1);
					if (std::sscanf(tok.c_str(), "{i:%u:%u:%u}", &id, &en, &ra) == 3 && id > 0 && id < 65536)
					{
						if (!plain.empty()) { out.push_back(Piece{ false, plain }); plain.clear(); }
						Piece pc;
						pc.item = true;
						pc.itemId = static_cast<uint16_t>(id);
						pc.enchant = static_cast<uint8_t>(std::min(en, 255u));
						pc.rarity = static_cast<uint8_t>(std::min(ra, 255u));
						out.push_back(pc);
						i = end + 1;
						continue;
					}
				}
			}
			plain += text[i++];
		}
		if (!plain.empty()) out.push_back(Piece{ false, plain });
		return out;
	}

	std::string ClipUtf8(const std::string& s, size_t maxBytes)
	{
		if (s.size() <= maxBytes) return s;
		size_t len = maxBytes;
		while (len > 0 && (static_cast<unsigned char>(s[len]) & 0xC0) == 0x80) --len;
		return s.substr(0, len);
	}

	bool ValidUtf8(std::string_view s)
	{
		size_t i = 0;
		while (i < s.size())
		{
			const unsigned char c = static_cast<unsigned char>(s[i]);
			int len = c < 0x80 ? 1 : ((c & 0xE0) == 0xC0 ? 2 : ((c & 0xF0) == 0xE0 ? 3 : ((c & 0xF8) == 0xF0 ? 4 : 0)));
			if (len == 0 || i + static_cast<size_t>(len) > s.size()) return false;
			for (int k = 1; k < len; ++k)
				if ((static_cast<unsigned char>(s[i + static_cast<size_t>(k)]) & 0xC0) != 0x80) return false;
			if (len == 1 && (c < 0x20 || c == 0x7F)) return false;
			i += static_cast<size_t>(len);
		}
		return true;
	}

	float FontSize(int font) { return font <= 0 ? 12.5f : (font >= 2 ? 16.f : 14.f); }

	void DefaultTabs(std::vector<Tab>& tabs)
	{
		tabs.clear();
		auto add = [&](const char* name, uint32_t mask) { Tab t; t.name = name; t.mask = mask; tabs.push_back(std::move(t)); };
		add("모두", kAllMask);
		add("일반", Bit(Ch::Normal) | Bit(Ch::System) | Bit(Ch::Notice));
		add("결사", Bit(Ch::Guild));
		add("귓속말", Bit(Ch::WhisperIn) | Bit(Ch::WhisperOut));
		add("거래", Bit(Ch::Trade));
		add("시스템", Bit(Ch::System) | Bit(Ch::Notice));
	}

	json::Value ToJson(const std::vector<Tab>& tabs, int active, const Settings& s)
	{
		json::Value v = json::Value::Obj();
		json::Value arr = json::Value::Arr();
		for (const auto& t : tabs)
		{
			json::Value o = json::Value::Obj();
			o.Set("name", json::Value::Str(t.name));
			o.Set("mask", json::Value::Num(t.mask));
			o.Set("flash", json::Value::Bool(t.flash));
			o.Set("user", json::Value::Bool(t.user));
			arr.Push(std::move(o));
		}
		v.Set("tabs", std::move(arr));
		v.Set("active", json::Value::Num(active));
		v.Set("idle", json::Value::Num(s.idleAlpha));
		v.Set("busy", json::Value::Num(s.activeAlpha));
		v.Set("font", json::Value::Num(s.font));
		v.Set("clock", json::Value::Bool(s.clock));
		v.Set("fade", json::Value::Bool(s.fadeText));
		json::Value bl = json::Value::Arr();
		for (const auto& n : s.blocked) bl.Push(json::Value::Str(n));
		v.Set("block", std::move(bl));
		return v;
	}

	void FromJson(const json::Value& v, std::vector<Tab>& tabs, int& active, Settings& s)
	{
		if (!v.IsObject()) return;
		if (const json::Value* arr = v.Get("tabs"); arr && arr->IsArray() && !arr->a.empty())
		{
			std::vector<Tab> loaded;
			for (const auto& o : arr->a)
			{
				if (!o.IsObject()) continue;
				Tab t;
				t.name = o.StrOr("name", "탭");
				t.mask = static_cast<uint32_t>(o.NumOr("mask", kAllMask)) & kAllMask;
				t.flash = o.BoolOr("flash", true);
				t.user = o.BoolOr("user", false);
				if (t.name.empty() || t.mask == 0) continue;
				loaded.push_back(std::move(t));
				if (loaded.size() >= 10) break;
			}
			if (!loaded.empty()) tabs = std::move(loaded);
		}
		active = std::clamp(static_cast<int>(v.NumOr("active", 0)), 0, static_cast<int>(tabs.size()) - 1);
		s.idleAlpha = std::clamp(static_cast<float>(v.NumOr("idle", 0.35)), 0.f, 1.f);
		s.activeAlpha = std::clamp(static_cast<float>(v.NumOr("busy", 0.85)), 0.f, 1.f);
		s.font = std::clamp(static_cast<int>(v.NumOr("font", 1)), 0, 2);
		s.clock = v.BoolOr("clock", false);
		s.fadeText = v.BoolOr("fade", true);
		s.blocked.clear();
		if (const json::Value* bl = v.Get("block"); bl && bl->IsArray())
			for (const auto& n : bl->a)
				if (n.type == json::Value::Type::String && !n.s.empty()) s.blocked.push_back(n.s);
	}
}
