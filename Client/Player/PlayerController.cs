using Godot;

namespace Client.Player;

/// <summary>
/// 3인칭 플레이어 이동.
///
/// **이 이동은 임시다.** CLAUDE.md 9장에서 이동 권위는 서버에 있고,
/// 클라이언트가 보내는 것은 입력(방향)뿐이다. 서버가 붙으면
/// <see cref="Intent"/> 를 패킷으로 보내고 서버가 돌려준 좌표로 보정한다.
///
/// 그래서 세 단계를 일부러 분리해 두었다:
///   1. <see cref="ReadIntent"/>   입력 → 이동 의도 (서버로 보낼 것)
///   2. <see cref="ApplyIntent"/>  의도 → 속도/위치 (서버도 같은 식으로 검증)
///   3. 표시                        모델 회전·애니메이션 (클라 전용)
///
/// 서버 연결 시 2단계가 예측이 되고, 서버 좌표와 어긋나면 3단계만 보간한다.
/// </summary>
public partial class PlayerController : CharacterBody3D
{
    /// <summary>클라이언트가 서버로 보낼 것. 결과 좌표가 아니라 의도다.</summary>
    public readonly struct Intent
    {
        /// <summary>월드 XZ 평면의 단위 이동 방향. 정지면 영벡터.</summary>
        public Vector2 Move { get; init; }
        public bool Sprint { get; init; }
        public bool Jump { get; init; }
    }

    [Export] public float WalkSpeed { get; set; } = 4.2f;
    [Export] public float SprintSpeed { get; set; } = 7.8f;

    // 14 / 18 로 두었더니 "너무 미끄럽다"는 지적을 받았다.
    // 전속력까지 0.3초, 180도 전환에 0.6초가 걸렸다.
    // 액션 MMO 는 훨씬 즉각적이다 — 전속력 0.08초, 전환 0.12초로 맞췄다.
    [Export] public float Acceleration { get; set; } = 50.0f;
    [Export] public float Friction { get; set; } = 60.0f;

    /// <summary>
    /// 이미 달리는 중에 **방향만 바꿀 때** 쓰는 가속도.
    /// 가속과 같은 값이면 반대로 꺾을 때 한 번 미끄러진 뒤에야 돌아선다.
    /// </summary>
    [Export] public float TurnAcceleration { get; set; } = 90.0f;
    /// <summary>
    /// 점프 초속도. 도달 높이는 v² / 2g 이므로 **높이를 2배로 하려면 √2 배**다.
    /// 5.2 (약 0.58m) → 7.4 (약 1.16m).
    /// </summary>
    [Export] public float JumpVelocity { get; set; } = 7.4f;
    /// <summary>모델이 진행 방향으로 돌아서는 속도. 낮으면 게걸음처럼 보인다.</summary>
    [Export] public float TurnSpeed { get; set; } = 20.0f;

    /// <summary>
    /// 점프 없이 올라갈 수 있는 턱 높이.
    ///
    /// 계단 한 단보다 커야 하는 건 당연하고, **경사진 도로의 포석 턱**도 넘어야 한다.
    /// 4m 포석 타일은 평면이라 16% 경사 구간에서 타일마다 0.69m 단차가 생긴다.
    /// 0.45 로 두었더니 남쪽 계단 앞 도로에서 막혔다.
    /// 캐릭터 키가 2.2m 이므로 0.75 는 키의 34% — 난간(1.1m)이나 상자(1.05m)는
    /// 여전히 못 올라간다.
    /// </summary>
    [Export] public float MaxStepHeight { get; set; } = 0.75f;

    /// <summary>턱을 넘을 때 앞으로 재 보는 거리. 캡슐 반지름보다 커야 걸치지 않는다.</summary>
    [Export] public float StepProbeDistance { get; set; } = 0.55f;

    /// <summary>턱을 오른 뒤 모델이 따라붙는 속도(m/s). 낮을수록 부드럽고 굼뜨다.</summary>
    [Export] public float StepSmoothSpeed { get; set; } = 6.0f;

    /// <summary>이동 방향을 정하는 기준. 3인칭에서는 카메라 방향이다.</summary>
    [Export] public Node3D? CameraPivot { get; set; }

    private float _stepOffsetY;
    private float _gravity = 24.0f;
    private Node3D? _model;
    private CharacterAnimator? _animator;

    public float PlanarSpeed => new Vector2(Velocity.X, Velocity.Z).Length();

    /// <summary>
    /// 모델이 실제로 보이는 위치. 턱을 오른 직후에는 몸체보다 낮다.
    /// 카메라가 이것을 따라가야 계단마다 화면이 같이 튀지 않는다.
    /// </summary>
    public Vector3 VisualPosition => GlobalPosition + new Vector3(0, _stepOffsetY, 0);

