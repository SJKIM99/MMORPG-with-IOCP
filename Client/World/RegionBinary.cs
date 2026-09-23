using System;
using System.Collections.Generic;
using System.Text;
using Godot;

namespace Client.World;

/// <summary>
/// world/build/&lt;region&gt;.bin 리더.
///
/// 포맷 정의는 <c>tools/region_format.py</c> 다. 그 파일이 원본이고 여기와
/// 서버의 <c>GameServer/World/RegionData.h</c> 는 옮겨 적은 것이다 —
/// 셋 중 하나가 어긋나면 헤더의 terrain_hash 대조에서 걸린다.
///
/// **서버와 클라가 같은 파일을 읽는다.** CLAUDE.md 5장의 "같은 JSON 을 읽는다"를
/// 한 단계 좁힌 것이다. 파서가 둘이면 "클라에선 벽, 서버에선 통과" 가 생길 수
/// 있지만, 같은 바이트를 같은 규칙으로 읽으면 그 자리가 없어진다.
///
/// JSON 을 버린 것이 아니다. world/regions/*.json 은 여전히 소스이고 git 이
/// diff 한다. 이 .bin 은 tools/build_region.py 가 만드는 산출물이다.
/// </summary>
public static class RegionBinary
{
    private const uint Magic = 0x314E4752;      // "RGN1" 리틀엔디안
    private const uint FormatVersion = 1;
    private const int HeaderBytes = 64;
    private const int SectionEntryBytes = 16;

    /// <summary>tools/region_format.py 의 PLACEMENT_STRIDE 와 같아야 한다.</summary>
    private const int PlacementStride = 28;

    private const byte FlagSmoke = 1 << 0;

    private static readonly string[] CollisionNames = { "none", "static_walkable", "static_blocker" };
    private static readonly string[] NavNames = { "include", "exclude", "area:water" };

    public static Region Load(string absolutePath)
    {
        using FileAccess file = FileAccess.Open(absolutePath, FileAccess.ModeFlags.Read);
        if (file is null)
        {
            throw new System.IO.FileNotFoundException(
                $"리전 바이너리를 열 수 없다: {absolutePath} ({FileAccess.GetOpenError()})\n"
                + "tools/build_region.py 를 먼저 돌려라");
        }

        byte[] raw = file.GetBuffer((long)file.GetLength());
        return Parse(raw, absolutePath);
    }

