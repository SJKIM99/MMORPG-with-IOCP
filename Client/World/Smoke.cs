using Godot;

namespace Client.World;

/// <summary>
/// 굴뚝 연기. 순수 클라이언트 연출이다 — 서버는 굴뚝을 모른다(CLAUDE.md 6장).
///
/// 굴뚝 위치는 런타임에 메시를 뒤져 찾지 않는다. <c>tools/find_chimneys.py</c>
/// 가 미리 찾아 manifest 에 <c>chimney</c> 로 적어 뒀다(5장 — 데이터로 둔다).
/// 어느 집에 불이 지펴져 있는지는 배치 JSON 의 <c>smoke</c> 가 정한다.
///
/// 파티클 재질도 텍스처 없이 만든다. 이미지를 만들 수 없으므로(1장)
/// 작은 구체 메시를 띄우고 시간에 따라 부풀리며 투명하게 만든다.
/// </summary>
public static class Smoke
{
    // 입자 수와 수명은 **함께** 정해야 한다. 초당 방출량 = Amount / Lifetime 이다.
    //
    // 처음에 10개 / 5초로 두고 중력을 +0.25 로 줬더니 입자가 계속 가속해
    // 30m 짜리 기둥이 되었고, 거기에 초당 2개만 뿌리니 화면에서 보이지 않았다.
    // 렌더링 문제로 착각했지만 순전히 튜닝 문제였다.
    private const int Amount = 44;
    private const float Lifetime = 4.2f;

    private static Mesh? _puff;
    private static StandardMaterial3D? _material;

    /// <summary>
    /// 배치 노드에 연기를 붙인다. 붙였으면 true.
    /// </summary>
    /// <param name="placement">배치 노드. 이미 scale 이 걸려 있다.</param>
    /// <param name="chimneys">모델 로컬 좌표의 굴뚝 위치들(배율 적용 전).</param>
    /// <param name="scale">배치 배율. 부모에 걸린 배율을 상쇄하는 데 쓴다.</param>
    /// <param name="phase">굴뚝마다 다르게 주어 같은 박자로 뿜지 않게 한다.</param>
    public static int Attach(Node3D placement, float[][] chimneys, float scale, float phase)
    {
        if (chimneys.Length == 0)
        {
            return 0;
        }

        _puff ??= new SphereMesh
        {
            // 최종 입자 크기 = 이 반지름 x Scale(Min~Max) x ScaleCurve 다.
            // 셋이 다 곱해지는 것을 잊고 반지름만 보고 잡았다가 6cm 짜리
            // 입자를 만들어 한 번 더 헤맸다.
            Radius = 0.9f,
            Height = 1.8f,
            RadialSegments = 6,
            Rings = 3,
            Material = PuffMaterial(),
        };

        int made = 0;
        foreach (float[] c in chimneys)
        {
            if (c.Length < 3)
            {
                continue;
            }

            var particles = new GpuParticles3D
            {
                Name = $"Smoke{made}",
                // 위치는 부모(배율 적용됨) 공간이므로 모델 로컬 좌표를 그대로 쓴다.
                // 굴뚝 입구보다 아주 조금 위에서 시작해 돌 틈으로 새지 않게 한다.
                Position = new Vector3(c[0], c[1] + 0.05f, c[2]),
                Amount = Amount,
                Lifetime = Lifetime,

                // **부모 배율을 상쇄한다.** 이걸 빼먹으면 입자 크기뿐 아니라
                // **속도와 중력까지 6배**가 되어 연기가 20m 넘게 치솟는다.
                // 굴뚝에서 연기가 끊겨 보여서 렌더링 문제로 한참 헤맸다.
                // CollisionBuilder 가 콜라이더에 쓰는 것과 같은 수법이다.
                Scale = Vector3.One / scale,
                DrawPass1 = _puff,
                ProcessMaterial = ProcessMaterial(),
                // 미리 돌려 둔다. 안 그러면 마을에 들어선 순간 굴뚝 44개가
                // 동시에 연기를 뿜기 시작한다.
                Preprocess = Lifetime,
                SpeedScale = 1.0f,
                // 연기가 그림자를 드리우면 지붕이 얼룩진다.
                CastShadow = GeometryInstance3D.ShadowCastingSetting.Off,
                // 연기가 굴뚝에서 3m 쯤 피어오른다. 컬링 범위를 직접 준다 —
                // 기본값은 방출 지점만 보고 판단해 화면 가장자리에서 툭 사라진다.
                VisibilityAabb = new Aabb(new Vector3(-4, 0, -4), new Vector3(8, 11, 8)),
            };
            placement.AddChild(particles);

            // 같은 위상으로 뿜으면 마을 전체가 한 박자로 숨쉬는 것처럼 보인다.
            particles.Preprocess = Lifetime * (0.2f + 0.8f * Mathf.Abs(Mathf.Sin(phase)));
            made++;
        }
        return made;
    }

