using System.Threading.Tasks;
using Godot;

namespace Client.World;

/// <summary>
/// 자식 노드 하나를 계속 돌린다. 풍차 날개와 물레방아 바퀴용이다.
///
/// KayKit 의 방앗간 모델은 날개(<c>mill_blades</c>)와 바퀴
/// (<c>watermill_wheel</c>)가 **이미 별도 노드로 분리되어** 있고 원점도
/// 축 중심에 있다. 그래서 회전만 시키면 된다 — 메시를 건드릴 필요가 없다.
///
/// 축은 메시 AABB 의 납작한 쪽으로 정한다:
///   mill_blades      X,Y 로 +-0.667 펼쳐지고 Z 가 얇다  -> Z 축
///   watermill_wheel  Y,Z 로 +-0.497 펼쳐지고 X 가 얇다  -> X 축
///
/// 클라이언트 전용 연출이다. 서버는 방앗간이 도는지 모른다(6장).
/// </summary>
public partial class Spinner : Node3D
{
    /// <summary>회전 대상. 이 노드의 자식이 아니어도 된다.</summary>
    public Node3D Target { get; set; } = null!;

    /// <summary>로컬 회전축.</summary>
    public Vector3 Axis { get; set; } = Vector3.Back;

    /// <summary>각속도(rad/s).</summary>
    public float Speed { get; set; } = 0.6f;

    /// <summary>에셋 ID 로 회전 규칙을 찾는다. 해당 없으면 null.</summary>
    ///
    /// <remarks>
    /// Node 는 **glTF 원본의 노드 이름**이다. Godot 임포터가 이름을 바꿀 수 있어
    /// 그대로 못 찾을 수 있다 — <see cref="Find"/> 의 주석 참조.
    /// </remarks>
    public static (string Node, Vector3 Axis, float Speed)? RuleFor(string assetId) => assetId switch
    {
        // 풍차는 바람을 받아 느리게 돈다. 빠르면 장난감처럼 보인다.
        "bld_mill" => ("mill_blades", Vector3.Back, 0.55f),
        // 물레방아는 물에 밀려 더 느리다.
        "bld_watermill" => ("watermill_wheel", Vector3.Right, 0.40f),
        _ => null,
    };

    /// <summary>
    /// 배치 노드 안에서 대상 노드를 찾아 회전을 건다. 찾으면 true.
    /// </summary>
    public static bool Attach(Node3D placement, string assetId, float phase)
    {
        if (RuleFor(assetId) is not { } rule)
        {
            return false;
        }

        // 원본 이름으로 먼저 찾고, 없으면 임포터가 접미사를 떼어냈다고 보고 다시 찾는다.
        Node3D? target = Find(placement, rule.Node)
                         ?? Find(placement, StripImportSuffix(rule.Node));
        if (target is null)
        {
            // 팩이 업데이트되어 노드 이름이 바뀌면 조용히 안 돌게 된다.
            GD.PushWarning($"[spin] {assetId} 안에서 '{rule.Node}' 를 찾지 못했다");
            return false;
        }

        var spinner = new Spinner
        {
            Name = "Spin",
            Target = target,
            Axis = rule.Axis,
            Speed = rule.Speed,
        };
        placement.AddChild(spinner);

        // 방앗간 4채가 모두 같은 각도로 돌면 복제한 티가 난다. 위상을 흩는다.
        target.Rotate(rule.Axis, phase);
        return true;
    }

    public override void _Process(double delta)
    {
        if (GodotObject.IsInstanceValid(Target))
        {
            Target.Rotate(Axis, Speed * (float)delta);
        }
    }

