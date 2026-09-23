using System.Collections.Generic;
using System.Diagnostics;
using Godot;

namespace Client.World;

/// <summary>
/// world/regions/&lt;id&gt;.json 을 읽어 씬을 만든다.
///
/// CLAUDE.md 5장 — 씬 파일에 손으로 배치하지 않는다. JSON 이 유일한 진실이고
/// 서버의 콜리전 빌드와 이 로더가 같은 파일을 읽는다.
///
/// 정적 콜리전도 같은 JSON 의 collision 필드에서 만든다.
/// 서버 내비메시(Week 2)도 같은 필드를 읽으므로 둘이 어긋날 수 없다.
/// </summary>
public partial class WorldLoader : Node3D
{
    [Export] public string RegionId { get; set; } = "town";

    /// <summary>스폰 지점과 순찰 경로를 색 마커로 그린다. 배치 확인용.</summary>
    [Export] public bool ShowDebugMarkers { get; set; } = true;

    /// <summary>나무·풀에 흔들림 셰이더를 입힌다.</summary>
    [Export] public bool WindEnabled { get; set; } = true;

    /// <summary>배치의 collision 필드대로 정적 콜리전을 만든다.</summary>
    [Export] public bool CollisionEnabled { get; set; } = true;

    private Manifest _manifest = null!;
    private readonly Dictionary<string, PackedScene> _cache = new();
    private int _swayingCount;
    private int _colliderCount;
    private int _spinningCount;
    private int _smokeCount;
    private int _waterCells;
    private int _waterHoles;

    public Region Region { get; private set; } = null!;

    /// <summary>로더가 실제로 붙인 개수. 자가검사가 이 값과 대조한다.</summary>
    public int SpinningCount => _spinningCount;
    public int SmokeCount => _smokeCount;
    public int WaterHoles => _waterHoles;

    /// <summary>플레이어 스폰 등에서 재사용한다. 파일을 두 번 읽지 않기 위함.</summary>
    public Manifest Manifest => _manifest;

    public override void _Ready()
    {
        var sw = Stopwatch.StartNew();

        _manifest = WorldData.LoadManifest();
        Region = WorldData.LoadRegion(RegionId);

        BuildGround();
        int placed = BuildPlacements();
        int npcs = BuildNpcs();
        int monsters = BuildSpawns();
        if (ShowDebugMarkers)
        {
            BuildPatrolRoutes();
        }

        sw.Stop();

        GD.Print(
            $"[world] {Region.Id} ({Region.DisplayName})  " +
            $"{Region.Size[0]}x{Region.Size[1]}m  " +
            $"배치 {placed}  NPC {npcs}  몬스터 {monsters}  " +
            $"흔들림 {_swayingCount}  회전 {_spinningCount}  연기 {_smokeCount}  " +
            $"물 {_waterCells}  콜라이더 {_colliderCount}  " +
            $"고유메시 {_cache.Count}  {sw.ElapsedMilliseconds}ms  " +
            $"pvp={Region.Flags.Pvp}");
    }

    // -----------------------------------------------------------------------

    private void BuildGround()
    {
        float w = Region.Size[0];
        float h = Region.Size[1];

        var material = new StandardMaterial3D
        {
            // 색은 정점에 구워 넣는다 — 도로가 지면에 칠해져 있기 때문이다.
            AlbedoColor = Colors.White,
            VertexColorUseAsAlbedo = true,
            // 색을 16진 sRGB 로 넣었으므로 그렇게 해석하라고 알려야 한다.
            // 기본값(선형)으로 두면 전체가 바랜다.
            VertexColorIsSrgb = true,
            Roughness = 1.0f,
        };

        Mesh mesh;
        Vector3 position;
        bool isTerrain = false;

        if (Region.Terrain is { Resolution: > 1 } terrain)
        {
            // 높이맵 격자는 원점에서 시작하므로 옮기지 않는다.
            mesh = TerrainMesh.Build(
                terrain, w, new Color(Region.Ground.Color), new Color(Region.Ground.RoadColor));
            position = Vector3.Zero;
            isTerrain = true;
        }
        else
        {
            // 지형이 없는 구버전 리전. PlaneMesh 는 원점 중심이라 절반 옮긴다.
            mesh = new PlaneMesh { Size = new Vector2(w, h) };
            position = new Vector3(w * 0.5f, Region.Ground.Y, h * 0.5f);
        }

        AddChild(new MeshInstance3D
        {
            Name = "Ground",
            Mesh = mesh,
            MaterialOverride = material,
            Position = position,
        });

        if (isTerrain && CollisionEnabled)
        {
            AddChild(CollisionBuilder.BuildTerrain(mesh));
        }

        BuildWater();
    }

