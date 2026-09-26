#include "core/Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace json
{
	const Value* Value::Get(std::string_view key) const
	{
		if (type != Type::Object) return nullptr;
		auto it = o.find(std::string(key));
		return it == o.end() ? nullptr : &it->second;
	}
	double Value::NumOr(std::string_view key, double def) const
	{
		const Value* v = Get(key);
		return (v && v->type == Type::Number) ? v->n : def;
	}
	bool Value::BoolOr(std::string_view key, bool def) const
	{
		const Value* v = Get(key);
		if (!v) return def;
		if (v->type == Type::Bool) return v->b;
		if (v->type == Type::Number) return v->n != 0.0;
		return def;
	}
	std::string Value::StrOr(std::string_view key, std::string def) const
	{
		const Value* v = Get(key);
		return (v && v->type == Type::String) ? v->s : def;
	}

	namespace
	{
		struct Parser
		{
			std::string_view t;
			size_t i = 0;
			int depth = 0;

			void Ws() { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
			bool Lit(std::string_view w) { if (t.substr(i, w.size()) == w) { i += w.size(); return true; } return false; }

			static void Utf8(std::string& out, unsigned cp)
			{
				if (cp < 0x80) out += static_cast<char>(cp);
				else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
				else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
			}

			bool Str(std::string& out)
			{
				if (i >= t.size() || t[i] != '"') return false;
				++i;
				while (i < t.size())
				{
					const char c = t[i++];
					if (c == '"') return true;
					if (c != '\\') { out += c; continue; }
					if (i >= t.size()) return false;
					const char e = t[i++];
					switch (e)
					{
					case '"': out += '"'; break;
					case '\\': out += '\\'; break;
					case '/': out += '/'; break;
					case 'b': out += '\b'; break;
					case 'f': out += '\f'; break;
					case 'n': out += '\n'; break;
					case 'r': out += '\r'; break;
					case 't': out += '\t'; break;
					case 'u':
					{
						if (i + 4 > t.size()) return false;
						unsigned cp = static_cast<unsigned>(std::strtoul(std::string(t.substr(i, 4)).c_str(), nullptr, 16));
						i += 4;
						if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u')
						{
							const unsigned lo = static_cast<unsigned>(std::strtoul(std::string(t.substr(i + 2, 4)).c_str(), nullptr, 16));
							i += 6;
							cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						}
						Utf8(out, cp);
						break;
					}
					default: return false;
					}
				}
				return false;
			}

			bool Val(Value& v)
			{
				if (++depth > 64) return false;
				Ws();
				if (i >= t.size()) return false;
				const char c = t[i];
				bool ok = true;
				if (c == '{')
				{
					v = Value::Obj();
					++i; Ws();
					if (i < t.size() && t[i] == '}') { ++i; }
					else
					{
						for (;;)
						{
							Ws();
							std::string key;
							if (!Str(key)) { ok = false; break; }
							Ws();
							if (i >= t.size() || t[i] != ':') { ok = false; break; }
							++i;
							Value child;
							if (!Val(child)) { ok = false; break; }
							v.o[key] = std::move(child);
							Ws();
							if (i < t.size() && t[i] == ',') { ++i; continue; }
							if (i < t.size() && t[i] == '}') { ++i; break; }
							ok = false; break;
						}
					}
				}
				else if (c == '[')
				{
					v = Value::Arr();
					++i; Ws();
					if (i < t.size() && t[i] == ']') { ++i; }
					else
					{
						for (;;)
						{
							Value child;
							if (!Val(child)) { ok = false; break; }
							v.a.push_back(std::move(child));
							Ws();
							if (i < t.size() && t[i] == ',') { ++i; continue; }
							if (i < t.size() && t[i] == ']') { ++i; break; }
							ok = false; break;
						}
					}
				}
				else if (c == '"') { v = Value::Str(""); ok = Str(v.s); }
				else if (Lit("true")) v = Value::Bool(true);
				else if (Lit("false")) v = Value::Bool(false);
				else if (Lit("null")) v = Value{};
				else
				{
					const size_t st = i;
					while (i < t.size() && (std::isdigit(static_cast<unsigned char>(t[i])) || t[i] == '-' || t[i] == '+' || t[i] == '.' || t[i] == 'e' || t[i] == 'E')) ++i;
					if (st == i) ok = false;
					else v = Value::Num(std::strtod(std::string(t.substr(st, i - st)).c_str(), nullptr));
				}
				--depth;
				return ok;
			}
		};

		void Esc(std::string& out, const std::string& s)
		{
			out += '"';
			for (const unsigned char c : s)
			{
				switch (c)
				{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
					else out += static_cast<char>(c);
				}
			}
			out += '"';
		}

		void DumpTo(std::string& out, const Value& v, bool pretty, int ind)
		{
			auto nl = [&](int d) { if (pretty) { out += '\n'; out.append(static_cast<size_t>(d) * 2, ' '); } };
			switch (v.type)
			{
			case Value::Type::Null: out += "null"; break;
			case Value::Type::Bool: out += v.b ? "true" : "false"; break;
			case Value::Type::Number:
			{
				char b[40];
				if (std::isfinite(v.n) && v.n == std::floor(v.n) && std::fabs(v.n) < 1e15) std::snprintf(b, sizeof(b), "%.0f", v.n);
				else std::snprintf(b, sizeof(b), "%.4g", std::isfinite(v.n) ? v.n : 0.0);
				out += b;
				break;
			}
			case Value::Type::String: Esc(out, v.s); break;
			case Value::Type::Array:
			{
				out += '[';
				bool first = true;
				for (const auto& c : v.a)
				{
					if (!first) out += pretty ? ", " : ",";
					first = false;
					DumpTo(out, c, false, 0);   // 배열은 한 줄(설정 파일이 짧게 읽힌다)
				}
				out += ']';
				break;
			}
			case Value::Type::Object:
			{
				out += '{';
				bool first = true;
				for (const auto& [k, c] : v.o)
				{
					if (!first) out += ',';
					first = false;
					nl(ind + 1);
					Esc(out, k);
					out += pretty ? ": " : ":";
					DumpTo(out, c, pretty, ind + 1);
				}
				if (!v.o.empty()) nl(ind);
				out += '}';
				break;
			}
			}
		}
	}

	bool Parse(std::string_view text, Value& out)
	{
		Parser p{ text };
		Value v;
		if (!p.Val(v)) return false;
		p.Ws();
		if (p.i != text.size()) return false;
		out = std::move(v);
		return true;
	}

	std::string Dump(const Value& v, bool pretty)
	{
		std::string out;
		DumpTo(out, v, pretty, 0);
		if (pretty) out += '\n';
		return out;
	}

	bool LoadFile(const std::string& path, Value& out)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return false;
		std::stringstream ss;
		ss << in.rdbuf();
		return Parse(ss.str(), out);
	}

	bool SaveFile(const std::string& path, const Value& v)
	{
		std::error_code ec;
		const std::filesystem::path p(path);
		if (p.has_parent_path())
			std::filesystem::create_directories(p.parent_path(), ec);
		const std::string tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) return false;
			out << Dump(v, true);
			if (!out) return false;
		}
		std::filesystem::rename(tmp, p, ec);
		if (ec)
		{
			std::filesystem::remove(p, ec);
			std::filesystem::rename(tmp, p, ec);
		}
		return !ec;
	}
}
