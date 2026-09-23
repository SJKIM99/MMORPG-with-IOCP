using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using Godot;

namespace Client.World;

/// <summary>
/// world/manifest.json 과 world/regions/*.json 을 읽는다.
///
/// 이 파일들은 res:// 밖(저장소의 world/)에 있다. 리소스가 아니라 데이터이고,
/// 서버가 읽는 것과 같은 파일이어야 하기 때문이다 (CLAUDE.md 5장).
/// 에셋 .glb 만 res://assets/ 정션을 통해 임포트된다.
/// </summary>
public static class WorldData
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNameCaseInsensitive = true,
        ReadCommentHandling = JsonCommentHandling.Skip,
        AllowTrailingCommas = true,
    };

    /// <summary>저장소의 world/ 디렉터리. res:// 의 부모에 있다.</summary>
    public static string WorldRoot =>
        ProjectSettings.GlobalizePath("res://").PathJoin("../world").SimplifyPath();

    public static Manifest LoadManifest() => Read<Manifest>(WorldRoot.PathJoin("manifest.json"));

    /// <summary>
    /// 리전은 **바이너리 산출물**을 읽는다 (world/build/&lt;id&gt;.bin).
    ///
    /// JSON 을 버린 것이 아니다 — world/regions/*.json 은 여전히 소스이고
    /// git 이 diff 한다. 서버에는 JSON 파서가 없어서(넣으려면 새 라이브러리가
    /// 필요하고 그건 2장의 승인 대상) 서버·클라가 함께 읽을 수 있는 형식이
    /// 바이너리뿐이었다. 같은 파일을 읽으면 콜리전 불일치가 생길 자리가 없다.
    ///
    /// manifest 는 그대로 JSON 이다. 서버가 쓰지 않는 에셋 메타데이터라
    /// 바꿀 이유가 없다.
    /// </summary>
    public static Region LoadRegion(string id) =>
        RegionBinary.Load(WorldRoot.PathJoin("build").PathJoin($"{id}.bin"));

    private static T Read<T>(string absolutePath)
    {
        using FileAccess file = FileAccess.Open(absolutePath, FileAccess.ModeFlags.Read);
        if (file is null)
        {
            throw new System.IO.FileNotFoundException(
                $"월드 데이터를 열 수 없다: {absolutePath} ({FileAccess.GetOpenError()})");
        }

        string json = file.GetAsText();
        return JsonSerializer.Deserialize<T>(json, Options)
               ?? throw new JsonException($"비어 있는 JSON: {absolutePath}");
    }
}

// ---------------------------------------------------------------------------
// manifest.json
// ---------------------------------------------------------------------------

public sealed class Manifest
{
    [JsonPropertyName("sources")]
    public Dictionary<string, Source> Sources { get; set; } = new();

    [JsonPropertyName("assets")]
    public Dictionary<string, AssetEntry> Assets { get; set; } = new();

    /// <summary>에셋 ID 를 res:// 경로로 바꾼다.</summary>
    /// <remarks>
    /// manifest 의 root 는 "assets/vendor/kaykit_skeletons" 처럼 저장소 기준이지만,
    /// Godot 쪽에서는 Client/assets 정션을 거쳐 "res://assets/kaykit_skeletons" 가 된다.
    /// 그 한 단계만 갈아끼운다.
    /// </remarks>
    public string ResourcePath(string assetId)
    {
        AssetEntry entry = Assets[assetId];
        string root = Sources[entry.Source].Root;      // assets/vendor/<pack>
        string pack = root[(root.LastIndexOf('/') + 1)..];
        return $"res://assets/{pack}/{entry.File}";
    }
}

public sealed class Source
{
    [JsonPropertyName("root")] public string Root { get; set; } = "";
    [JsonPropertyName("pack")] public string Pack { get; set; } = "";
    [JsonPropertyName("license")] public string License { get; set; } = "";
}

public sealed class AssetEntry
{
    [JsonPropertyName("source")] public string Source { get; set; } = "";
    [JsonPropertyName("file")] public string File { get; set; } = "";
    [JsonPropertyName("kind")] public string Kind { get; set; } = "";
    [JsonPropertyName("collision")] public string Collision { get; set; } = "none";
    [JsonPropertyName("nav")] public string Nav { get; set; } = "include";
    [JsonPropertyName("scale")] public float Scale { get; set; } = 1.0f;
    [JsonPropertyName("animation")] public string? Animation { get; set; }
    [JsonPropertyName("collider")] public ColliderSpec? Collider { get; set; }

