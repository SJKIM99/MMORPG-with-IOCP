using Godot;

namespace Client.World;

/// <summary>
/// 콜리전을 만든다. 모양은 전부 <c>world/manifest.json</c> + <c>regions/*.json</c> 에서 나온다.
///
/// CLAUDE.md 5장 — 서버의 Recast 내비메시도 같은 JSON 에서 나오므로
/// "클라에선 벽, 서버에선 통과" 가 구조적으로 생기지 않는다.
///
/// 지금 이 콜리전은 **클라이언트 표시용**이다. 이동 권위는 서버에 있고(9장),
/// 서버가 붙으면 여기서 막히는 것과 서버가 거부하는 것이 같아야 한다.
/// </summary>
public static class CollisionBuilder
{
    /// <summary>지형 메시를 그대로 콜리전으로 쓴다. 정적이라 trimesh 가 가장 정확하다.</summary>
    public static StaticBody3D BuildTerrain(Mesh mesh)
    {
        var body = new StaticBody3D { Name = "TerrainBody" };
        body.AddChild(new CollisionShape3D
        {
            Name = "TerrainShape",
            Shape = mesh.CreateTrimeshShape(),
        });
        return body;
    }

    /// <summary>
    /// 배치물 하나에 콜리전을 붙인다. 붙였으면 true.
    ///
    /// 모양 선택:
    /// - manifest 에 <c>collider</c> 가 있으면 그것을 쓴다. 나무가 여기 해당한다 —
    ///   AABB 를 쓰면 수관 전체(최대 13m)가 벽이 되어 숲을 지나갈 수 없다.
    /// - 없으면 메시의 AABB 로 상자를 만든다. 건물·벽·상자는 이게 맞다.
    /// </summary>
    public static bool Attach(Node3D node, AssetEntry entry, string collision, float scale)
    {
        if (collision == "none")
        {
            return false;
        }

        Shape3D? shape;
        Vector3 offset;

        if (entry.Collider is { Shape: "ramp" })
        {
            // 계단은 **시각 전용**으로 두고 콜리전은 매끄러운 경사면 하나로 만든다.
            //
            // trimesh 로 두면 단차마다 캡슐이 걸려 step-up 이 발동하고,
            // 그 한 단씩의 수직 이동이 화면에서 툭툭 튀는 것으로 보인다.
            // 경사면이면 MoveAndSlide 가 알아서 미끄러뜨려 올라간다 —
            // step-up 자체가 필요 없어진다.
            return AttachRamp(node, scale);
        }

        if (entry.Collider is { Shape: "mesh" })
        {
            // 구멍이 있는 모델 — 성문, 문틀. 상자로 덮으면 통로까지 막혀
            // 마을을 나갈 수 없게 된다 (자가진단이 실제로 잡아낸 버그).
            return AttachTrimesh(node);
        }

        if (entry.Collider is { } spec && spec.Shape == "cylinder")
        {
            Aabb box = LocalAabb(node);
            shape = new CylinderShape3D
            {
                Radius = spec.Radius * scale,
                Height = Mathf.Max(box.Size.Y * scale, 0.2f),
            };
            // 실린더는 중심이 원점이다. 밑동을 지면에 맞춘다.
            offset = new Vector3(0, (box.Position.Y + box.Size.Y * 0.5f) * scale, 0);
        }
        else
        {
            Aabb box = LocalAabb(node);
            if (box.Size.LengthSquared() < 0.0001f)
            {
                return false;
            }
            shape = new BoxShape3D { Size = box.Size * scale };
            offset = box.GetCenter() * scale;
        }

        var body = new StaticBody3D { Name = "Body" };
        body.AddChild(new CollisionShape3D { Shape = shape, Position = offset });

        // 배치 노드는 이미 배율이 걸려 있다. 몸체를 그 아래 두면 배율이 두 번
        // 곱해지므로, 크기를 직접 계산해 넣고 노드 배율은 상쇄한다.
        body.Scale = Vector3.One / Mathf.Max(scale, 0.0001f);
        node.AddChild(body);
        return true;
    }

