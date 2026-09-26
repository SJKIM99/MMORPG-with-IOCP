#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 아주 작은 JSON(Phase 3-A: config/ui_<캐릭터>.json) — 의존성 없이 읽고 쓴다.
//   · 값: null · bool · 숫자(double) · 문자열(UTF-8 그대로, \" \\ \n \t \uXXXX) · 배열 · 객체(키 순서 = 사전순)
//   · 설정 파일 크기(수 KB)에 맞춘 단순 재귀 하강 파서. 틀린 문서는 Parse가 false.
// ─────────────────────────────────────────────────────────────────────────────
namespace json
{
	struct Value
	{
		enum class Type { Null, Bool, Number, String, Array, Object };
		Type type = Type::Null;
		bool b = false;
		double n = 0.0;
		std::string s;
		std::vector<Value> a;
		std::map<std::string, Value> o;

		Value() = default;
		static Value Num(double v) { Value x; x.type = Type::Number; x.n = v; return x; }
		static Value Str(std::string v) { Value x; x.type = Type::String; x.s = std::move(v); return x; }
		static Value Bool(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
		static Value Arr() { Value x; x.type = Type::Array; return x; }
		static Value Obj() { Value x; x.type = Type::Object; return x; }

		[[nodiscard]] bool IsObject() const { return type == Type::Object; }
		[[nodiscard]] bool IsArray() const { return type == Type::Array; }
		[[nodiscard]] const Value* Get(std::string_view key) const;
		[[nodiscard]] double NumOr(std::string_view key, double def) const;
		[[nodiscard]] bool BoolOr(std::string_view key, bool def) const;
		[[nodiscard]] std::string StrOr(std::string_view key, std::string def) const;
		Value& Set(const std::string& key, Value v) { type = Type::Object; return o[key] = std::move(v); }
		Value& Push(Value v) { type = Type::Array; a.push_back(std::move(v)); return a.back(); }
	};

	bool Parse(std::string_view text, Value& out);
	std::string Dump(const Value& v, bool pretty = true);
	bool LoadFile(const std::string& path, Value& out);
	bool SaveFile(const std::string& path, const Value& v);   // 폴더가 없으면 만든다 · 임시 파일에 쓰고 바꿔 넣는다
}