    /// <summary>
    /// 수면. 지면과 **같은 격자**에서 만든다 — 따로 만든 평면을 얹으면
    /// 물가에서 지형과 어긋나 틈이 벌어진다(도로 타일에서 겪은 문제).
    ///
    /// 콜리전은 붙이지 않는다. 물은 걸어 들어가는 곳이고, 통행 제한은
    /// 서버의 내비메시가 <c>water.nav = exclude</c> 로 처리한다(9장).
    /// </summary>
    private void BuildWater()
    {
        if (Region.Terrain is not { } terrain || Region.Water is not { } info)
        {
            return;
        }

        ArrayMesh? mesh = WaterMesh.Build(terrain, out int holes);
        _waterHoles = holes;
        if (mesh is null)
        {
            return;
        }

        AddChild(new MeshInstance3D
        {
            Name = "Water",
            Mesh = mesh,
            MaterialOverride = WaterMesh.Material(info),
            Position = Vector3.Zero,
            // 수면이 그림자를 드리우면 물 밑이 새카매진다.
            CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
        });
        _waterCells = info.Cells;
    }

    private int BuildPlacements()
    {
        var root = new Node3D { Name = "Placements" };
        AddChild(root);

        int count = 0;
        int swaying = 0;
        int colliders = 0;
        int spinning = 0;
        int smoking = 0;
        foreach (Placement p in Region.Placements)
        {
            Node3D? node = Instantiate(p.Asset);
            if (node is null)
            {
                continue;
            }

            // 에셋 ID 로 이름을 붙인다. 자동 이름(@Node3D@324)이면 콜리전 문제를
            // 추적할 때 무엇에 부딪혔는지 알 수 없다.
            node.Name = $"{p.Asset}#{count}";
            node.Position = new Vector3(p.Pos[0], p.Pos[1], p.Pos[2]);
            node.Rotation = new Vector3(0, p.Yaw, 0);
            node.Scale = Vector3.One * p.Scale;
            root.AddChild(node);
            count++;

            if (WindEnabled && WindMaterial.Applies(p.Asset))
            {
                // 큰 나무는 덜, 작은 풀은 더 흔들리게 한다. 같은 각도로 흔들면
                // 나무 꼭대기가 몇 미터씩 움직여 부자연스럽다.
                bool tall = p.Asset.StartsWith("prop_tree_");
                WindMaterial.Apply(node, tall ? 0.022f : 0.11f);
                swaying++;
            }

            // 풍차 날개와 물레방아 바퀴를 돌린다. 위상은 배치 순서로 흩는다.
            if (Spinner.Attach(node, p.Asset, count * 1.7f))
            {
                spinning++;
            }

            if (p.Smoke)
            {
                smoking += Smoke.Attach(
                    node, _manifest.Assets[p.Asset].Chimney, p.Scale, count * 0.73f);
            }

            if (CollisionEnabled
                && CollisionBuilder.Attach(node, _manifest.Assets[p.Asset], p.Collision, p.Scale))
            {
                colliders++;
            }
        }

        _swayingCount = swaying;
        _colliderCount = colliders;
        _spinningCount = spinning;
        _smokeCount = smoking;
        return count;
    }

    private int BuildNpcs()
    {
        if (Region.Npcs.Count == 0)
        {
            return 0;
        }

        var root = new Node3D { Name = "Npcs" };
        AddChild(root);

        int count = 0;
        foreach (Npc npc in Region.Npcs)
        {
            Node3D? model = Instantiate(npc.Asset);
            if (model is null)
            {
                continue;
            }

            // 모델을 액터 밑에 넣는다. 액터가 회전하고 모델은 가만히 있는다 —
            // 플레이어와 같은 구조라 시선 처리 코드가 한 벌로 끝난다.
            var actor = new NpcActor
            {
                Name = npc.Id,
                Position = new Vector3(npc.Pos[0], npc.Pos[1], npc.Pos[2]),
                Rotation = new Vector3(0, npc.Yaw, 0),
                HomeYaw = npc.Yaw,
                IdleClip = npc.Animation,
                IdleVariants = npc.IdleVariants,
            };
            root.AddChild(actor);

            model.Scale = Vector3.One * _manifest.Assets[npc.Asset].Scale;
            actor.AddChild(model);
            actor.Bind(model);
            count++;
        }
        return count;
    }