    /// <summary>
    /// 계단을 덮는 경사면 상자 하나를 만든다.
    ///
    /// 계단 모델은 원점이 **윗단**이고 local +Z 로 내려간다 (x 폭, y 상승, z 진행).
    /// 즉 경사면은 (0, rise, 0) 에서 (0, 0, run) 으로 이어진다.
    /// 그 선분을 따라 상자를 눕히고, 표면 법선 방향으로 두께의 절반만큼 내린다.
    /// </summary>
    private static bool AttachRamp(Node3D node, float scale)
    {
        Aabb box = LocalAabb(node);
        float width = box.Size.X;
        float rise = box.Size.Y;
        float run = box.Size.Z;

        if (rise < 0.01f || run < 0.01f)
        {
            return false;
        }

        float slope = Mathf.Sqrt(rise * rise + run * run);
        float angle = Mathf.Atan2(rise, run);
        const float thickness = 0.5f;

        // 경사면 중점. 모델 원점 기준이다.
        var mid = new Vector3(box.GetCenter().X, box.Position.Y + rise * 0.5f, run * 0.5f);

        // 표면 법선은 X축 회전을 적용한 +Y 다. 그 반대로 두께 절반만큼 내린다.
        var normal = new Vector3(0, Mathf.Cos(angle), Mathf.Sin(angle));
        Vector3 center = mid - normal * (thickness * 0.5f);

        var body = new StaticBody3D { Name = "Body" };
        body.AddChild(new CollisionShape3D
        {
            Name = "Ramp",
            // Body 의 배율을 상쇄해 두었으므로 크기는 **월드 단위**로 넣어야 한다.
            // 길이를 조금 늘려 위아래 끝에서 지형과 틈이 생기지 않게 한다.
            Shape = new BoxShape3D
            {
                Size = new Vector3(width * scale, thickness, slope * scale + 0.3f),
            },
            Position = center * scale,
            Rotation = new Vector3(angle, 0, 0),
        });

        // 배치 노드에 이미 배율이 걸려 있으므로 상쇄한다.
        body.Scale = Vector3.One / Mathf.Max(scale, 0.0001f);
        node.AddChild(body);
        return true;
    }

    /// <summary>
    /// 메시 그대로를 콜리전으로 쓴다. 통로가 뚫린 모델에만 쓴다 —
    /// 정확한 대신 상자보다 비싸다.
    /// </summary>
    private static bool AttachTrimesh(Node node)
    {
        bool any = false;

        void Walk(Node n)
        {
            if (n is MeshInstance3D { Mesh: not null } mesh)
            {
                // 메시 노드 아래에 StaticBody3D 를 만들어 준다. 메시 로컬 공간이라
                // 배치의 배율·회전이 그대로 따라온다.
                mesh.CreateTrimeshCollision();
                any = true;
            }
            foreach (Node child in n.GetChildren())
            {
                Walk(child);
            }
        }

        Walk(node);
        return any;
    }

    /// <summary>노드 트리의 모든 메시를 합친 로컬 AABB (배율 적용 전).</summary>
    private static Aabb LocalAabb(Node node)
    {
        var total = new Aabb();
        bool first = true;

        void Walk(Node n, Transform3D accumulated)
        {
            if (n is VisualInstance3D visual)
            {
                Aabb local = visual.GetAabb();
                Aabb moved = accumulated * local;
                if (first)
                {
                    total = moved;
                    first = false;
                }
                else
                {
                    total = total.Merge(moved);
                }
            }

            foreach (Node child in n.GetChildren())
            {
                Transform3D next = child is Node3D c ? accumulated * c.Transform : accumulated;
                Walk(child, next);
            }
        }

        Walk(node, Transform3D.Identity);
        return total;
    }
}
