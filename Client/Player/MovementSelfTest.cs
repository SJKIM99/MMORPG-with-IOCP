using System.Threading.Tasks;
using Godot;

namespace Client.Player;

/// <summary>
/// 플레이어를 스크립트로 움직여 실제로 오를 수 있는지 본다.
///
///   godot --path Client -- --movetest
///
/// 계단은 눈으로 봐서는 오를 수 있는지 알 수 없다. 콜라이더가 통짜 상자여도
/// 화면에는 계단으로 보인다. 실제로 걸어 보는 수밖에 없다.
/// </summary>
public static class MovementSelfTest
{
    /// <summary>
    /// ExpectY 는 "얼마나 올랐나"가 아니라 "어디에 올라섰나"다.
    /// 상승량으로 재면 출발 지점의 낙하가 덜 끝났을 때 음수가 나와 판정이 흔들린다.
    /// 목표 표고로 재면 흔들리지 않는다.
    /// </summary>
    private readonly record struct Case(
        string Name, Vector3 Start, Vector2 Direction, float ExpectY, string Why);

    /// <summary>이 검사가 좌표를 알고 있는 리전. 다른 리전에서는 건너뛴다.</summary>
    private const string Region = "town";

    /// <summary>검사를 돌리고 실패 수를 돌려준다.</summary>
    /// <param name="regionId">
    /// 현재 리전. 검사 좌표(광장 계단, 성 언덕, 개울 다리)가 전부 마을 것이라
    /// 다른 리전에서 돌리면 허공에서 출발해 월드 밖으로 떨어진다 —
    /// 실제로 필드에서 y = -846 이 나왔다. 좌표를 리전마다 만들 값어치는 없고,
    /// 조용히 통과시키면 검사가 있는 줄 알고 방심하게 되므로 건너뛴다고 찍는다.
    /// </param>
    public static async Task<int> Run(PlayerController player, SceneTree tree, string regionId)
    {
        if (regionId != Region)
        {
            GD.Print($"[movetest] 건너뜀 — 검사 좌표가 '{Region}' 전용이다 (현재 '{regionId}')");
            return 0;
        }

        // 마을 광장 진입 계단. 상판 5.98m, 발치 2.98m — 3m 를 올라야 한다.
        var cases = new[]
        {
            // y 는 넉넉히 띄워서 시작한다. 지형 속에서 시작하면 끼어서
            // 이동 0m 가 나오고 무엇이 문제인지 구분되지 않는다.
            new Case("남쪽 계단", new Vector3(128, 6.0f, 164), new Vector2(0, -1), 5.5f,
                "광장(상판 5.98m)에 점프 없이 올라서야 한다"),
            new Case("동쪽 계단", new Vector3(164, 6.0f, 128), new Vector2(-1, 0), 5.5f,
                "방향이 달라도 같아야 한다"),
            new Case("성 언덕 경사", new Vector3(128, 9.0f, 95), new Vector2(0, -1), 9.5f,
                "경사면으로 성 언덕 상판(10.14m)까지 올라야 한다"),
            new Case("평지 전진", new Vector3(128, 3.0f, 200), new Vector2(0, -1), -1.0f,
                "막히지 않고 앞으로 나아가야 한다"),
        };

        int failed = 0;
        foreach (Case c in cases)
        {
            failed += await RunCase(player, tree, c);
        }

        failed += CheckDirections(player);
        failed += await CheckFacing(player, tree);
        failed += await CheckSmoothClimb(player, tree);
        failed += await CheckBridge(player, tree);
        GD.Print($"[movetest] 실패 {failed}건");
        return failed;
    }

    /// <summary>
    /// 개울의 다리를 **점프 없이** 건널 수 있는지 본다.
    ///
    /// 다리는 물 위에 얹기만 하면 상판이 도로보다 높이 솟아 벽이 된다.
    /// 실제로 상판이 1.5m 높아 점프로도 못 올라갔다. 도로 쪽 지형을 상판
    /// 높이까지 돋워야 하는데, 그 높이는 지형·수면·모델 치수가 맞물려 나오므로
    /// 하나만 어긋나도 조용히 다시 벽이 된다. 걸어서 확인한다.
    /// </summary>
    private static async Task<int> CheckBridge(PlayerController player, SceneTree tree)
    {
        // 서쪽 진입로. 다리는 x 56~68 에 있고 개울 수면은 0.62m 다.
        const float startX = 48.0f;
        const float goalX = 74.0f;
        const float minY = 1.0f;   // 이보다 낮으면 물에 빠진 것이다

        player.Velocity = Vector3.Zero;
        player.GlobalPosition = new Vector3(startX, 5.0f, 128.0f);
        player.ScriptedIntent = null;
        for (int i = 0; i < 90; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }

        Vector3 from = player.GlobalPosition;
        player.ScriptedIntent = new PlayerController.Intent { Move = new Vector2(1, 0) };
        for (int i = 0; i < 420; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }
        Vector3 to = player.GlobalPosition;
        player.ScriptedIntent = null;

        bool crossed = to.X >= goalX;
        bool dry = to.Y >= minY;
        bool ok = crossed && dry;

        GD.Print($"[movetest] {(ok ? "OK  " : "실패")} 다리 건너기    "
                 + $"x {from.X:F1} -> {to.X:F1} (기대 >= {goalX:F0}), "
                 + $"y {to.Y:F2} (기대 >= {minY:F1})");
        if (!crossed)
        {
            GD.Print("           -> 다리 앞에서 막혔다. 상판 끝 높이가 도로와 어긋났을 것이다");
        }
        else if (!dry)
        {
            GD.Print("           -> 다리를 못 타고 개울에 빠졌다");
        }
        return ok ? 0 : 1;
    }