    /// <summary>
    /// 굴뚝 위치들. 모델 로컬 좌표이고 **배율 적용 전**이다.
    /// <c>tools/find_chimneys.py</c> 가 메시에서 찾아 적는다 — 런타임에
    /// 메시를 뒤지지 않기 위함이다(CLAUDE.md 5장).
    /// </summary>
    [JsonPropertyName("chimney")]
    public float[][] Chimney { get; set; } = System.Array.Empty<float[]>();
}

/// <summary>
/// 콜리전 모양이 메시 AABB 와 다른 에셋용. 값은 배율 적용 **전** 원본 단위다.
/// 나무가 대표적이다 — AABB 는 수관 전체라 그대로 쓰면 숲이 벽이 된다.
/// </summary>
public sealed class ColliderSpec
{
    [JsonPropertyName("shape")] public string Shape { get; set; } = "box";
    [JsonPropertyName("radius")] public float Radius { get; set; } = 0.5f;
}

// ---------------------------------------------------------------------------
// regions/*.json
// ---------------------------------------------------------------------------

public sealed class Region
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("display_name")] public string DisplayName { get; set; } = "";
    [JsonPropertyName("size")] public float[] Size { get; set; } = { 0, 0 };
    [JsonPropertyName("sector_size")] public float SectorSize { get; set; } = 32f;
    [JsonPropertyName("terrain")] public Terrain? Terrain { get; set; }
    [JsonPropertyName("ground")] public Ground Ground { get; set; } = new();
    [JsonPropertyName("water")] public WaterInfo? Water { get; set; }
    [JsonPropertyName("flags")] public RegionFlags Flags { get; set; } = new();
    [JsonPropertyName("spawn_point")] public float[] SpawnPoint { get; set; } = { 0, 0, 0 };
    [JsonPropertyName("placements")] public List<Placement> Placements { get; set; } = new();
    [JsonPropertyName("npcs")] public List<Npc> Npcs { get; set; } = new();
    [JsonPropertyName("spawns")] public List<SpawnGroup> Spawns { get; set; } = new();
    [JsonPropertyName("patrol_routes")] public List<PatrolRoute> PatrolRoutes { get; set; } = new();
}

public sealed class Ground
{
    [JsonPropertyName("y")] public float Y { get; set; }
    [JsonPropertyName("color")] public string Color { get; set; } = "#5d7c4a";

    /// <summary>포장된 곳의 색. terrain.surface 가 1 인 정점에 칠한다.</summary>
    [JsonPropertyName("road_color")] public string RoadColor { get; set; } = "#a8a49b";
}

/// <summary>
/// 수면의 색과 통행 규약.
///
/// <c>nav</c> 는 클라이언트가 쓰지 않는다 — **서버의 Recast 빌드**가 읽어
/// 내비메시에서 물을 빼는 값이다(CLAUDE.md 5장의 nav 필드와 같은 규약).
/// 같은 JSON 을 양쪽이 읽으므로 "클라에선 물, 서버에선 땅" 이 될 수 없다.
/// </summary>
public sealed class WaterInfo
{
    [JsonPropertyName("color")] public string Color { get; set; } = "#3d7a92";
    [JsonPropertyName("deep_color")] public string DeepColor { get; set; } = "#15414f";
    [JsonPropertyName("foam_color")] public string FoamColor { get; set; } = "#dff1f5";
    [JsonPropertyName("nav")] public string Nav { get; set; } = "exclude";
    [JsonPropertyName("cells")] public int Cells { get; set; }
}

/// <summary>
/// 높이맵. row-major 로 index = iz * resolution + ix 이고 값은 미터다.
/// tools/terrain.py 가 만들고 서버의 콜리전 빌드도 같은 배열을 읽는다.
/// </summary>
public sealed class Terrain
{
    [JsonPropertyName("cell")] public float Cell { get; set; } = 4f;
    [JsonPropertyName("resolution")] public int Resolution { get; set; }
    [JsonPropertyName("heights")] public float[] Heights { get; set; } = System.Array.Empty<float>();