    /// <summary>NPC 가 플레이어를 찾을 때 쓰는 그룹 이름.</summary>
    public const string GroupName = "player";

    public override void _Ready()
    {
        // 프로젝트 설정의 중력을 그대로 쓴다. 기본 9.8 은 스타일라이즈 게임에서
        // 너무 둥실거려서 조금 높인다.
        _gravity = (float)ProjectSettings.GetSetting("physics/3d/default_gravity", 9.8) * 2.4f;

        // NPC 가 이 그룹으로 플레이어를 찾는다. 로더가 플레이어를 모르게 두기
        // 위함이다 — 월드와 플레이어는 서로를 참조하지 않는다.
        AddToGroup(GroupName);
    }

    public void AttachModel(Node3D model, CharacterAnimator? animator)
    {
        _model = model;
        _animator = animator;
        AddChild(model);
    }

    // -----------------------------------------------------------------------
    // 1) 입력 → 의도
    // -----------------------------------------------------------------------

    public Intent ReadIntent()
    {
        var raw = new Vector2(
            Held(Key.D) - Held(Key.A),
            Held(Key.S) - Held(Key.W));

        return new Intent
        {
            Move = RotateByCamera(raw),
            Sprint = Input.IsKeyPressed(Key.Shift),
            Jump = Input.IsKeyPressed(Key.Space),
        };
    }

    /// <summary>
    /// 키 입력(화면 기준)을 카메라가 보는 방향 기준의 월드 XZ 방향으로 바꾼다.
    ///
    /// Godot 에서 yaw 회전은 카메라 앞(-Z)을 (-sin, -cos) 로,
    /// 오른쪽(+X)을 (cos, -sin) 으로 보낸다. 따라서
    ///   world = raw.X * 오른쪽 + (-raw.Y) * 앞
    ///         = ( raw.X*cos + raw.Y*sin,  -raw.X*sin + raw.Y*cos )
    ///
    /// 전에는 sin 항의 부호를 둘 다 반대로 써서 yaw=0 에서만 맞았다.
    /// 카메라를 90도 돌리면 W 가 카메라 뒤로 가서 "회전이 반대"로 느껴졌다.
    /// 그래서 이 계산만 따로 빼 두었다 — 검사에서 직접 부른다.
    /// </summary>
    public Vector2 RotateByCamera(Vector2 raw)
    {
        if (raw.LengthSquared() < 0.0001f)
        {
            return Vector2.Zero;
        }

        raw = raw.Normalized();
        float yaw = CameraPivot?.GlobalRotation.Y ?? 0.0f;
        float sin = Mathf.Sin(yaw);
        float cos = Mathf.Cos(yaw);
        return new Vector2(
            raw.X * cos + raw.Y * sin,
            -raw.X * sin + raw.Y * cos);
    }

    // -----------------------------------------------------------------------
    // 2) 의도 → 이동
    // -----------------------------------------------------------------------

    public void ApplyIntent(Intent intent, double delta)
    {
        float dt = (float)delta;
        Vector3 velocity = Velocity;

        if (!IsOnFloor())
        {
            velocity.Y -= _gravity * dt;
        }
        else if (intent.Jump)
        {
            velocity.Y = JumpVelocity;
        }

        float target = intent.Sprint ? SprintSpeed : WalkSpeed;
        var wish = new Vector3(intent.Move.X, 0, intent.Move.Y) * target;
        var planar = new Vector3(velocity.X, 0, velocity.Z);

        // 가속·감속·방향전환을 나눠 둔다. 하나로 묶으면 멈출 때든 꺾을 때든
        // 한 박자 미끄러진다.
        float rate;
        if (intent.Move == Vector2.Zero)
        {
            rate = Friction;
        }
        else if (planar.LengthSquared() > 1.0f
                 && planar.Normalized().Dot(wish.Normalized()) < 0.7f)
        {
            // 이미 달리는 중에 45도 넘게 꺾었다 — 더 세게 붙잡는다.
            rate = TurnAcceleration;
        }
        else
        {
            rate = Acceleration;
        }

        planar = planar.MoveToward(wish, rate * dt);

        Velocity = new Vector3(planar.X, velocity.Y, planar.Z);

        bool wasGrounded = IsOnFloor();
        Vector3 before = GlobalPosition;
        MoveAndSlide();

        // 계단 오르기.
        //
        // CharacterBody3D 에는 계단 처리가 없다. 경사면은 MoveAndSlide 가
        // 알아서 미끄러뜨려 올라가지만, 계단은 수직면이라 그냥 막힌다.
        // 점프 없이 오르려면 "살짝 떠서 앞으로 간 뒤 내려딛는" 동작이 필요하다.
        if (wasGrounded && IsOnWall() && planar.LengthSquared() > 0.25f)
        {
            var movedPlanar = new Vector2(
                GlobalPosition.X - before.X, GlobalPosition.Z - before.Z);
            float wanted = new Vector2(planar.X, planar.Z).Length() * dt;

            // 가려던 거리의 절반도 못 갔으면 뭔가에 막힌 것이다.
            if (movedPlanar.Length() < wanted * 0.5f)
            {
                TryStepUp(planar.Normalized() * StepProbeDistance);
            }
        }
    }

