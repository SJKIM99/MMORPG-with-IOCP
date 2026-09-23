#include "pch.h"
#include "RegionData.h"

#include <filesystem>
#include <fstream>
#include <iomanip>

namespace
{
	constexpr char     kMagic[4]        = { 'R', 'G', 'N', '1' };
	constexpr uint32_t kFormatVersion   = 1;
	constexpr size_t   kHeaderBytes     = 64;
	constexpr size_t   kSectionEntryBytes = 16;

	// tools/region_format.py 의 PLACEMENT_STRIDE 와 같아야 한다.
	constexpr size_t kPlacementStride = 28;

	// 경계를 검사하는 순차 리더.
	//
	// 파일이 잘렸거나 섹션 오프셋이 틀리면 **정의되지 않은 동작이 아니라 실패**로
	// 끝나야 한다. 시작할 때 한 번 읽는 파일이라 검사 비용은 무의미하고,
	// 반대로 여기서 넘어가면 원인을 알 수 없는 크래시가 된다 (13장 비정상 경로).
	class ByteReader
	{
	public:
		ByteReader(const std::byte* data, size_t size) : _data(data), _size(size) {}

		[[nodiscard]] bool Ok() const noexcept { return _ok; }
		[[nodiscard]] size_t Cursor() const noexcept { return _cursor; }

		void Seek(size_t offset) noexcept
		{
			if (offset > _size) { _ok = false; return; }
			_cursor = offset;
		}

		template<typename T>
		T Read() noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			T value{};
			if (!_ok || _cursor + sizeof(T) > _size) { _ok = false; return value; }
			std::memcpy(&value, _data + _cursor, sizeof(T));
			_cursor += sizeof(T);
			return value;
		}

		// 배열을 통째로 복사한다. 원소마다 Read 를 부르면 16641개에서 느리다.
		bool ReadFloats(std::vector<float>& out, size_t count) noexcept
		{
			const size_t bytes = count * sizeof(float);
			if (!_ok || _cursor + bytes > _size) { _ok = false; return false; }
			out.resize(count);
			std::memcpy(out.data(), _data + _cursor, bytes);
			_cursor += bytes;
			return true;
		}

		[[nodiscard]] const std::byte* At(size_t offset) const noexcept
		{
			return (offset <= _size) ? _data + offset : nullptr;
		}

	private:
		const std::byte* _data;
		size_t _size;
		size_t _cursor = 0;
		bool   _ok = true;
	};

	// tools/region_format.py 의 fnv1a 와 같은 값을 내야 한다.
	uint32_t Fnv1a(const std::byte* data, size_t size) noexcept
	{
		uint32_t hash = 0x811C9DC5u;
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= static_cast<uint32_t>(data[i]);
			hash *= 0x01000193u;
		}
		return hash;
	}

	struct Section
	{
		uint32_t offset = 0;
		uint32_t bytes = 0;
		uint32_t count = 0;
	};
}

const std::string& RegionData::Str(uint32_t index) const noexcept
{
	static const std::string empty;
	return (index < _strings.size()) ? _strings[index] : empty;
}

std::string RegionData::FindRegionFile(const std::string& regionId)
{
	// 서버는 Binary\Debug 에서, 툴은 저장소 루트에서 돈다. 상대 경로를 박으면
	// 둘 중 하나가 깨지므로 world/build 가 나올 때까지 위로 올라간다.
	namespace fs = std::filesystem;
	fs::path dir = fs::current_path();
	for (int depth = 0; depth < 6; ++depth)
	{
		const fs::path candidate = dir / "world" / "build" / (regionId + ".bin");
		if (fs::exists(candidate))
			return candidate.string();

		if (!dir.has_parent_path() || dir.parent_path() == dir)
			break;
		dir = dir.parent_path();
	}
	return {};
}