    /// <summary>
    /// 포장 여부. 0 = 풀, 1 = 포석. heights 와 같은 배열 구조다.
    /// 도로를 타일 모델로 깔지 않고 **지면에 칠하기** 위한 마스크다.
    /// </summary>
    [JsonPropertyName("surface")] public float[] Surface { get; set; } = System.Array.Empty<float>();

    /// <summary>
    /// 수면 높이(m). <see cref="NoWater"/> 면 그 격자점에는 물이 없다.
    ///
    /// 지형 높이와 **따로** 두는 이유: 물은 수평면이고 지형은 아니다.
    /// 하나로 합치면 개울 바닥이 곧 수면이 되어 물이 지형을 따라 출렁인다.
    /// </summary>
    [JsonPropertyName("water")] public float[] Water { get; set; } = System.Array.Empty<float>();

    /// <summary>물 없음을 뜻하는 센티널. 표준 JSON 에 NaN 을 넣을 수 없어서 쓴다.</summary>
    [JsonPropertyName("no_water")] public float NoWater { get; set; } = -1000f;

    /// <summary>
    /// 지형 높이의 FNV-1a. 파이썬 기록기·C++ 리더와 **같은 값**이 나와야 한다.
    /// 세 구현이 어긋나는 순간 여기서 드러난다.
    /// </summary>
    public uint Hash { get; set; }
}

public sealed class RegionFlags
{
    [JsonPropertyName("pvp")] public bool Pvp { get; set; }
    [JsonPropertyName("spawn")] public bool Spawn { get; set; }
}

public sealed class Placement
{
    [JsonPropertyName("asset")] public string Asset { get; set; } = "";
    [JsonPropertyName("pos")] public float[] Pos { get; set; } = { 0, 0, 0 };
    [JsonPropertyName("yaw")] public float Yaw { get; set; }
    [JsonPropertyName("scale")] public float Scale { get; set; } = 1.0f;
    [JsonPropertyName("collision")] public string Collision { get; set; } = "none";
    [JsonPropertyName("nav")] public string Nav { get; set; } = "include";

    /// <summary>
    /// 이 집에 불이 지펴져 있는가. 배치 생성기가 절반만 켠다.
    ///
    /// manifest 의 chimney 는 "그 모델 어디에 굴뚝이 있나"(모델의 성질)이고,
    /// 이 값은 "그 집에 지금 불이 있나"(배치의 성질)다. 층위가 달라 파일도 다르다.
    /// </summary>
    [JsonPropertyName("smoke")] public bool Smoke { get; set; }
}

public sealed class Npc
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("asset")] public string Asset { get; set; } = "";
    [JsonPropertyName("pos")] public float[] Pos { get; set; } = { 0, 0, 0 };
    [JsonPropertyName("yaw")] public float Yaw { get; set; }
    [JsonPropertyName("role")] public string Role { get; set; } = "";

    /// <summary>역할별 대기 동작 클립 이름. 배치 생성기가 정한다.</summary>
    [JsonPropertyName("animation")] public string Animation { get; set; } = "Idle_A";

    /// <summary>가끔 섞어 재생할 동작들. 없으면 기본 동작만 반복한다.</summary>
    [JsonPropertyName("idle_variants")]
    public string[] IdleVariants { get; set; } = System.Array.Empty<string>();
}

public sealed class SpawnGroup
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("monster")] public string Monster { get; set; } = "";
    [JsonPropertyName("asset")] public string Asset { get; set; } = "";
    [JsonPropertyName("center")] public float[] Center { get; set; } = { 0, 0, 0 };
    [JsonPropertyName("radius")] public float Radius { get; set; }
    [JsonPropertyName("count")] public int Count { get; set; }

    /// <summary>리스폰 간격. 스폰 권한은 서버에 있어 클라는 표시에만 쓴다(9장).</summary>
    [JsonPropertyName("respawn_ms")] public int RespawnMs { get; set; }
}

public sealed class PatrolRoute
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("loop")] public bool Loop { get; set; }
    [JsonPropertyName("waypoints")] public List<float[]> Waypoints { get; set; } = new();
}
