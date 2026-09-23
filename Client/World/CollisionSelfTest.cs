using Godot;

namespace Client.World;

/// <summary>
/// 콜리전이 실제로 막는지 레이캐스트로 확인한다. GUI 없이 돌릴 수 있다.
///
///   godot --path Client -- --selftest
///
/// 눈으로 보면 "벽이 있다"까지만 알 수 있고 "뚫리는지"는 모른다.
/// 특히 나무는 수관 전체가 벽이 되면 숲을 지나갈 수 없는데 화면으로는 구분이 안 된다.
/// </summary>
public static class CollisionSelfTest
{
    private readonly record struct Probe(
        string Name, Vector3 From, Vector3 To, bool ExpectHit, string Why);

    public static int Run(Node3D context, Region region)
    {
        PhysicsDirectSpaceState3D space = context.GetWorld3D().DirectSpaceState;

        // 마을 기준 좌표. 리전이 바뀌면 이 검사는 건너뛴다.
        if (region.Id != "town")
        {
            GD.Print("[selftest] town 리전에서만 돌린다");
            return 0;
        }

        var probes = new[]
        {
            // 스폰 지점(128, 210)과 광장 중심(우물)은 피한다.
            // 그 위로 쏘면 플레이어 캡슐이나 우물에 맞아 지형 검사가 거짓 통과한다.
            new Probe("지형(남문 앞)", new Vector3(146, 40, 198), new Vector3(146, -5, 198),
                true, "아래로 쏘면 지형에 맞아야 한다"),
            new Probe("지형(광장)", new Vector3(140, 40, 140), new Vector3(140, -5, 140),
                true, "광장 상판(약 6.0m)에 맞아야 한다"),
            new Probe("성벽(동쪽 변)", new Vector3(200, 3, 214), new Vector3(200, 3, 232),
                true, "성벽을 가로지르면 막혀야 한다"),
            new Probe("성문 통로", new Vector3(128, 2, 214), new Vector3(128, 2, 232),
                false, "성문은 뚫려 있어야 한다 — 막히면 마을을 나갈 수 없다"),
            new Probe("광장 상공", new Vector3(128, 20, 128), new Vector3(128, 20, 150),
                false, "허공에는 아무것도 없어야 한다"),
        };

        int failed = 0;
        foreach (Probe p in probes)
        {
            var query = PhysicsRayQueryParameters3D.Create(p.From, p.To);
            Godot.Collections.Dictionary hit = space.IntersectRay(query);
            bool got = hit.Count > 0;
            bool ok = got == p.ExpectHit;
            if (!ok)
            {
                failed++;
            }

            string where = got ? $" @ {(Vector3)hit["position"]}" : "";
            GD.Print($"[selftest] {(ok ? "OK  " : "실패")} {p.Name,-14} "
                     + $"기대={(p.ExpectHit ? "막힘" : "통과")} 실제={(got ? "막힘" : "통과")}{where}");
            if (!ok)
            {
                GD.Print($"           -> {p.Why}");
            }
        }

        // 나무: 줄기는 막고 수관 가장자리는 통과해야 한다.
        failed += CheckTrees(space, region);

        GD.Print($"[selftest] 실패 {failed}건");
        return failed;
    }

    /// <summary>
    /// 나무 콜라이더가 줄기 크기인지 본다.
    /// AABB 를 쓰면 반경 2m 가 넘는 벽이 되어 숲이 통과 불가가 된다.
    /// </summary>
    private static int CheckTrees(PhysicsDirectSpaceState3D space, Region region)
    {
        Placement? tree = null;
        foreach (Placement p in region.Placements)
        {
            if (p.Asset.StartsWith("prop_tree_common_") || p.Asset.StartsWith("prop_tree_pine_"))
            {
                tree = p;
                break;
            }
        }

        if (tree is null)
        {
            return 0;
        }

        var center = new Vector3(tree.Pos[0], tree.Pos[1] + 1.2f, tree.Pos[2]);
        int failed = 0;

        // 줄기 관통 — 맞아야 한다.
        var through = PhysicsRayQueryParameters3D.Create(
            center + new Vector3(-3, 0, 0), center + new Vector3(3, 0, 0));
        bool trunkHit = space.IntersectRay(through).Count > 0;

        // 줄기에서 2m 옆 — 수관 아래지만 비어 있어야 한다.
        var beside = PhysicsRayQueryParameters3D.Create(
            center + new Vector3(-3, 0, 2.0f), center + new Vector3(3, 0, 2.0f));
        bool sideHit = space.IntersectRay(beside).Count > 0;

        if (!trunkHit)
        {
            failed++;
        }
        if (sideHit)
        {
            failed++;
        }

        GD.Print($"[selftest] {(trunkHit ? "OK  " : "실패")} 나무 줄기      기대=막힘 "
                 + $"실제={(trunkHit ? "막힘" : "통과")}  ({tree.Asset})");
        GD.Print($"[selftest] {(!sideHit ? "OK  " : "실패")} 나무 옆 2m     기대=통과 "
                 + $"실제={(sideHit ? "막힘" : "통과")}  수관이 벽이 되면 안 된다");
        return failed;
    }
}