    /// <summary>
    /// 계단을 오를 때 Y 가 **연속적으로** 오르는지 본다.
    ///
    /// 화면으로는 "물리는 맞는데 카메라 때문에 튀어 보이는 것"과 구분할 수 없다.
    /// 매 물리 틱의 Y 증가분을 재서, 한 틱에 크게 뛰는 구간이 있으면 순간이동이다.
    /// 걷기 4.2m/s 에 경사 33도면 한 틱(1/60초)의 정상 상승은 약 0.046m 다.
    /// </summary>
    private static async Task<int> CheckSmoothClimb(PlayerController player, SceneTree tree)
    {
        player.Velocity = Vector3.Zero;
        player.GlobalPosition = new Vector3(128, 6.0f, 160);
        player.ScriptedIntent = null;
        for (int i = 0; i < 90; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }

        player.ScriptedIntent = new PlayerController.Intent { Move = new Vector2(0, -1) };

        // **보이는 위치**를 잰다. 몸체는 턱을 넘을 때 필연적으로 순간이동하고,
        // 화면에 나오는 것은 보간된 모델이다. 둘을 섞으면 "물리는 맞는데
        // 카메라 때문에 튀어 보이는 경우"와 구분할 수 없다.
        float previous = player.VisualPosition.Y;
        float biggest = 0.0f;
        int jumps = 0;
        for (int i = 0; i < 300; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
            float y = player.VisualPosition.Y;
            float step = y - previous;
            previous = y;

            if (step > biggest)
            {
                biggest = step;
            }
            if (step > 0.20f)
            {
                jumps++;
            }
        }
        player.ScriptedIntent = null;

        // 0.20m 를 넘는 상승이 한 틱에 일어나면 계단 한 단을 통째로 뛴 것이다.
        bool ok = jumps == 0;
        GD.Print($"[movetest] {(ok ? "OK  " : "실패")} 오름 연속성    "
                 + $"한 틱 최대 상승 {biggest:F3}m, 0.20m 초과 {jumps}회 (기대 0회)");
        if (!ok)
        {
            GD.Print("           -> 계단을 한 단씩 뛰어오르고 있다. "
                     + "콜리전이 경사면이 아니라 단차로 되어 있을 가능성이 크다");
        }
        return ok ? 0 : 1;
    }

    /// <summary>
    /// 걸어갈 때 모델이 **진행 방향**을 보는지 확인한다.
    /// 반대면 뒤로 걷는 것처럼 보이고, 3인칭에서는 캐릭터가 카메라를 쳐다본다.
    /// </summary>
    private static async Task<int> CheckFacing(PlayerController player, SceneTree tree)
    {
        player.Velocity = Vector3.Zero;
        player.GlobalPosition = new Vector3(128, 3.0f, 200);
        for (int i = 0; i < 60; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }

        var dir = new Vector2(0, -1);   // 북쪽(-Z)으로 걷는다
        player.ScriptedIntent = new PlayerController.Intent { Move = dir };
        for (int i = 0; i < 90; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }

        Node3D? model = player.GetNodeOrNull<Node3D>("Model");
        player.ScriptedIntent = null;
        if (model is null)
        {
            return 0;
        }

        // KayKit 모델의 앞면은 +Z 다.
        Vector3 facing = model.GlobalTransform.Basis.Z;
        var want = new Vector3(dir.X, 0, dir.Y);
        float dot = new Vector3(facing.X, 0, facing.Z).Normalized().Dot(want);

        bool ok = dot > 0.9f;
        GD.Print($"[movetest] {(ok ? "OK  " : "실패")} 모델 방향     "
                 + $"진행 {want} 과 정면의 내적 {dot:F2} (기대 > 0.9)");
        if (!ok)
        {
            GD.Print("           -> 캐릭터가 진행 방향을 봐야 한다. "
                     + "음수면 180도 뒤집힌 것 — 카메라를 쳐다보게 된다");
        }
        return ok ? 0 : 1;
    }