    /// <summary>
    /// 앞이 막혔을 때 한 단 올라가 본다. 성공하면 위치를 옮기고 true.
    ///
    /// 세 번 재 본다: 위로 뜰 공간이 있나 → 그 높이에서 앞으로 갈 수 있나 →
    /// 내려딛을 바닥이 걸을 수 있는 경사인가. 하나라도 아니면 되돌린다.
    /// 마지막 조건이 없으면 벽을 타고 오르게 된다.
    /// </summary>
    private bool TryStepUp(Vector3 horizontal)
    {
        Transform3D start = GlobalTransform;
        var up = new Vector3(0, MaxStepHeight, 0);

        if (TestMove(start, up))
        {
            return false;
        }

        Transform3D raised = start;
        raised.Origin += up;
        if (TestMove(raised, horizontal))
        {
            return false;
        }

        GlobalPosition = raised.Origin + horizontal;

        // 올라온 만큼 + 여유를 두고 내려딛는다.
        KinematicCollision3D? landing = MoveAndCollide(new Vector3(0, -(MaxStepHeight + 0.1f), 0));
        if (landing is null || landing.GetNormal().Dot(Vector3.Up) < Mathf.Cos(FloorMaxAngle))
        {
            GlobalTransform = start;   // 허공이거나 너무 가파르다
            return false;
        }

        // 몸체는 방금 순간이동했다. 모델을 그만큼 내려 두고 서서히 따라오게 한다.
        // 이 보정이 없으면 계단 한 단마다 캐릭터가 깜빡 위로 튄다.
        _stepOffsetY = Mathf.Clamp(
            _stepOffsetY - (GlobalPosition.Y - start.Origin.Y),
            -MaxStepHeight * 1.5f,
            0.0f);
        return true;
    }

    // -----------------------------------------------------------------------
    // 3) 표시 — 클라이언트 전용. 서버는 yaw 하나만 안다 (CLAUDE.md 3장).
    // -----------------------------------------------------------------------

    public void UpdateVisual(double delta)
    {
        if (_model is null)
        {
            return;
        }

        var planar = new Vector2(Velocity.X, Velocity.Z);
        if (planar.LengthSquared() > 0.04f)
        {
            // **KayKit 캐릭터 모델의 앞면은 +Z 다.** yaw 회전은 +Z 를
            // (sin yaw, cos yaw) 로 보내므로 진행 방향 d 를 보려면
            // yaw = atan2(d.x, d.z) 다.
            //
            // Godot 기본값(-Z 앞)으로 알고 + Pi 를 더했더니 180도 뒤집혀서,
            // W 를 눌러 멀어지는데 캐릭터가 카메라를 쳐다봤다.
            float wanted = Mathf.Atan2(planar.X, planar.Y);
            _model.Rotation = new Vector3(
                0,
                Mathf.LerpAngle(_model.Rotation.Y, wanted, TurnSpeed * (float)delta),
                0);
        }

        // 계단을 오르면 몸체는 즉시 올라간다. 모델만 뒤따라오게 해서
        // 순간이동처럼 보이지 않게 한다.
        _stepOffsetY = Mathf.MoveToward(_stepOffsetY, 0.0f, StepSmoothSpeed * (float)delta);
        _model.Position = new Vector3(0, _stepOffsetY, 0);

        _animator?.UpdateLocomotion(PlanarSpeed, IsOnFloor(), WalkSpeed);
    }

    /// <summary>
    /// 설정하면 키보드 대신 이 의도를 쓴다. 자동 검증용 — 계단을 실제로 오르는지
    /// 사람이 키를 누르지 않고 확인하려면 입력을 대신 넣을 수단이 필요하다.
    /// 서버가 붙으면 다른 플레이어를 이 경로로 움직이게 된다.
    /// </summary>
    public Intent? ScriptedIntent { get; set; }

    public override void _PhysicsProcess(double delta)
    {
        Intent intent = ScriptedIntent ?? ReadIntent();
        ApplyIntent(intent, delta);
        UpdateVisual(delta);
    }

    private static float Held(Key key) => Input.IsKeyPressed(key) ? 1.0f : 0.0f;
}