    /// <summary>
    /// 스폰 그룹을 눈으로 확인할 수 있게 실제 몬스터 모델을 뿌린다.
    /// 서버가 붙기 전까지의 임시 표시다 — 스폰 권한은 서버에 있다(9장).
    /// </summary>
    private int BuildSpawns()
    {
        if (Region.Spawns.Count == 0)
        {
            return 0;
        }

        var root = new Node3D { Name = "SpawnPreview" };
        AddChild(root);

        int total = 0;
        foreach (SpawnGroup group in Region.Spawns)
        {
            var groupNode = new Node3D { Name = group.Id };
            root.AddChild(groupNode);

            // 스폰 지점마다 고정 시드를 써서 볼 때마다 위치가 바뀌지 않게 한다.
            var rng = new RandomNumberGenerator();
            rng.Seed = (ulong)group.Id.GetHashCode();

            float scale = _manifest.Assets[group.Asset].Scale;
            for (int i = 0; i < group.Count; i++)
            {
                Node3D? node = Instantiate(group.Asset);
                if (node is null)
                {
                    break;
                }

                float angle = rng.RandfRange(0, Mathf.Tau);
                float dist = Mathf.Sqrt(rng.Randf()) * group.Radius;
                node.Position = new Vector3(
                    group.Center[0] + Mathf.Cos(angle) * dist,
                    group.Center[1],
                    group.Center[2] + Mathf.Sin(angle) * dist);
                node.Rotation = new Vector3(0, rng.RandfRange(0, Mathf.Tau), 0);
                node.Scale = Vector3.One * scale;
                groupNode.AddChild(node);
                total++;
            }

            if (ShowDebugMarkers)
            {
                groupNode.AddChild(Marker(
                    new Vector3(group.Center[0], group.Center[1] + 0.1f, group.Center[2]),
                    group.Radius,
                    new Color(1f, 0.35f, 0.2f, 0.25f)));
            }
        }
        return total;
    }

    private void BuildPatrolRoutes()
    {
        if (Region.PatrolRoutes.Count == 0)
        {
            return;
        }

        var root = new Node3D { Name = "PatrolRoutes" };
        AddChild(root);

        var material = new StandardMaterial3D
        {
            AlbedoColor = new Color(1f, 0.9f, 0.2f),
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
        };

        foreach (PatrolRoute route in Region.PatrolRoutes)
        {
            var mesh = new ImmediateMesh();
            mesh.SurfaceBegin(Mesh.PrimitiveType.LineStrip, material);
            foreach (float[] wp in route.Waypoints)
            {
                mesh.SurfaceAddVertex(new Vector3(wp[0], wp[1] + 0.5f, wp[2]));
            }
            if (route.Loop && route.Waypoints.Count > 0)
            {
                float[] first = route.Waypoints[0];
                mesh.SurfaceAddVertex(new Vector3(first[0], first[1] + 0.5f, first[2]));
            }
            mesh.SurfaceEnd();

            root.AddChild(new MeshInstance3D { Name = route.Id, Mesh = mesh });
        }
    }

    // -----------------------------------------------------------------------

    private static MeshInstance3D Marker(Vector3 center, float radius, Color color)
    {
        var material = new StandardMaterial3D
        {
            AlbedoColor = color,
            Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
            ShadingMode = BaseMaterial3D.ShadingModeEnum.Unshaded,
            CullMode = BaseMaterial3D.CullModeEnum.Disabled,
        };

        return new MeshInstance3D
        {
            Name = "AreaMarker",
            Mesh = new CylinderMesh
            {
                TopRadius = radius,
                BottomRadius = radius,
                Height = 0.2f,
                RadialSegments = 32,
            },
            MaterialOverride = material,
            Position = center,
        };
    }

    /// <summary>
    /// 에셋 ID 로 인스턴스를 만든다. 같은 모델은 PackedScene 을 재사용한다
    /// — 1400 개 배치에서 파일을 1400 번 여는 것을 막는다.
    /// </summary>
    private Node3D? Instantiate(string assetId)
    {
        if (!_cache.TryGetValue(assetId, out PackedScene? scene))
        {
            string path = _manifest.ResourcePath(assetId);
            scene = ResourceLoader.Load<PackedScene>(path);
            if (scene is null)
            {
                GD.PushWarning($"[world] 에셋을 열 수 없다: {assetId} -> {path}");
            }
            _cache[assetId] = scene!;
        }

        return scene?.Instantiate<Node3D>();
    }
}