    private static Region Parse(byte[] raw, string where)
    {
        if (raw.Length < HeaderBytes)
        {
            throw new InvalidOperationException($"{where}: 파일이 헤더보다 짧다");
        }

        var r = new Cursor(raw);
        uint magic = r.U32();
        if (magic != Magic)
        {
            throw new InvalidOperationException(
                $"{where}: magic 이 RGN1 이 아니다 (0x{magic:x8}) — 손상됐거나 다른 파일이다");
        }

        uint version = r.U32();
        if (version != FormatVersion)
        {
            throw new InvalidOperationException(
                $"{where}: 포맷 버전 {version}, 클라이언트는 {FormatVersion} "
                + "— tools/build_region.py 를 다시 돌려라");
        }

        int sectionCount = (int)r.U32();
        uint terrainHash = r.U32();

        var region = new Region();
        float sizeX = r.F32();
        float sizeZ = r.F32();
        region.Size = new[] { sizeX, sizeZ };
        region.SectorSize = r.F32();
        region.SpawnPoint = new[] { r.F32(), r.F32(), r.F32() };
        r.F32();                            // y_min — 클라는 쓰지 않는다
        r.F32();                            // y_max
        r.U32();                            // seed
        bool pvp = r.U8() != 0;
        bool spawnAllowed = r.U8() != 0;
        r.U8(); r.U8();                     // pad
        uint idStr = r.U32();
        uint displayStr = r.U32();
        region.Flags = new RegionFlags { Pvp = pvp, Spawn = spawnAllowed };

        if (r.Offset != HeaderBytes)
        {
            throw new InvalidOperationException(
                $"{where}: 헤더를 {r.Offset}바이트 읽었다 (기대 {HeaderBytes}) — 리더가 어긋났다");
        }

        // --- 섹션 표 --------------------------------------------------------
        var sections = new Dictionary<string, (int Offset, int Bytes, int Count)>();
        for (int i = 0; i < sectionCount; i++)
        {
            r.Seek(HeaderBytes + i * SectionEntryBytes);
            string tag = Encoding.ASCII.GetString(raw, r.Offset, 4);
            r.Seek(r.Offset + 4);
            int offset = (int)r.U32();
            int bytes = (int)r.U32();
            int count = (int)r.U32();
            if (offset + bytes > raw.Length)
            {
                throw new InvalidOperationException($"{where}: 섹션 {tag} 가 파일 밖을 가리킨다");
            }
            sections[tag] = (offset, bytes, count);
        }

        // --- 문자열 표 -------------------------------------------------------
        string[] strings = Array.Empty<string>();
        if (sections.TryGetValue("STRS", out var strs))
        {
            strings = new string[strs.Count];
            r.Seek(strs.Offset);
            for (int i = 0; i < strs.Count; i++)
            {
                int length = (int)r.U32();
                strings[i] = Encoding.UTF8.GetString(raw, r.Offset, length);
                // 항목마다 4바이트 정렬 패딩이 붙는다.
                r.Seek(r.Offset + length + ((4 - (length % 4)) % 4));
            }
        }
        string Str(uint index) => index < strings.Length ? strings[index] : "";

        region.Id = Str(idStr);
        region.DisplayName = Str(displayStr);

        // --- 지형 -----------------------------------------------------------
        if (!sections.TryGetValue("TERR", out var terr))
        {
            throw new InvalidOperationException($"{where}: TERR 섹션이 없다");
        }
        r.Seek(terr.Offset);
        var terrain = new Terrain
        {
            Resolution = (int)r.U32(),
            Cell = r.F32(),
        };
        terrain.Heights = r.Floats(terr.Count);

        // **읽은 것이 기록된 것과 같은지 스스로 확인한다.**
        // 섹션 오프셋을 한 칸만 잘못 읽어도 여기서 걸리고, 파이썬·C++ 과 같은
        // 값이 나와야 하므로 포맷이 어긋나면 셋 다 여기서 멈춘다.
        uint actual = Fnv1a(raw, terr.Offset + 8, terr.Count * 4);
        if (actual != terrainHash)
        {
            throw new InvalidOperationException(
                $"{where}: 지형 해시 불일치 — 파일 {terrainHash:x8}, 계산 {actual:x8}");
        }

        if (sections.TryGetValue("SURF", out var surf))
        {
            r.Seek(surf.Offset);
            terrain.Surface = r.Floats(surf.Count);
        }
        if (sections.TryGetValue("WATR", out var watr))
        {
            r.Seek(watr.Offset);
            terrain.NoWater = r.F32();
            terrain.Water = r.Floats(watr.Count);
        }
        terrain.Hash = actual;
        region.Terrain = terrain;

        // --- 색 (클라 전용 섹션) ------------------------------------------------
        //
        // 빈 문자열이면 **클래스 기본값을 그대로 둔다.** JSON 에 없던 키가
        // 기본값으로 남던 동작과 같게 맞춘 것이다 — 필드 리전에는 road_color 가
        // 없어서, 빈 문자열을 그대로 넣었더니 Color("") 가 던졌다.
        if (sections.TryGetValue("COLR", out var colr))
        {
            r.Seek(colr.Offset);
            var ground = new Ground();
            ground.Color = Or(Str(r.U32()), ground.Color);
            ground.RoadColor = Or(Str(r.U32()), ground.RoadColor);
            region.Ground = ground;

            var water = new WaterInfo();
            water.Color = Or(Str(r.U32()), water.Color);
            water.DeepColor = Or(Str(r.U32()), water.DeepColor);
            water.FoamColor = Or(Str(r.U32()), water.FoamColor);
            region.Water = water;
        }

        // 물 격자 수는 **저장하지 않고 센다.** 파생값을 파일에 넣으면 배열과
        // 어긋날 수 있고, 어긋났을 때 어느 쪽이 맞는지 알 방법이 없다.
        if (region.Water is not null && terrain.Water.Length > 0)
        {
            int cells = 0;
            foreach (float w in terrain.Water)
            {
                if (w > terrain.NoWater) cells++;
            }
            region.Water.Cells = cells;
        }

        // --- 배치 -----------------------------------------------------------
        if (sections.TryGetValue("PLAC", out var plac))
        {
            if (plac.Bytes != plac.Count * PlacementStride)
            {
                throw new InvalidOperationException(
                    $"{where}: 배치 섹션 크기가 스트라이드와 맞지 않는다 — 포맷이 바뀌었다");
            }
            region.Placements = new List<Placement>(plac.Count);
            r.Seek(plac.Offset);
            for (int i = 0; i < plac.Count; i++)
            {
                var p = new Placement
                {
                    Asset = Str(r.U32()),
                    Pos = new[] { r.F32(), r.F32(), r.F32() },
                    Yaw = r.F32(),
                    Scale = r.F32(),
                };
                p.Collision = Name(CollisionNames, r.U8());
                p.Nav = Name(NavNames, r.U8());
                byte flags = r.U8();
                r.U8();                                 // pad
                p.Smoke = (flags & FlagSmoke) != 0;
                region.Placements.Add(p);
            }
        }

        // --- NPC (가변 길이) --------------------------------------------------
        if (sections.TryGetValue("NPCS", out var npcs))
        {
            region.Npcs = new List<Npc>(npcs.Count);
            r.Seek(npcs.Offset);
            for (int i = 0; i < npcs.Count; i++)
            {
                var n = new Npc
                {
                    Id = Str(r.U32()),
                    Asset = Str(r.U32()),
                    Pos = new[] { r.F32(), r.F32(), r.F32() },
                    Yaw = r.F32(),
                    Role = Str(r.U32()),
                    Animation = Str(r.U32()),
                };
                int variants = (int)r.U32();
                var list = new string[variants];
                for (int v = 0; v < variants; v++)
                {
                    list[v] = Str(r.U32());
                }
                n.IdleVariants = list;
                region.Npcs.Add(n);
            }
        }

        // --- 몬스터 스폰 그룹 ---------------------------------------------------
        if (sections.TryGetValue("SPWN", out var spwn))
        {
            region.Spawns = new List<SpawnGroup>(spwn.Count);
            r.Seek(spwn.Offset);
            for (int i = 0; i < spwn.Count; i++)
            {
                region.Spawns.Add(new SpawnGroup
                {
                    Id = Str(r.U32()),
                    Monster = Str(r.U32()),
                    Asset = Str(r.U32()),
                    Center = new[] { r.F32(), r.F32(), r.F32() },
                    Radius = r.F32(),
                    Count = (int)r.U32(),
                    RespawnMs = (int)r.U32(),
                });
            }
        }

        // --- 순찰 경로 (가변 길이) ----------------------------------------------
        if (sections.TryGetValue("PATH", out var path))
        {
            region.PatrolRoutes = new List<PatrolRoute>(path.Count);
            r.Seek(path.Offset);
            for (int i = 0; i < path.Count; i++)
            {
                var route = new PatrolRoute { Id = Str(r.U32()), Loop = r.U32() != 0 };
                int points = (int)r.U32();
                route.Waypoints = new List<float[]>(points);
                for (int w = 0; w < points; w++)
                {
                    route.Waypoints.Add(new[] { r.F32(), r.F32(), r.F32() });
                }
                region.PatrolRoutes.Add(route);
            }
        }

        return region;
    }

