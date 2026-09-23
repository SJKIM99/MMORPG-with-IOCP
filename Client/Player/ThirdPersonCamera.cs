using Godot;
using Godot.Collections;

namespace Client.Player;

/// <summary>
/// 3인칭 궤도 카메라.
///
/// <see cref="SpringArm3D"/> 를 쓰지 않는다. 스프링암은 막히면 길이를 0 까지
/// 줄여 버려서, 벽을 등지고 카메라를 돌리면 **1인칭이 된다.** 최소 거리를
/// 보장할 방법이 없다. 그래서 레이캐스트로 직접 계산한다.
///
/// 최소 거리를 지키느라 카메라가 벽에 살짝 파고들 수 있는데,
/// 1인칭으로 튀는 것보다 낫다. 상용 게임도 대개 이쪽을 택한다.
/// </summary>
public partial class ThirdPersonCamera : Node3D
{
    [Export] public float Distance { get; set; } = 7.0f;

    /// <summary>이보다 가까워지지 않는다. 캐릭터가 항상 보이는 거리.</summary>
    [Export] public float MinDistance { get; set; } = 2.6f;

    [Export] public float MaxDistance { get; set; } = 16.0f;

    /// <summary>바라보는 지점의 높이. 캐릭터 가슴께.</summary>
    [Export] public float FocusHeight { get; set; } = 1.6f;

    /// <summary>벽에 닿았을 때 그만큼 앞으로 당긴다. 근평면이 뚫리지 않게.</summary>
    [Export] public float WallPadding { get; set; } = 0.45f;

    [Export] public float Sensitivity { get; set; } = 0.0045f;
    [Export] public float MinPitch { get; set; } = -1.15f;
    [Export] public float MaxPitch { get; set; } = 0.62f;
    [Export] public float FollowSpeed { get; set; } = 18.0f;

    /// <summary>벽에서 멀어질 때 다시 늘어나는 속도. 갑자기 튀지 않게.</summary>
    [Export] public float ExtendSpeed { get; set; } = 6.0f;

    private Node3D? _target;
    private Camera3D _camera = null!;
    private float _yaw;
    private float _pitch = -0.28f;
    private float _currentDistance;
    private Vector3 _focus;

    public Camera3D Camera => _camera;

    public override void _Ready()
    {
        _camera = new Camera3D { Name = "Camera", Current = true, Far = 4000.0f };
        AddChild(_camera);
        _currentDistance = Distance;
        Input.MouseMode = Input.MouseModeEnum.Captured;
    }

    public void SetTarget(Node3D target)
    {
        _target = target;
        _focus = target.GlobalPosition + Vector3.Up * FocusHeight;
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (@event is InputEventMouseMotion motion
            && Input.MouseMode == Input.MouseModeEnum.Captured)
        {
            _yaw -= motion.Relative.X * Sensitivity;
            _pitch = Mathf.Clamp(_pitch - motion.Relative.Y * Sensitivity, MinPitch, MaxPitch);
        }
        else if (@event is InputEventMouseButton { Pressed: true } button)
        {
            if (button.ButtonIndex == MouseButton.WheelUp)
            {
                Distance = Mathf.Max(MinDistance, Distance - 0.8f);
            }
            else if (button.ButtonIndex == MouseButton.WheelDown)
            {
                Distance = Mathf.Min(MaxDistance, Distance + 0.8f);
            }
        }
        else if (@event is InputEventKey { Pressed: true, Keycode: Key.Escape })
        {
            // 창 밖으로 빠져나올 수단이 없으면 개발 중에 곤란하다.
            Input.MouseMode = Input.MouseMode == Input.MouseModeEnum.Captured
                ? Input.MouseModeEnum.Visible
                : Input.MouseModeEnum.Captured;
        }
    }

    public override void _Process(double delta)
    {
        if (_target is null)
        {
            return;
        }

        float dt = (float)delta;

        // 플레이어는 몸체가 아니라 **모델이 보이는 위치**를 따라간다.
        // 계단을 오르면 몸체는 즉시 순간이동하는데 카메라가 그걸 따라가면
        // 한 단마다 화면이 튄다.
        Vector3 wanted = _target is PlayerController player
            ? player.VisualPosition
            : _target.GlobalPosition;
        wanted += Vector3.Up * FocusHeight;

        _focus = _focus.Lerp(wanted, Mathf.Min(1.0f, FollowSpeed * dt));

        // 시선 방향에서 뒤로 물러난 지점이 목표 위치다.
        var basis = new Basis(Vector3.Up, _yaw) * new Basis(Vector3.Right, _pitch);
        Vector3 back = basis * Vector3.Back;

        float target = Mathf.Clamp(WallDistance(_focus, back, Distance), MinDistance, Distance);

        // 줄어들 때는 즉시(벽에 박히지 않게), 늘어날 때는 천천히.
        _currentDistance = target < _currentDistance
            ? target
            : Mathf.MoveToward(_currentDistance, target, ExtendSpeed * dt);

        GlobalPosition = _focus + back * _currentDistance;
        LookAt(_focus, Vector3.Up);
    }

    /// <summary>초점에서 뒤로 쏴서 벽까지의 거리를 잰다. 막히지 않으면 요청한 거리.</summary>
    private float WallDistance(Vector3 focus, Vector3 back, float wanted)
    {
        var query = PhysicsRayQueryParameters3D.Create(focus, focus + back * wanted);
        if (_target is CollisionObject3D body)
        {
            // 플레이어 자신에 막히면 카메라가 바로 얼굴에 붙는다.
            query.Exclude = new Array<Rid> { body.GetRid() };
        }

        Dictionary hit = GetWorld3D().DirectSpaceState.IntersectRay(query);
        if (hit.Count == 0)
        {
            return wanted;
        }

        return focus.DistanceTo((Vector3)hit["position"]) - WallPadding;
    }
}