    /// <summary>
    /// 카메라 기준 WASD 가 맞는지 본다. 수식 오류는 yaw=0 에서는 드러나지 않는다 —
    /// 실제로 카메라를 돌린 상태에서 재야 한다.
    /// </summary>
    private static int CheckDirections(PlayerController player)
    {
        Node3D? pivot = player.CameraPivot;
        if (pivot is null)
        {
            return 0;
        }

        float saved = pivot.GlobalRotation.Y;
        int failed = 0;

        foreach (float yawDeg in new[] { 0.0f, 90.0f, 180.0f, -90.0f })
        {
            pivot.GlobalRotation = new Vector3(0, Mathf.DegToRad(yawDeg), 0);

            // 카메라가 보는 방향(로컬 -Z)을 월드로 옮긴 것.
            Vector3 forward = -pivot.GlobalTransform.Basis.Z;
            Vector3 right = pivot.GlobalTransform.Basis.X;

            foreach ((string key, Vector2 raw, Vector3 want) in new[]
            {
                ("W", new Vector2(0, -1), forward),
                ("S", new Vector2(0, 1), -forward),
                ("D", new Vector2(1, 0), right),
                ("A", new Vector2(-1, 0), -right),
            })
            {
                Vector2 got = player.RotateByCamera(raw);
                var got3 = new Vector3(got.X, 0, got.Y);
                var want2 = new Vector3(want.X, 0, want.Z).Normalized();

                bool ok = got3.Normalized().Dot(want2) > 0.99f;
                if (!ok)
                {
                    failed++;
                    GD.Print($"[movetest] 실패 방향 yaw={yawDeg,4:F0}도 {key} "
                             + $"기대 {want2} 실제 {got3.Normalized()}");
                }
            }
        }

        pivot.GlobalRotation = new Vector3(0, saved, 0);
        GD.Print($"[movetest] {(failed == 0 ? "OK  " : "실패")} WASD 방향     "
                 + $"카메라 yaw 4종 x 4키 = 16건 중 실패 {failed}");
        return failed;
    }

    private static async Task<int> RunCase(PlayerController player, SceneTree tree, Case c)
    {
        player.Velocity = Vector3.Zero;
        player.GlobalPosition = c.Start;
        player.ScriptedIntent = null;

        // 완전히 내려앉을 때까지 기다린다. 낙하가 남아 있으면 시작 표고가 흔들린다.
        for (int i = 0; i < 90; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }

        Vector3 from = player.GlobalPosition;
        player.ScriptedIntent = new PlayerController.Intent { Move = c.Direction };

        // 약 7초 걸어 본다. 진입 램프가 길어 계단 앞까지 20m 넘게 가야 한다.
        for (int i = 0; i < 420; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.PhysicsFrame);
        }

        Vector3 to = player.GlobalPosition;
        player.ScriptedIntent = null;

        float travelled = new Vector2(to.X - from.X, to.Z - from.Z).Length();

        bool ok;
        string verdict;
        if (c.ExpectY < 0)
        {
            ok = travelled > 5.0f;
            verdict = $"이동 {travelled:F1}m (기대 > 5m)";
        }
        else
        {
            ok = to.Y >= c.ExpectY;
            verdict = $"도달 y={to.Y:F2} (기대 >= {c.ExpectY:F1}), "
                      + $"시작 y={from.Y:F2}, 이동 {travelled:F1}m";
        }

        GD.Print($"[movetest] {(ok ? "OK  " : "실패")} {c.Name,-12} {verdict}");
        if (!ok)
        {
            GD.Print($"           -> {c.Why}");
            GD.Print($"           시작 {from}  끝 {to}");

            // 왜 멈췄는지 — 바닥에 서 있나, 벽에 붙었나, 그 자리 바닥은 어디인가.
            var probe = PhysicsRayQueryParameters3D.Create(
                to + Vector3.Up * 5.0f, to + Vector3.Down * 5.0f);
            probe.Exclude = new Godot.Collections.Array<Rid> { player.GetRid() };
            Godot.Collections.Dictionary hit =
                player.GetWorld3D().DirectSpaceState.IntersectRay(probe);
            string floor = "없음";
            if (hit.Count > 0)
            {
                var body = (Node3D)hit["collider"];
                Node3D? owner = body.GetParent() as Node3D;
                // local 과 global 을 같이 찍는다. 둘이 다르면 조상 노드에 변환이
                // 걸린 것이다 — 실제로 씬 루트가 0.6도 기울어 있던 적이 있다.
                floor = $"{((Vector3)hit["position"]).Y:F2}  {body.GetPath()}\n"
                        + $"           local={owner?.Position} global={owner?.GlobalPosition}";
            }
            GD.Print($"           접지={player.IsOnFloor()} 벽={player.IsOnWall()} "
                     + $"속도={player.Velocity.Length():F2} 발밑바닥={floor}");
        }
        return ok ? 0 : 1;
    }
}