std::unique_ptr<RegionData> RegionData::Load(const std::string& path, std::string& outError)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		outError = "파일을 열 수 없다: " + path;
		return nullptr;
	}

	const std::streamsize size = file.tellg();
	if (size < static_cast<std::streamsize>(kHeaderBytes))
	{
		outError = "파일이 헤더보다 짧다: " + path;
		return nullptr;
	}

	std::vector<std::byte> raw(static_cast<size_t>(size));
	file.seekg(0);
	if (!file.read(reinterpret_cast<char*>(raw.data()), size))
	{
		outError = "파일을 다 읽지 못했다: " + path;
		return nullptr;
	}

	ByteReader reader(raw.data(), raw.size());

	// ── 헤더 ────────────────────────────────────────────────────────────────
	char magic[4]{};
	for (char& c : magic) c = static_cast<char>(reader.Read<uint8_t>());
	if (std::memcmp(magic, kMagic, 4) != 0)
	{
		outError = "magic 이 RGN1 이 아니다 — 리전 바이너리가 아니거나 손상됐다";
		return nullptr;
	}

	const uint32_t version = reader.Read<uint32_t>();
	if (version != kFormatVersion)
	{
		outError = "포맷 버전이 다르다: 파일 " + std::to_string(version)
			+ ", 서버 " + std::to_string(kFormatVersion)
			+ " — tools/build_region.py 를 다시 돌려라";
		return nullptr;
	}

	const uint32_t sectionCount = reader.Read<uint32_t>();
	const uint32_t terrainHash  = reader.Read<uint32_t>();

	auto region = std::make_unique<RegionData>();
	region->_sizeX      = reader.Read<float>();
	region->_sizeZ      = reader.Read<float>();
	region->_sectorSize = reader.Read<float>();
	region->_spawn.x    = reader.Read<float>();
	region->_spawn.y    = reader.Read<float>();
	region->_spawn.z    = reader.Read<float>();
	region->_minY       = reader.Read<float>();
	region->_maxY       = reader.Read<float>();
	region->_seed       = reader.Read<uint32_t>();
	region->_pvp          = reader.Read<uint8_t>() != 0;
	region->_spawnAllowed = reader.Read<uint8_t>() != 0;
	reader.Read<uint8_t>();   // pad
	reader.Read<uint8_t>();   // pad
	const uint32_t idStr      = reader.Read<uint32_t>();
	const uint32_t displayStr = reader.Read<uint32_t>();

	if (!reader.Ok() || reader.Cursor() != kHeaderBytes)
	{
		outError = "헤더 크기가 64바이트가 아니다 — 리더와 기록기가 어긋났다";
		return nullptr;
	}

	// ── 섹션 표 ──────────────────────────────────────────────────────────────
	std::unordered_map<std::string, Section> sections;
	for (uint32_t i = 0; i < sectionCount; ++i)
	{
		reader.Seek(kHeaderBytes + i * kSectionEntryBytes);
		char tag[5]{};
		for (int c = 0; c < 4; ++c) tag[c] = static_cast<char>(reader.Read<uint8_t>());

		Section s;
		s.offset = reader.Read<uint32_t>();
		s.bytes  = reader.Read<uint32_t>();
		s.count  = reader.Read<uint32_t>();

		if (!reader.Ok() || static_cast<size_t>(s.offset) + s.bytes > raw.size())
		{
			outError = std::string("섹션 ") + tag + " 가 파일 밖을 가리킨다";
			return nullptr;
		}
		sections.emplace(tag, s);
	}

	auto find = [&sections](const char* tag) -> const Section*
	{
		const auto it = sections.find(tag);
		return (it != sections.end()) ? &it->second : nullptr;
	};

	// ── 문자열 표 ────────────────────────────────────────────────────────────
	if (const Section* strs = find("STRS"))
	{
		reader.Seek(strs->offset);
		region->_strings.reserve(strs->count);
		for (uint32_t i = 0; i < strs->count; ++i)
		{
			const uint32_t length = reader.Read<uint32_t>();
			if (!reader.Ok() || reader.Cursor() + length > raw.size())
			{
				outError = "문자열 표가 잘렸다";
				return nullptr;
			}
			region->_strings.emplace_back(
				reinterpret_cast<const char*>(reader.At(reader.Cursor())), length);
			// 항목마다 4바이트 정렬 패딩이 붙는다.
			reader.Seek(reader.Cursor() + length + ((4 - (length % 4)) % 4));
		}
	}
	region->_id          = region->Str(idStr);
	region->_displayName = region->Str(displayStr);

	// ── 지형 ────────────────────────────────────────────────────────────────
	const Section* terr = find("TERR");
	if (terr == nullptr)
	{
		outError = "TERR 섹션이 없다";
		return nullptr;
	}
	reader.Seek(terr->offset);
	region->_resolution = static_cast<int>(reader.Read<uint32_t>());
	region->_cell       = reader.Read<float>();
	if (!reader.ReadFloats(region->_heights, terr->count))
	{
		outError = "지형 높이 배열이 잘렸다";
		return nullptr;
	}

	// **읽은 것이 기록된 것과 같은지 스스로 확인한다.**
	// 섹션 오프셋을 한 칸만 잘못 읽어도 여기서 걸린다. 세 언어 리더가 같은
	// 값을 내야 하므로 포맷이 어긋나는 순간 셋 다 여기서 멈춘다.
	const uint32_t actual = Fnv1a(
		reinterpret_cast<const std::byte*>(region->_heights.data()),
		region->_heights.size() * sizeof(float));
	if (actual != terrainHash)
	{
		char buffer[128];
		::sprintf_s(buffer, "지형 해시 불일치: 파일 %08x, 계산 %08x", terrainHash, actual);
		outError = buffer;
		return nullptr;
	}
	region->_terrainHash = actual;

	// SURF(포장 색)와 COLR(색)은 **읽지 않는다.** 클라이언트 표시 전용이다.
	// 섹션 표를 쓴 덕분에 건너뛰는 데 아무 비용이 들지 않는다.

	// ── 수면 ────────────────────────────────────────────────────────────────
	if (const Section* watr = find("WATR"))
	{
		reader.Seek(watr->offset);
		region->_noWater = reader.Read<float>();
		reader.ReadFloats(region->_water, watr->count);
	}

	// ── 배치 ────────────────────────────────────────────────────────────────
	if (const Section* plac = find("PLAC"))
	{
		if (plac->bytes != plac->count * kPlacementStride)
		{
			outError = "배치 섹션 크기가 스트라이드와 맞지 않는다 — 포맷이 바뀌었다";
			return nullptr;
		}
		reader.Seek(plac->offset);
		region->_placements.resize(plac->count);
		for (RegionPlacement& p : region->_placements)
		{
			p.asset = reader.Read<uint32_t>();
			p.pos.x = reader.Read<float>();
			p.pos.y = reader.Read<float>();
			p.pos.z = reader.Read<float>();
			p.yaw   = reader.Read<float>();
			p.scale = reader.Read<float>();
			p.collision = static_cast<RegionCollision>(reader.Read<uint8_t>());
			p.nav       = static_cast<RegionNav>(reader.Read<uint8_t>());
			p.flags     = reader.Read<uint8_t>();
			reader.Read<uint8_t>();   // pad
		}
	}

	// ── NPC (가변 길이) ───────────────────────────────────────────────────────
	if (const Section* npcs = find("NPCS"))
	{
		reader.Seek(npcs->offset);
		region->_npcs.resize(npcs->count);
		for (RegionNpc& n : region->_npcs)
		{
			n.id        = reader.Read<uint32_t>();
			n.asset     = reader.Read<uint32_t>();
			n.pos.x     = reader.Read<float>();
			n.pos.y     = reader.Read<float>();
			n.pos.z     = reader.Read<float>();
			n.yaw       = reader.Read<float>();
			n.role      = reader.Read<uint32_t>();
			n.animation = reader.Read<uint32_t>();

			const uint32_t variants = reader.Read<uint32_t>();
			n.idleVariants.reserve(variants);
			for (uint32_t i = 0; i < variants; ++i)
				n.idleVariants.push_back(reader.Read<uint32_t>());
		}
	}

	// ── 몬스터 스폰 그룹 ───────────────────────────────────────────────────────
	if (const Section* spwn = find("SPWN"))
	{
		reader.Seek(spwn->offset);
		region->_spawns.resize(spwn->count);
		for (RegionSpawn& s : region->_spawns)
		{
			s.id        = reader.Read<uint32_t>();
			s.monster   = reader.Read<uint32_t>();
			s.asset     = reader.Read<uint32_t>();
			s.center.x  = reader.Read<float>();
			s.center.y  = reader.Read<float>();
			s.center.z  = reader.Read<float>();
			s.radius    = reader.Read<float>();
			s.count     = reader.Read<uint32_t>();
			s.respawnMs = reader.Read<uint32_t>();
		}
	}

	// ── 순찰 경로 (가변 길이) ─────────────────────────────────────────────────
	if (const Section* path = find("PATH"))
	{
		reader.Seek(path->offset);
		region->_routes.resize(path->count);
		for (RegionRoute& r : region->_routes)
		{
			r.id   = reader.Read<uint32_t>();
			r.loop = reader.Read<uint32_t>() != 0;

			const uint32_t points = reader.Read<uint32_t>();
			r.waypoints.reserve(points);
			for (uint32_t i = 0; i < points; ++i)
			{
				Vec3 wp;
				wp.x = reader.Read<float>();
				wp.y = reader.Read<float>();
				wp.z = reader.Read<float>();
				r.waypoints.push_back(wp);
			}
		}
	}

	if (!reader.Ok())
	{
		outError = "파일이 중간에 끊겼다 — 읽는 도중 경계를 넘었다";
		return nullptr;
	}

	return region;
}

void RegionData::PrintSummary() const
{
	cout << "[region] " << _id << " (" << _displayName << ")  "
		<< _sizeX << "x" << _sizeZ << "m  sector " << _sectorSize << "m"
		<< "  terrain " << _resolution << "^2 cell " << _cell << "m"
		<< "  y " << _minY << "~" << _maxY
		<< "  placements " << _placements.size()
		<< "  npcs " << _npcs.size()
		<< "  spawns " << _spawns.size()
		<< "  routes " << _routes.size()
		<< "  water " << _water.size()
		<< "  strings " << _strings.size()
		// 파이썬·C# 과 눈으로 대조하는 값이므로 8자리로 0 을 채운다.
		// 채우지 않으면 032127a1 이 32127a1 로 나와 "다르다" 고 착각한다.
		<< "  hash " << std::setw(8) << std::setfill('0') << std::hex << _terrainHash
		<< std::dec << std::setfill(' ')
		<< "  pvp=" << (_pvp ? "true" : "false")
		<< endl;
}