    private static ParticleProcessMaterial ProcessMaterial() => new()
    {
        Direction = Vector3.Up,
        Spread = 10.0f,
        InitialVelocityMin = 0.7f,
        InitialVelocityMax = 1.2f,
        // **Y 를 0 근처로 둔다.** 양수면 계속 가속해 연기가 하늘 끝까지 올라간다.
        // 옆으로만 밀어 바람에 흘러가는 모양을 만든다.
        Gravity = new Vector3(0.34f, 0.05f, 0.18f),
        // 피어오르며 부풀고 흩어진다.
        ScaleMin = 0.5f,
        ScaleMax = 0.8f,
        ScaleCurve = Ramp(0.45f, 2.0f),
        AngularVelocityMin = -25.0f,
        AngularVelocityMax = 25.0f,
        Color = new Color(0.86f, 0.85f, 0.83f, 1.0f),
        AlphaCurve = Fade(),
        EmissionShape = ParticleProcessMaterial.EmissionShapeEnum.Sphere,
        EmissionSphereRadius = 0.16f,
    };

    private static StandardMaterial3D PuffMaterial() => _material ??= new StandardMaterial3D
    {
        AlbedoColor = new Color(0.86f, 0.85f, 0.83f),
        Transparency = BaseMaterial3D.TransparencyEnum.Alpha,
        ShadingMode = BaseMaterial3D.ShadingModeEnum.PerPixel,
        VertexColorUseAsAlbedo = true,
        // 입자끼리 서로를 가리며 깜빡이는 것을 막는다.
        DepthDrawMode = BaseMaterial3D.DepthDrawModeEnum.OpaqueOnly,
        DisableReceiveShadows = true,
    };

    /// <summary>
    /// 굴뚝 연기가 실제로 붙었고 **배율이 상쇄되었는지** 확인한다.
    ///
    /// 정지 화면으로는 "연기가 없다"와 "연기가 너무 작다"와 "연기가 30m 위에
    /// 있다"를 구분할 수 없다. 셋 다 겪었다. 숫자로 본다.
    /// </summary>
    /// <param name="expected">
    /// 로더가 붙였다고 보고한 개수. 필드에는 집이 없어 0 이 정상이므로
    /// 상수로 박지 않고 로더에게 묻는다.
    /// </param>
    public static int SelfTest(Node root, int expected)
    {
        var found = new System.Collections.Generic.List<GpuParticles3D>();
        Collect(root, found);

        if (found.Count != expected)
        {
            GD.Print($"[smoke] 실패 굴뚝 {found.Count}개 (기대 {expected}개)");
            return 1;
        }
        if (expected == 0)
        {
            GD.Print("[smoke] OK   이 리전에는 굴뚝 연기가 없다");
            return 0;
        }

        int notEmitting = 0;
        float worstScale = 0.0f;
        foreach (GpuParticles3D p in found)
        {
            if (!p.Emitting)
            {
                notEmitting++;
            }
            // 부모 배율을 상쇄했으면 월드 배율이 1 이어야 한다. 6 이면
            // 속도와 중력까지 6배가 되어 연기가 하늘로 치솟는다.
            float world = p.GlobalBasis.Scale.X;
            worstScale = Mathf.Max(worstScale, Mathf.Abs(world - 1.0f));
        }

        bool ok = notEmitting == 0 && worstScale < 0.05f;
        GD.Print($"[smoke] {(ok ? "OK  " : "실패")} 굴뚝 {found.Count}개, "
                 + $"멈춘 것 {notEmitting}개 (기대 0), "
                 + $"월드 배율 오차 최대 {worstScale:F3} (기대 < 0.05)");
        return ok ? 0 : 1;
    }

    private static void Collect(Node node, System.Collections.Generic.List<GpuParticles3D> into)
    {
        if (node is GpuParticles3D particles)
        {
            into.Add(particles);
        }
        foreach (Node child in node.GetChildren())
        {
            Collect(child, into);
        }
    }

    /// <summary>0 에서 1 로 커지는 커브. 입자가 피어오르며 부푼다.</summary>
    private static CurveTexture Ramp(float from, float to)
    {
        var curve = new Curve();
        curve.AddPoint(new Vector2(0, from));
        curve.AddPoint(new Vector2(1, to));
        return new CurveTexture { Curve = curve };
    }

    /// <summary>솟자마자 진해졌다가 서서히 사라진다.</summary>
    private static CurveTexture Fade()
    {
        var curve = new Curve();
        curve.AddPoint(new Vector2(0.0f, 0.0f));
        curve.AddPoint(new Vector2(0.18f, 0.92f));
        curve.AddPoint(new Vector2(1.0f, 0.0f));
        return new CurveTexture { Curve = curve };
    }
}
