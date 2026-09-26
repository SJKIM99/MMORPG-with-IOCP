#include "render/Critters.h"

#include "audio/Sfx.h"
#include "core/Util.h"
#include "fx/Particles.h"
#include "render/Draw.h"
#include "render/Height.h"
#include "render/Wind.h"
#include "Data/WorldData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// 비둘기 떼 · 물고기 — Critters.h 설명 참고.
//
// 비둘기 한 떼의 상태: 땅(쪼기 · 종종걸음) → 놀람(가까운 새부터 0.05~0.1초 간격으로 날아오름) → 선회(광장 위 원, 새마다
// 원 위의 자리를 쫓는다) → 내려앉기(자리마다 걷는 칸 하나를 골라 미끄러지듯 내려와 날개를 몇 번 치고 선다) → 땅.
// 밤: 둥지로(멀리 · 높이 날아가 사라짐) → 아침: 먼 하늘에서 내려앉기.
// 새의 움직임은 칸/초 · z/초 단위의 조종(목표 속도로 끌려가기) — 프레임이 길어도(샌드박스 170ms) 1/60초 조각으로 적분한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace critters
{
	namespace
	{
		using util::RandF;
		using util::Rgb;

		enum class BirdState : uint8_t { Ground, Air, Away };
		enum class FlockState : uint8_t { Grounded, Flying, Landing, Leaving, Away };

		struct Bird
		{
			Vector2 p{};              // 칸
			float z = 0.f;            // 절대 높이(z)
			Vector2 v{};              // 칸/초
			float vz = 0.f;           // z/초
			float face = 1.f;         // 화면 오른쪽(+1) · 왼쪽(-1)을 본다(땅)
			float peck = -1.f;        // 쪼기 진행(0~1), 음수면 쪼지 않음
			float next = 0.f;         // 다음 행동까지(초)
			Vector2 walkTo{};         // 종종걸음 목표
			bool walking = false;
			float flap = 0.f;         // 날개 위상(라디안)
			float flapRate = 0.f;     // 초당 날갯짓(0 = 활공)
			float glideT = 0.f;       // 활공 · 날갯짓 번갈아
			float takeoff = -1.f;     // 놀란 뒤 날아오르기까지(땅에 있는 동안, 0 이상이면 곧 난다)
			Vector2 land{};           // 내려앉을 칸
			float landZ = 0.f;
			BirdState st = BirdState::Ground;
			uint8_t look = 0;         // 깃털 빛(0 회청 · 1 흰 · 2 갈색 얼룩 · 3 짙은)
			float seed = 0.f;
			float hop = 0.f;          // 내려앉은 순간 통 튐(0~1)
		};

		struct Spot
		{
			Vector2 c{};                      // 가운데(칸)
			float groundZ = 0.f;
			std::vector<Vector2> cells;       // 앉을 수 있는 칸(걷는 칸 · 같은 층 · 계단 아님)
		};

		struct Flock
		{
			int spot = 0;
			FlockState st = FlockState::Grounded;
			float t = 0.f;                    // 이 상태에 머문 시간
			float dur = 0.f;                  // 선회 시간
			Vector2 center{};                 // 선회 가운데(칸)
			float radius = 4.f, omega = 0.9f, phase0 = 0.f, alt = 4.f;
			float calm = 0.f;                 // 이 시간 동안은 놀라지 않는다(막 내려앉음)
			Vector2 scarer{};                 // 마지막으로 놀라게 한 자리
			Vector2 awayDir{ 1.f, 0.f };
			int startles = 0, landings = 0;
			std::vector<Bird> birds;
		};

		struct Fish
		{
			Vector2 a{}, b{};                 // 뛰는 곳 → 떨어지는 곳(칸)
			float waterZ = 0.f;
			float t = 0.f, dur = 0.7f, h = 0.7f;
			bool splashed = false;
			float size = 1.f;
		};

		struct Ripple
		{
			Vector2 p{};
			float t = 0.f, dur = 0.9f, r0 = 3.f, r1 = 16.f;
			float strength = 1.f;
		};

		std::vector<Spot> g_spots;
		std::vector<Flock> g_flocks;
		std::vector<Vector2> g_water;         // 개울 칸(가운데)
		std::vector<Fish> g_fish;
		std::vector<Ripple> g_ripples;
		float g_acc = 0.f;
		float g_fishAcc = 0.f;
		float g_night = 0.f;
		Vector2 g_listener{};
		Stats g_stats;
		bool g_inited = false;

		float Dist(Vector2 a, Vector2 b) { return std::hypot(a.x - b.x, a.y - b.y); }
		Vector2 Add(Vector2 a, Vector2 b) { return Vector2{ a.x + b.x, a.y + b.y }; }
		Vector2 Sub(Vector2 a, Vector2 b) { return Vector2{ a.x - b.x, a.y - b.y }; }
		Vector2 Mul(Vector2 a, float s) { return Vector2{ a.x * s, a.y * s }; }
		Vector2 Norm(Vector2 a)
		{
			const float l = std::hypot(a.x, a.y);
			return l > 1e-4f ? Vector2{ a.x / l, a.y / l } : Vector2{ 1.f, 0.f };
		}

		bool Stairs(char c) { return c == '^' || c == 'v' || c == '<' || c == '>'; }

		// 소리: 듣는 자리에서 멀수록 작게, 화면 가로 위치로 좌우
		void PlayAt(sfx::Id id, Vector2 at, float vol, float range)
		{
			const float d = Dist(at, g_listener);
			const float g = util::Clamp01(1.f - d / range);
			if (g <= 0.02f) return;
			const float dx = (at.x - at.y) - (g_listener.x - g_listener.y);
			sfx::Play(id, vol * g * g, 0.08f, util::Clamp01(0.5f + dx * 0.05f));
		}

		void BuildSpots()
		{
			// 비둘기가 모이는 구획 — 판석 · 길이 넓은 곳(districts.txt의 이름)
			static const char* kNames[] = { "분수 광장", "남문 광장", "시장 거리", "결사 회관 단", "여관 골목" };
			static const int kCount[] = { 10, 8, 10, 6, 7 };
			for (size_t n = 0; n < std::size(kNames); ++n)
			{
				const TownMap::DistrictRow* d = nullptr;
				for (const auto& row : TownMap::kDistricts)
					if (std::strcmp(row.name, kNames[n]) == 0) { d = &row; break; }
				if (!d) continue;
				// 가운데에서 가장 가까운 걷는 칸의 층을 이 자리의 층으로
				int bestLv = -1;
				float bestD = 1e9f;
				for (int dy = -4; dy <= 4; ++dy)
					for (int dx = -4; dx <= 4; ++dx)
					{
						const int x = static_cast<int>(std::lround(d->x)) + dx, y = static_cast<int>(std::lround(d->y)) + dy;
						if (WorldData::IsBlocked(x, y)) continue;
						const float dd = std::hypot(static_cast<float>(dx), static_cast<float>(dy));
						if (dd < bestD) { bestD = dd; bestLv = WorldData::TownLevel(x, y); }
					}
				if (bestLv < 0) continue;
				Spot s;
				s.c = Vector2{ d->x, d->y };
				s.groundZ = bestLv * hgt::kLevelZ;
				for (int dy = -3; dy <= 3; ++dy)
					for (int dx = -3; dx <= 3; ++dx)
					{
						const int x = static_cast<int>(std::lround(d->x)) + dx, y = static_cast<int>(std::lround(d->y)) + dy;
						if (dx * dx + dy * dy > 10) continue;
						if (WorldData::IsBlocked(x, y) || WorldData::TownLevel(x, y) != bestLv) continue;
						if (Stairs(WorldData::TownCell(x, y))) continue;
						s.cells.push_back(Vector2{ static_cast<float>(x), static_cast<float>(y) });
					}
				if (s.cells.size() < 6) continue;
				g_spots.push_back(s);

				Flock f;
				f.spot = static_cast<int>(g_spots.size()) - 1;
				const int count = kCount[n];
				for (int i = 0; i < count; ++i)
				{
					Bird b;
					const Vector2 cell = s.cells[static_cast<size_t>(util::RandI(0, static_cast<int>(s.cells.size()) - 1))];
					b.p = Vector2{ cell.x + RandF(-0.35f, 0.35f), cell.y + RandF(-0.35f, 0.35f) };
					b.z = s.groundZ;
					b.face = RandF() < 0.5f ? -1.f : 1.f;
					b.next = RandF(0.f, 2.f);
					const float r = RandF();
					b.look = r < 0.62f ? 0 : (r < 0.77f ? 1 : (r < 0.9f ? 2 : 3));
					b.seed = RandF(0.f, 100.f);
					f.birds.push_back(b);
				}
				g_flocks.push_back(std::move(f));
			}
		}

		void BuildWater()
		{
			for (int y = 0; y < TownMap::kH; ++y)
				for (int x = 0; x < TownMap::kW; ++x)
				{
					if (TownMap::kSurface[y][x] != '~') continue;
					// 물 한가운데만(가장자리 칸은 기슭 그림이 덮는다)
					int water = 0;
					for (int dy = -1; dy <= 1; ++dy)
						for (int dx = -1; dx <= 1; ++dx)
						{
							const int xx = x + dx, yy = y + dy;
							if (xx >= 0 && yy >= 0 && xx < TownMap::kW && yy < TownMap::kH && TownMap::kSurface[yy][xx] == '~') ++water;
						}
					if (water >= 6)
						g_water.push_back(Vector2{ static_cast<float>(x + TownMap::kX0), static_cast<float>(y + TownMap::kY0) });
				}
		}

		// ── 비둘기 ───────────────────────────────────────────────────────────
		void Startle(Flock& f, Vector2 from, bool leave)
		{
			const Spot& s = g_spots[static_cast<size_t>(f.spot)];
			f.st = leave ? FlockState::Leaving : FlockState::Flying;
			f.t = 0.f;
			f.dur = RandF(4.5f, 7.f);
			f.scarer = from;
			const Vector2 away = Norm(Sub(s.c, from));
			f.center = Add(s.c, Vector2{ away.x * 1.5f + RandF(-1.f, 1.f), away.y * 1.5f + RandF(-1.f, 1.f) });
			f.radius = RandF(3.2f, 4.6f);
			f.omega = (RandF() < 0.5f ? -1.f : 1.f) * RandF(0.75f, 1.0f);
			f.phase0 = std::atan2(away.y, away.x);
			f.alt = s.groundZ + RandF(3.1f, 3.9f);
			f.awayDir = Norm(Vector2{ RandF(-1.f, 1.f), RandF(-1.f, -0.2f) });   // 둥지는 대충 북쪽(화면 위)
			// 가까운 새부터 차례로
			std::vector<std::pair<float, size_t>> order;
			for (size_t i = 0; i < f.birds.size(); ++i) order.push_back({ Dist(f.birds[i].p, from), i });
			std::sort(order.begin(), order.end());
			for (size_t k = 0; k < order.size(); ++k)
			{
				Bird& b = f.birds[order[k].second];
				if (b.st != BirdState::Ground) continue;
				b.takeoff = leave ? RandF(0.f, 1.2f) : 0.02f + 0.07f * static_cast<float>(k) + RandF(0.f, 0.05f);
				b.peck = -1.f;
				// 날아오르기 전 한 걸음 물러난다
				const Vector2 dir = Norm(Sub(b.p, from));
				b.walkTo = Add(b.p, Mul(dir, 0.25f));
				b.walking = !WorldData::IsBlocked(static_cast<int>(std::lround(b.walkTo.x)), static_cast<int>(std::lround(b.walkTo.y)));
			}
			++g_stats.startles;
			++f.startles;
			if (!leave)
			{
				PlayAt(sfx::Id::Flutter, s.c, 0.8f, 15.f);
				// 깃털 몇 장 — 날아오른 자리에서 천천히 바람을 타고 떨어진다
				const int feathers = util::RandI(2, 4);
				for (int i = 0; i < feathers; ++i)
				{
					const Bird& b = f.birds[static_cast<size_t>(util::RandI(0, static_cast<int>(f.birds.size()) - 1))];
					fx::Particle& p = fx::Particles().Emit();
					p.pos = gfx::IsoPx(b.p.x, b.p.y);
					p.z = (b.z - s.groundZ) * gfx::kUnitZ + RandF(14.f, 30.f);
					const Vector2 wd = wind::Dir();
					p.vel = Vector2{ wd.x * RandF(8.f, 18.f) + RandF(-10.f, 10.f), wd.y * RandF(4.f, 9.f) };
					p.vz = RandF(-16.f, -7.f);
					p.life = p.maxLife = RandF(2.6f, 3.8f);
					p.size0 = p.size1 = RandF(4.5f, 6.5f);
					p.c0 = p.c1 = RandF() < 0.6f ? Rgb(0xE6E8EA, 235) : Rgb(0x9AA2AE, 235);
					p.rot = RandF(0.f, 6.f);
					p.vrot = RandF(-2.5f, 2.5f);
					p.wobble = 22.f;
					p.shape = fx::Shape::Leaf;
				}
			}
		}

		void BeginLanding(Flock& f, int spotIndex, bool fromAway)
		{
			f.spot = spotIndex;
			f.st = FlockState::Landing;
			f.t = 0.f;
			const Spot& s = g_spots[static_cast<size_t>(spotIndex)];
			// 새마다 앉을 칸 하나(겹치지 않게 흩는다)
			std::vector<Vector2> pool = s.cells;
			for (auto& b : f.birds)
			{
				if (pool.empty()) pool = s.cells;
				const size_t k = static_cast<size_t>(util::RandI(0, static_cast<int>(pool.size()) - 1));
				b.land = Vector2{ pool[k].x + RandF(-0.3f, 0.3f), pool[k].y + RandF(-0.3f, 0.3f) };
				b.landZ = s.groundZ;
				pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(k));
				if (fromAway)
				{
					// 먼 하늘(북쪽 위)에서 들어온다 — 거리가 조금씩 달라 한 마리씩 내려앉는다
					b.p = Add(s.c, Vector2{ RandF(-14.f, -7.f), RandF(-16.f, -9.f) });
					b.z = s.groundZ + RandF(5.f, 6.5f);
					b.v = Mul(Norm(Sub(s.c, b.p)), 3.f);
					b.vz = -0.5f;
				}
				b.st = BirdState::Air;
				b.takeoff = -1.f;
			}
		}

		void UpdateGroundBird(Bird& b, const Spot& s, float dt)
		{
			b.hop = std::max(0.f, b.hop - dt * 4.f);
			if (b.peck >= 0.f)
			{
				b.peck += dt / 0.22f;
				if (b.peck >= 1.f) b.peck = -1.f;
			}
			if (b.walking)
			{
				const Vector2 d = Sub(b.walkTo, b.p);
				const float l = std::hypot(d.x, d.y);
				const float step = 0.85f * dt;
				if (l <= step) { b.p = b.walkTo; b.walking = false; }
				else
				{
					b.p = Add(b.p, Mul(d, step / l));
					const float sx = (d.x - d.y);
					if (std::fabs(sx) > 0.01f) b.face = sx > 0.f ? 1.f : -1.f;
				}
			}
			b.next -= dt;
			if (b.next > 0.f) return;
			const float r = RandF();
			if (r < 0.55f)
			{
				b.peck = 0.f;                                    // 쪼기 한 번 — 몇 번 이어서
				b.next = RandF(0.25f, 0.6f);
			}
			else if (r < 0.88f)
			{
				// 종종걸음: 가까운 자리로(너무 멀어지면 가운데 쪽으로)
				Vector2 to = Add(b.p, Vector2{ RandF(-0.7f, 0.7f), RandF(-0.7f, 0.7f) });
				if (Dist(to, s.c) > 3.2f) to = Add(b.p, Mul(Norm(Sub(s.c, b.p)), 0.6f));
				const int tx = static_cast<int>(std::lround(to.x)), ty = static_cast<int>(std::lround(to.y));
				if (!WorldData::IsBlocked(tx, ty) && WorldData::TownLevel(tx, ty) == static_cast<int>(std::lround(s.groundZ / hgt::kLevelZ)))
				{
					b.walkTo = to;
					b.walking = true;
				}
				b.next = RandF(0.6f, 1.4f);
			}
			else
				b.next = RandF(0.8f, 2.2f);                    // 가만히 둘러본다
		}

		// 공중: 목표(자리 · 높이)로 끌려간다. arrive = 가까우면 느려진다
		void Steer(Bird& b, Vector2 target, float targetZ, float maxSpeed, float accel, bool arrive, float dt)
		{
			const Vector2 d = Sub(target, b.p);
			const float l = std::hypot(d.x, d.y);
			float want = maxSpeed;
			if (arrive) want = std::min(maxSpeed, l * 1.7f + 0.15f);
			const Vector2 dv = Sub(l > 1e-4f ? Mul(d, want / l) : Vector2{}, b.v);
			const float k = std::min(1.f, accel * dt);
			b.v = Add(b.v, Mul(dv, k));
			const float wantVz = std::clamp((targetZ - b.z) * 1.8f, -2.2f, 2.6f);
			b.vz += (wantVz - b.vz) * std::min(1.f, 5.f * dt);
			b.p = Add(b.p, Mul(b.v, dt));
			b.z += b.vz * dt;
		}

		void FlapModel(Bird& b, bool climbing, float dt)
		{
			// 오를 때 · 느릴 때는 빠르게 치고, 높이에서는 활공과 날갯짓을 번갈아
			b.glideT -= dt;
			if (climbing || b.vz > 0.5f) b.flapRate = 11.f;
			else if (b.glideT <= 0.f)
			{
				b.flapRate = b.flapRate > 0.f ? 0.f : 8.f;
				b.glideT = b.flapRate > 0.f ? RandF(0.35f, 0.6f) : RandF(0.5f, 1.1f);
			}
			if (b.flapRate > 0.f) b.flap += dt * b.flapRate * util::kPi * 2.f;
		}

		void UpdateFlock(Flock& f, const std::vector<Mover>& movers, float dt)
		{
			const Spot& s = g_spots[static_cast<size_t>(f.spot)];
			f.t += dt;
			f.calm = std::max(0.f, f.calm - dt);
			const bool night = g_night > 0.66f;

			switch (f.st)
			{
			case FlockState::Grounded:
			{
				if (night) { Startle(f, Add(s.c, Vector2{ 0.f, 3.f }), true); break; }
				for (auto& b : f.birds) UpdateGroundBird(b, s, dt);
				// 달려오는 플레이어 · 몬스터(칸/초 1.6 이상)나 움직이는 짐승이 2칸 안(짐승 2.4칸)에 들어오면 떼 전체가 날아오른다.
				// 천천히 걷는 주민 · 서 있는 사람에게는 가까운 새만 몇 걸음 비켜 준다(광장이 늘 날개 소리로 시끄럽지 않게)
				for (const auto& m : movers)
				{
					if (Dist(m.tile, s.c) > 6.f) continue;
					const bool animal = m.kind == MoverKind::Animal;
					const bool alarming = animal ? m.speed > 0.3f : m.speed >= 1.6f;
					const float reach = alarming ? (animal ? 2.4f : 2.0f) : 1.1f;
					bool near = false;
					for (auto& b : f.birds)
					{
						const float d = Dist(m.tile, b.p);
						if (d >= reach) continue;
						near = true;
						if (!alarming && !b.walking)
						{
							// 비켜 걷기: 사람 반대쪽으로 0.6칸
							const Vector2 away = Norm(Sub(b.p, m.tile));
							const Vector2 to = Add(b.p, Mul(away, 0.6f));
							const int tx = static_cast<int>(std::lround(to.x)), ty = static_cast<int>(std::lround(to.y));
							if (!WorldData::IsBlocked(tx, ty)) { b.walkTo = to; b.walking = true; b.peck = -1.f; b.next = RandF(0.8f, 1.6f); }
						}
					}
					if (near && alarming && f.calm <= 0.f) { Startle(f, m.tile, false); break; }
				}
				break;
			}
			case FlockState::Flying:
			case FlockState::Leaving:
			{
				const bool leaving = f.st == FlockState::Leaving;
				int idx = 0;
				for (auto& b : f.birds)
				{
					const int i = idx++;
					if (b.st == BirdState::Away) continue;
					if (b.st == BirdState::Ground)
					{
						if (b.takeoff < 0.f) b.takeoff = 0.f;
						b.takeoff -= dt;
						UpdateGroundBird(b, s, dt);
						if (b.takeoff <= 0.f)
						{
							b.st = BirdState::Air;
							b.takeoff = -1.f;
							b.walking = false;
							b.vz = RandF(2.4f, 3.0f);                // 푸드덕 — 곧게 솟는다
							const Vector2 dir = Norm(Sub(b.p, f.scarer));
							b.v = Mul(dir, RandF(1.6f, 2.4f));
							b.flapRate = 13.f;
							b.glideT = 0.6f;
						}
						continue;
					}
					if (leaving)
					{
						const Vector2 far = Add(s.c, Mul(f.awayDir, 30.f));
						Steer(b, far, s.groundZ + 7.f, 5.5f, 2.5f, false, dt);
						FlapModel(b, b.z < s.groundZ + 2.f, dt);
						if (Dist(b.p, s.c) > 22.f) b.st = BirdState::Away;
						continue;
					}
					// 원 위의 자기 자리(느슨한 줄) — 앞선 새를 따라 돈다
					const float ang = f.phase0 + f.omega * f.t - static_cast<float>(i) * 0.33f * (f.omega > 0.f ? 1.f : -1.f);
					const float rr = f.radius + 0.35f * std::sin(b.seed + f.t * 0.7f);
					const Vector2 target = Add(f.center, Vector2{ std::cos(ang) * rr, std::sin(ang) * rr });
					const float tz = f.alt + 0.35f * std::sin(b.seed * 1.3f + f.t * 1.1f);
					Steer(b, target, tz, 5.0f, 2.8f, false, dt);
					FlapModel(b, f.t < 0.9f, dt);
				}
				if (leaving)
				{
					bool all = true;
					for (const auto& b : f.birds) if (b.st != BirdState::Away) { all = false; break; }
					if (all) { f.st = FlockState::Away; f.t = 0.f; }
				}
				else if (f.t > f.dur)
				{
					// 플레이어 · 짐승이 아직 가까이 있으면 다른 자리로(가장 가까운 한가한 자리), 아니면 제자리로
					bool stillThere = false;
					for (const auto& m : movers)
						if (m.kind != MoverKind::Person && Dist(m.tile, s.c) < 3.6f) { stillThere = true; break; }
					int to = f.spot;
					if (stillThere)
					{
						float best = 1e9f;
						for (size_t k = 0; k < g_spots.size(); ++k)
						{
							if (static_cast<int>(k) == f.spot) continue;
							bool busy = false;
							for (const auto& m : movers)
								if (m.kind != MoverKind::Person && Dist(m.tile, g_spots[k].c) < 3.f) { busy = true; break; }
							const float d = Dist(g_spots[k].c, s.c) + (busy ? 100.f : 0.f);
							if (d < best) { best = d; to = static_cast<int>(k); }
						}
						// 다른 떼가 이미 차지한 자리라도 괜찮다(두 떼가 섞여 앉는다)
					}
					BeginLanding(f, to, false);
				}
				break;
			}
			case FlockState::Landing:
			{
				bool all = true;
				for (auto& b : f.birds)
				{
					if (b.st == BirdState::Ground) { UpdateGroundBird(b, s, dt); continue; }
					all = false;
					const float d = Dist(b.p, b.land);
					// 멀리서는 높이를 지키다가 가까워지면 미끄러져 내려온다
					const float tz = b.landZ + std::min(3.2f, d * 0.55f);
					Steer(b, b.land, tz, 4.2f, 2.6f, true, dt);
					FlapModel(b, false, dt);
					if (d < 1.2f) { b.flapRate = 12.f; b.flap += dt * 12.f * util::kPi * 2.f; }   // 내려앉기 직전 날개로 제동
					if (d < 0.12f && b.z - b.landZ < 0.06f)
					{
						const float sx = (b.v.x - b.v.y);   // 날아온 쪽을 보고 선다
						if (std::fabs(sx) > 0.01f) b.face = sx > 0.f ? 1.f : -1.f;
						b.st = BirdState::Ground;
						b.p = b.land;
						b.z = b.landZ;
						b.v = Vector2{};
						b.vz = 0.f;
						b.hop = 1.f;
						b.next = RandF(0.3f, 1.2f);
					}
				}
				if (all)
				{
					f.st = FlockState::Grounded;
					f.t = 0.f;
					f.calm = 2.5f;
					++g_stats.landings;
					++f.landings;
				}
				break;
			}
			case FlockState::Away:
				if (!night && f.t > 2.f)
					BeginLanding(f, f.spot, true);
				break;
			}
		}

		// ── 물고기 ───────────────────────────────────────────────────────────
		void SpawnSplash(Vector2 p, float strength, bool sound)
		{
			Ripple r;
			r.p = p;
			r.dur = 0.8f + 0.3f * strength;
			r.r1 = 10.f + 8.f * strength;
			r.strength = strength;
			g_ripples.push_back(r);
			const Vector2 iso = gfx::IsoPx(p.x, p.y);
			const int drops = 4 + static_cast<int>(strength * 3.f);
			for (int i = 0; i < drops; ++i)
			{
				fx::Particle& q = fx::Particles().Emit();
				q.pos = iso;
				q.z = 1.f;
				const float a = RandF(0.f, util::kPi * 2.f);
				const float sp = RandF(14.f, 34.f);
				q.vel = Vector2{ std::cos(a) * sp, std::sin(a) * sp * 0.5f };
				q.vz = RandF(55.f, 105.f) * (0.7f + 0.4f * strength);
				q.gravity = 420.f;
				q.life = q.maxLife = RandF(0.35f, 0.55f);
				q.size0 = RandF(2.f, 3.2f);
				q.size1 = 1.f;
				q.c0 = Rgb(0xE8F4F8, 230);
				q.c1 = Rgb(0xB8D8E8, 0);
				q.shape = fx::Shape::Soft;
			}
			if (sound) PlayAt(sfx::Id::FishSplash, p, 0.55f * (0.6f + 0.4f * strength), 11.f);
		}

		void UpdateFish(float dt, Vector2 view)
		{
			if (g_water.empty()) return;
			// 밤에 더 자주(어부 벨코). 보이는 곳(화면 가운데에서 15칸 안)의 물 칸에서만
			const float rate = util::Lerp(0.16f, 0.42f, util::Clamp01((g_night - 0.3f) / 0.5f));
			g_fishAcc += dt * rate;
			while (g_fishAcc >= 1.f)
			{
				g_fishAcc -= 1.f;
				for (int tries = 0; tries < 6; ++tries)
				{
					const Vector2 w = g_water[static_cast<size_t>(util::RandI(0, static_cast<int>(g_water.size()) - 1))];
					if (Dist(w, view) > 15.f) continue;
					Fish f;
					f.a = Vector2{ w.x + RandF(-0.3f, 0.3f), w.y + RandF(-0.3f, 0.3f) };
					// 개울을 따라(가까운 물 칸 쪽으로) 뛴다
					Vector2 dir{ RandF(-1.f, 1.f), RandF(-1.f, 1.f) };
					for (int k = 0; k < 8; ++k)
					{
						const Vector2 cand{ RandF(-1.f, 1.f), RandF(-1.f, 1.f) };
						const int tx = static_cast<int>(std::lround(w.x + cand.x * 1.2f)), ty = static_cast<int>(std::lround(w.y + cand.y * 1.2f));
						if (WorldData::IsInTown(tx, ty) && WorldData::TownCell(tx, ty) == '~') { dir = cand; break; }
					}
					dir = Norm(dir);
					const float dist = RandF(0.55f, 1.0f);
					f.b = Add(f.a, Mul(dir, dist));
					f.waterZ = hgt::GroundZ(w) - 0.08f;
					f.dur = RandF(0.55f, 0.8f);
					f.h = RandF(0.5f, 0.85f);
					f.size = RandF(0.8f, 1.15f);
					g_fish.push_back(f);
					SpawnSplash(f.a, 0.6f, false);
					++g_stats.fishJumps;
					break;
				}
			}
			for (auto& f : g_fish)
			{
				f.t += dt;
				if (!f.splashed && f.t >= f.dur)
				{
					f.splashed = true;
					SpawnSplash(f.b, 1.f, true);
				}
			}
			g_fish.erase(std::remove_if(g_fish.begin(), g_fish.end(), [](const Fish& f) { return f.t > f.dur; }), g_fish.end());
		}

		// ── 그리기 ───────────────────────────────────────────────────────────
		// 비둘기 크기: 실제 비율(사람 키의 1/6)보다 30% 크게 — 광장 먼 줌에서도 무엇인지 읽히게(게임다운 과장)
		constexpr float kS = 1.3f;

		struct Palette { Color body, wing, head, neck, bar; };
		Palette PaletteOf(uint8_t look)
		{
			switch (look)
			{
			case 1:  return { Rgb(0xDCDDDA), Rgb(0xC8CAC8), Rgb(0xE8E8E4), Rgb(0xB8C8C0), Rgb(0x9A9C9C) };
			case 2:  return { Rgb(0x9A8C80), Rgb(0x7A5E48), Rgb(0x8E8A86), Rgb(0x5A7A70), Rgb(0x4A3A2E) };
			case 3:  return { Rgb(0x5E646E), Rgb(0x4A5058), Rgb(0x565C66), Rgb(0x3E6A62), Rgb(0x2A2E34) };
			default: return { Rgb(0x8C95A0), Rgb(0x747E8A), Rgb(0x7E8690), Rgb(0x4E7E72), Rgb(0x3A3E48) };
			}
		}

		Color Night(Color c)
		{
			// 밤에는 푸르게 가라앉는다(월드 조명이 따로 어둡게 하지만 작은 새는 너무 밝아 보였다)
			const float k = 1.f - 0.35f * util::Clamp01((g_night - 0.3f) / 0.6f);
			return Color{ static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k), static_cast<unsigned char>(c.b * (k + 0.08f * (1.f - k))), c.a };
		}

		// 기울어진 타원(화면 px) — 날아가는 몸통 · 물고기
		void OrientedEllipse(Vector2 c, Vector2 u, float a, float b, Color col)
		{
			Vector2 pts[12];
			const Vector2 n{ -u.y, u.x };
			for (int i = 0; i < 12; ++i)
			{
				const float t = static_cast<float>(i) / 12.f * util::kPi * 2.f;
				const float ca = std::cos(t) * a, sb = std::sin(t) * b;
				pts[i] = Vector2{ c.x + u.x * ca + n.x * sb, c.y + u.y * ca + n.y * sb };
			}
			gfx::Poly(pts, 12, col);
		}

		void DrawGroundBird(const Bird& b)
		{
			const Palette pal = PaletteOf(b.look);
			const Vector2 base = gfx::IsoPx(b.p.x, b.p.y);
			const float f = b.face;
			// 걸을 때 고개를 앞뒤로(비둘기 걸음) · 쪼기는 머리가 땅까지
			float hx = 0.f, hy = 0.f;
			if (b.walking)
			{
				const float ph = std::sin(b.seed * 3.f + static_cast<float>(GetTime()) * 11.f);
				hx = ph * 1.3f;
			}
			if (b.peck >= 0.f)
			{
				const float k = std::sin(b.peck * util::kPi);
				hx += 1.6f * k;
				hy += 4.6f * k;
			}
			const float hop = std::sin(b.hop * util::kPi) * 2.5f;
			const float bodyY = -4.2f - hop;
			const float tilt = (b.peck >= 0.f ? std::sin(b.peck * util::kPi) * 1.2f : 0.f);
			auto P = [&](float x, float y) { return Vector2{ base.x + x * f * kS, base.y + y * kS }; };
			// 다리
			gfx::Line(P(0.4f, -1.4f - hop), P(0.2f, 0.f - hop * 0.3f), 0.8f * kS, Night(Rgb(0xC07068)));
			gfx::Line(P(1.8f, -1.4f - hop), P(2.1f, 0.f - hop * 0.3f), 0.8f * kS, Night(Rgb(0xC07068)));
			// 꼬리
			gfx::Tri(P(-4.2f, bodyY - 0.6f - tilt), P(-8.4f, bodyY - 1.4f - tilt * 1.6f), P(-8.0f, bodyY + 0.9f - tilt), Night(util::Shade(pal.wing, 0.8f)));
			// 몸통 · 가슴
			gfx::Ellipse(P(0.f, bodyY), 5.2f * kS, 3.4f * kS, Night(pal.body));
			gfx::Ellipse(P(2.3f, bodyY - 0.5f + tilt * 0.4f), 3.1f * kS, 2.7f * kS, Night(util::Shade(pal.body, 1.12f)));
			// 날개 + 띠 둘
			gfx::Ellipse(P(-1.3f, bodyY - 0.7f), 4.0f * kS, 2.1f * kS, Night(pal.wing));
			gfx::Line(P(-3.0f, bodyY - 0.2f), P(-0.8f, bodyY - 0.7f), 0.7f * kS, Night(pal.bar));
			gfx::Line(P(-3.4f, bodyY + 0.7f), P(-1.2f, bodyY + 0.2f), 0.7f * kS, Night(pal.bar));
			// 목(무지갯빛) · 머리 · 부리 · 눈
			gfx::Ellipse(P(3.5f + hx * 0.5f, bodyY - 2.1f + hy * 0.5f), 1.9f * kS, 1.8f * kS, Night(pal.neck));
			const Vector2 head = P(4.5f + hx, bodyY - 3.5f + hy);
			gfx::Circle(head, 2.05f * kS, Night(pal.head));
			gfx::Tri(P(6.1f + hx, bodyY - 3.6f + hy), P(8.0f + hx, bodyY - 3.1f + hy), P(6.1f + hx, bodyY - 2.8f + hy), Night(Rgb(0xD0C4B0)));
			gfx::Circle(P(5.1f + hx, bodyY - 4.0f + hy), 0.55f * kS, Night(Rgb(0xB0502A)));
			// 등의 빛(왼쪽 위)
			gfx::Ellipse(P(-0.6f, bodyY - 2.0f), 2.2f * kS, 0.7f * kS, util::MulA(Rgb(0xFFFFFF), 0.18f));
		}

		void DrawAirBird(const Bird& b, float groundZUnder)
		{
			(void)groundZUnder;
			const Palette pal = PaletteOf(b.look);
			const Vector2 c = gfx::IsoPx(b.p.x, b.p.y);
			// 화면 속 진행 방향(바닥 속도 + 오르내림)
			Vector2 u{ (b.v.x - b.v.y) * 32.f, (b.v.x + b.v.y) * 16.f - b.vz * 36.f * 0.5f };
			u = Norm(u);
			// 날개: 바닥 평면에서 진행 방향에 수직, 날갯짓만큼 들린다
			const Vector2 h = Norm(b.v);
			const Vector2 perp{ -h.y, h.x };
			const float flapA = b.flapRate > 0.f ? std::sin(b.flap) * 0.95f : 0.12f + 0.05f * std::sin(b.seed + static_cast<float>(GetTime()) * 2.f);
			const float span = 0.30f * kS;
			auto wingTip = [&](float side)
			{
				const float ca = std::cos(flapA), sa = std::sin(flapA);
				const float dx = perp.x * span * ca * side, dy = perp.y * span * ca * side;
				return Vector2{ c.x + (dx - dy) * 32.f, c.y + (dx + dy) * 16.f - sa * span * 36.f * 1.1f };
			};
			const Vector2 rootF{ c.x + u.x * 1.8f * kS, c.y + u.y * 1.8f * kS };
			const Vector2 rootB{ c.x - u.x * 1.9f * kS, c.y - u.y * 1.9f * kS };
			const Color wingUp = Night(pal.wing), wingDown = Night(util::Shade(pal.body, 1.15f));
			const Color wc = flapA > 0.2f ? wingDown : wingUp;
			// 먼 날개(위로 비치는 쪽) 먼저 — 몸통 뒤
			const Vector2 tipL = wingTip(-1.f), tipR = wingTip(1.f);
			const bool leftFar = tipL.y < tipR.y;
			const Vector2 far = leftFar ? tipL : tipR, nearT = leftFar ? tipR : tipL;
			gfx::Tri(rootF, rootB, far, util::Shade(wc, 0.85f));
			// 꼬리 · 몸 · 머리
			const Vector2 tail{ c.x - u.x * 5.2f * kS, c.y - u.y * 5.2f * kS };
			const Vector2 tn{ -u.y * 2.1f * kS, u.x * 2.1f * kS };
			gfx::Tri(Vector2{ c.x - u.x * 3.2f * kS, c.y - u.y * 3.2f * kS }, Vector2{ tail.x + tn.x, tail.y + tn.y }, Vector2{ tail.x - tn.x, tail.y - tn.y }, Night(util::Shade(pal.wing, 0.8f)));
			OrientedEllipse(c, u, 4.8f * kS, 2.3f * kS, Night(pal.body));
			gfx::Circle(Vector2{ c.x + u.x * 4.6f * kS, c.y + u.y * 4.6f * kS - 0.6f }, 1.9f * kS, Night(pal.head));
			gfx::Circle(Vector2{ c.x + u.x * 3.4f * kS, c.y + u.y * 3.4f * kS - 0.2f }, 1.5f * kS, Night(pal.neck));
			// 가까운 날개
			gfx::Tri(rootF, rootB, nearT, wc);
			gfx::Line(Vector2{ (rootF.x + rootB.x) * 0.5f, (rootF.y + rootB.y) * 0.5f }, nearT, 0.6f, util::MulA(Night(pal.bar), 0.7f));
		}

		void DrawFish(const Fish& f)
		{
			const float u = util::Clamp01(f.t / f.dur);
			const Vector2 p{ util::Lerp(f.a.x, f.b.x, u), util::Lerp(f.a.y, f.b.y, u) };
			const float z = f.waterZ + 4.f * f.h * u * (1.f - u);
			const float dzdu = 4.f * f.h * (1.f - 2.f * u);
			const Vector2 iso = gfx::IsoPx(p.x, p.y);
			const Vector2 d{ (f.b.x - f.a.x - (f.b.y - f.a.y)) * 32.f, (f.b.x - f.a.x + f.b.y - f.a.y) * 16.f - dzdu * 36.f };
			const Vector2 dir = Norm(d);
			gfx::SetLift(z * gfx::kUnitZ);
			gfx::SetDepth(p.x + p.y + 0.35f);
			const float s = f.size;
			const Vector2 tail{ iso.x - dir.x * 6.5f * s, iso.y - dir.y * 6.5f * s };
			const Vector2 n{ -dir.y * 2.6f * s, dir.x * 2.6f * s };
			const float wag = std::sin(f.t * 30.f) * 0.8f;
			gfx::Tri(Vector2{ iso.x - dir.x * 3.8f * s, iso.y - dir.y * 3.8f * s }, Vector2{ tail.x + n.x + wag, tail.y + n.y }, Vector2{ tail.x - n.x + wag, tail.y - n.y }, Night(Rgb(0x6A7A80)));
			OrientedEllipse(iso, dir, 4.6f * s, 1.7f * s, Night(Rgb(0xB6C2C6)));
			OrientedEllipse(Vector2{ iso.x - dir.y * -0.7f, iso.y + dir.x * -0.7f }, dir, 3.8f * s, 0.8f * s, Night(Rgb(0x5A6C74)));
			// 물기 반짝임(꼭대기에서 가장 밝게)
			const float glint = std::sin(u * util::kPi);
			gfx::Circle(Vector2{ iso.x + dir.x * 1.5f, iso.y + dir.y * 1.5f - 0.8f }, 0.9f, util::MulA(Rgb(0xFFFFFF), 0.5f + 0.4f * glint));
		}
	}

	void Init()
	{
		if (g_inited) return;
		g_inited = true;
		BuildSpots();
		BuildWater();
		g_stats.flocks = static_cast<int>(g_flocks.size());
	}

	void Update(float dt, const std::vector<Mover>& movers, Vector2 listener, Vector2 view, float night)
	{
		if (!g_inited) Init();
		g_night = night;
		g_listener = listener;
		g_acc += std::min(dt, 0.25f);
		const float step = 1.f / 60.f;
		while (g_acc >= step)
		{
			g_acc -= step;
			for (auto& f : g_flocks) UpdateFlock(f, movers, step);
			UpdateFish(step, view);
			for (auto& r : g_ripples) r.t += step;
			g_ripples.erase(std::remove_if(g_ripples.begin(), g_ripples.end(), [](const Ripple& r) { return r.t > r.dur; }), g_ripples.end());
		}
		int birds = 0, flying = 0;
		for (const auto& f : g_flocks)
			for (const auto& b : f.birds)
			{
				if (b.st == BirdState::Away) continue;
				++birds;
				if (b.st == BirdState::Air) ++flying;
			}
		g_stats.birds = birds;
		g_stats.flying = flying;
	}

	void DrawGround()
	{
		// 나는 새의 그림자 — 높을수록 옅고 작다(바로 아래, 빛은 거의 머리 위)
		for (const auto& f : g_flocks)
			for (const auto& b : f.birds)
			{
				if (b.st == BirdState::Away) continue;
				const float gz = hgt::GroundZ(b.p);
				const float h = std::max(0.f, b.z - gz);
				const Vector2 c = gfx::IsoPx(b.p.x, b.p.y);
				const float k = util::Clamp01(1.f - h / 7.f);
				const float a = (b.st == BirdState::Ground ? 70.f : 55.f * (0.3f + 0.7f * k)) * (1.f - 0.4f * util::Clamp01(g_night));
				const float rx = (b.st == BirdState::Ground ? 4.6f : 4.f + 2.f * (1.f - k)) * kS;
				gfx::Ellipse(c, rx, rx * 0.5f, Color{ 10, 14, 22, static_cast<unsigned char>(a) });
			}
		// 물 고리 — 두 겹이 벌어지며 옅어진다
		for (const auto& r : g_ripples)
		{
			const float u = util::Clamp01(r.t / r.dur);
			const Vector2 c = gfx::IsoPx(r.p.x, r.p.y);
			for (int k = 0; k < 2; ++k)
			{
				const float uk = util::Clamp01(u * 1.15f - k * 0.22f);
				if (uk <= 0.f) continue;
				const float rad = util::Lerp(r.r0, r.r1 * (1.f - 0.25f * k), util::EaseOutCubic(uk));
				const float a = (1.f - uk) * (k == 0 ? 170.f : 110.f) * r.strength;
				gfx::EllipseArc(c, rad, rad * 0.5f, 0.f, util::kPi * 2.f, 1.3f, Color{ 214, 236, 244, static_cast<unsigned char>(a) });
			}
		}
	}

	void DrawWorld()
	{
		for (const auto& f : g_flocks)
			for (const auto& b : f.birds)
			{
				if (b.st == BirdState::Away) continue;
				gfx::ResetMod();
				const float gz = hgt::GroundZ(b.p);
				gfx::SetLift(b.z * gfx::kUnitZ);
				// 높은 곳(층)의 칸 윗면과 겹치지 않게 NPC처럼 조금 앞으로, 공중이면 더
				gfx::SetDepth(b.p.x + b.p.y + (b.st == BirdState::Ground ? (gz > 0.f ? 0.3f : 0.02f) : 0.35f));
				if (b.st == BirdState::Ground) DrawGroundBird(b);
				else DrawAirBird(b, gz);
			}
		for (const auto& fish : g_fish) DrawFish(fish);
		gfx::SetLift(0.f);
		gfx::ResetMod();
	}

	Stats GetStats() { return g_stats; }

	int NearestFlock(Vector2 tile)
	{
		int best = -1;
		float bd = 1e9f;
		for (size_t i = 0; i < g_flocks.size(); ++i)
		{
			const auto& f = g_flocks[i];
			if (f.st != FlockState::Grounded) continue;
			const float d = Dist(g_spots[static_cast<size_t>(f.spot)].c, tile);
			if (d < bd) { bd = d; best = static_cast<int>(i); }
		}
		return best;
	}

	FlockInfo Flock(int index)
	{
		FlockInfo info;
		if (index < 0 || index >= static_cast<int>(g_flocks.size())) return info;
		const auto& f = g_flocks[static_cast<size_t>(index)];
		info.center = g_spots[static_cast<size_t>(f.spot)].c;
		info.state = static_cast<int>(f.st);
		info.startles = f.startles;
		info.landings = f.landings;
		for (const auto& b : f.birds) if (b.st == BirdState::Air) ++info.flying;
		return info;
	}
}