    /// <summary>
    /// 회전체가 실제로 **돌고 있는지** 확인한다. 실패 수를 돌려준다.
    ///
    /// 정지 화면으로는 구분할 수 없다. 날개가 기울어 있는 것과 날개가 도는 것은
    /// 스크린샷에서 똑같이 보인다. 실제로 두 시점의 각도를 재는 수밖에 없다.
    ///
    /// 임포터가 노드 이름을 바꾸면(<see cref="Find"/>) 조용히 안 돌게 되므로
    /// 이 검사가 없으면 팩을 갱신했을 때 알아채지 못한다.
    /// </summary>
    /// <param name="expected">
    /// 로더가 붙였다고 보고한 개수. 이 값과 대조한다.
    /// "0 이면 실패"로 두면 방앗간이 없는 필드에서 항상 실패한다 — 리전마다
    /// 기대값이 다르므로 상수로 박지 않고 로더에게 묻는다.
    /// </param>
    public static async Task<int> SelfTest(Node root, SceneTree tree, int expected)
    {
        var spinners = new Godot.Collections.Array<Node>();
        Collect(root, spinners);

        if (spinners.Count != expected)
        {
            GD.Print($"[spin] 실패 회전체 {spinners.Count}개 (기대 {expected}개)");
            return 1;
        }
        if (expected == 0)
        {
            GD.Print("[spin] OK   이 리전에는 회전체가 없다");
            return 0;
        }

        var before = new float[spinners.Count];
        for (int i = 0; i < spinners.Count; i++)
        {
            before[i] = ((Spinner)spinners[i]).Target.Rotation.Dot(((Spinner)spinners[i]).Axis);
        }

        // 가장 느린 물레방아(0.40 rad/s)도 눈에 띄게 돌 만큼 기다린다.
        for (int i = 0; i < 30; i++)
        {
            await tree.ToSignal(tree, SceneTree.SignalName.ProcessFrame);
        }

        int stuck = 0;
        float least = float.MaxValue;
        for (int i = 0; i < spinners.Count; i++)
        {
            var spinner = (Spinner)spinners[i];
            float moved = Mathf.Abs(spinner.Target.Rotation.Dot(spinner.Axis) - before[i]);
            least = Mathf.Min(least, moved);
            if (moved < 0.001f)
            {
                stuck++;
            }
        }

        GD.Print($"[spin] {(stuck == 0 ? "OK  " : "실패")} 회전체 {spinners.Count}개, "
                 + $"멈춘 것 {stuck}개 (기대 0개), 최소 회전 {least:F3}rad");
        return stuck == 0 ? 0 : 1;
    }

    private static void Collect(Node node, Godot.Collections.Array<Node> into)
    {
        if (node is Spinner spinner && GodotObject.IsInstanceValid(spinner.Target))
        {
            into.Add(spinner);
        }
        foreach (Node child in node.GetChildren())
        {
            Collect(child, into);
        }
    }

    /// <summary>
    /// 노드를 이름으로 찾는다. **가장 깊은** 일치를 돌려준다.
    ///
    /// 깊이로 고르는 이유가 있다. Godot 의 씬 임포터는 노드 이름의 접미사를
    /// 예약어로 해석해 노드 타입을 바꾸고 **접미사를 이름에서 떼어낸다**
    /// (`-col`, `-convcol`, `-rigid`, `-vehicle`, `-wheel`, `-navmesh` 등.
    /// 하이픈뿐 아니라 밑줄도 인식한다).
    ///
    /// 그래서 `watermill_wheel` 이 `_wheel` 에 걸려 **VehicleWheel3D 로 바뀌고
    /// 이름이 `watermill` 이 된다.** 실제 트리가 이렇게 나온다:
    ///
    ///     bld_watermill#168 > watermill > watermill > watermill
    ///                          (씬루트)   (glTF루트)  (바퀴, 원래 watermill_wheel)
    ///
    /// 원본 이름으로는 못 찾고, 떼어낸 이름으로 찾으면 맨 위 루트가 먼저 걸려
    /// **건물 전체가 돌아간다.** 가장 깊은 것을 고르면 바퀴가 잡힌다.
    /// `mill_blades` 는 예약어가 아니라 그대로 남으므로 첫 시도에서 끝난다.
    /// </summary>
    private static Node3D? Find(Node node, string name)
    {
        Node3D? best = null;
        int bestDepth = -1;
        Walk(node, 0);
        return best;

        void Walk(Node current, int depth)
        {
            if (current is Node3D found && current.Name == name && depth > bestDepth)
            {
                best = found;
                bestDepth = depth;
            }
            foreach (Node child in current.GetChildren())
            {
                Walk(child, depth + 1);
            }
        }
    }

    /// <summary>임포터가 떼어냈을 법한 접미사를 지운 이름.</summary>
    private static string StripImportSuffix(string name)
    {
        foreach (string keyword in ImportKeywords)
        {
            if (name.EndsWith("_" + keyword) || name.EndsWith("-" + keyword))
            {
                return name[..^(keyword.Length + 1)];
            }
        }
        return name;
    }

    private static readonly string[] ImportKeywords =
    {
        "wheel", "col", "convcol", "colonly", "convcolonly", "rigid", "vehicle", "navmesh",
    };
}