    private static string Name(string[] table, byte index) =>
        index < table.Length ? table[index] : table[0];

    /// <summary>빈 문자열이면 기본값을 쓴다 — JSON 에 키가 없던 동작과 같게.</summary>
    private static string Or(string value, string fallback) =>
        string.IsNullOrEmpty(value) ? fallback : value;

    /// <summary>tools/region_format.py 의 fnv1a 와 같은 값을 내야 한다.</summary>
    private static uint Fnv1a(byte[] data, int offset, int length)
    {
        uint hash = 0x811C9DC5u;
        for (int i = 0; i < length; i++)
        {
            hash ^= data[offset + i];
            hash *= 0x01000193u;
        }
        return hash;
    }

    /// <summary>
    /// 경계를 검사하는 순차 리더.
    ///
    /// 잘린 파일이 <c>IndexOutOfRangeException</c> 으로 터지면 어디서 왜 끊겼는지
    /// 알 수 없다. 오프셋을 들고 있다가 메시지에 실어 준다.
    /// </summary>
    private sealed class Cursor
    {
        private readonly byte[] _data;

        public Cursor(byte[] data) => _data = data;

        public int Offset { get; private set; }

        public void Seek(int offset)
        {
            if (offset < 0 || offset > _data.Length)
            {
                throw new InvalidOperationException(
                    $"파일 밖으로 이동: {offset} / {_data.Length}");
            }
            Offset = offset;
        }

        public byte U8() => _data[Take(1)];
        public uint U32() => BitConverter.ToUInt32(_data, Take(4));
        public float F32() => BitConverter.ToSingle(_data, Take(4));

        public float[] Floats(int count)
        {
            int start = Take(count * 4);
            var values = new float[count];
            Buffer.BlockCopy(_data, start, values, 0, count * 4);
            return values;
        }

        private int Take(int bytes)
        {
            if (Offset + bytes > _data.Length)
            {
                throw new InvalidOperationException(
                    $"파일이 중간에 끊겼다: {Offset}+{bytes} > {_data.Length}");
            }
            int start = Offset;
            Offset += bytes;
            return start;
        }
    }
}
