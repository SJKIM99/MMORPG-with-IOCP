using Godot;

namespace Client.World;

/// <summary>
/// 배치 확인용 자유 비행 카메라. 개발 도구이지 게임 플레이가 아니다.
/// 플레이어 카메라는 서버 권위 이동이 붙을 때 따로 만든다.
///
/// 우클릭 유지 = 마우스 룩 / WASD = 이동 / Q,E = 하강,상승
/// Shift = 가속 / 마우스 휠 = 기본 속도 조절
/// </summary>
public partial class FlyCamera : Camera3D
{
    [Export] public float Speed { get; set; } = 24.0f;
    [Export] public float BoostMultiplier { get; set; } = 5.0f;
    [Export] public float MouseSensitivity { get; set; } = 0.0025f;

    private float _yaw;
    private float _pitch;
    private bool _looking;

    public override void _Ready()
    {
        SyncRotation();
        // 512m 월드 전경이 잘리지 않도록 원경 클리핑을 넉넉히 둔다.
        Far = 4000.0f;
    }

    /// <summary>
    /// 밖에서 Rotation 이나 LookAt 으로 방향을 바꾼 뒤 호출한다.
    /// 캐시해 둔 yaw/pitch 를 현재 값에 맞춰야 다음 마우스 입력에서 튀지 않는다.
    /// </summary>
    public void SyncRotation()
    {
        _yaw = Rotation.Y;
        _pitch = Rotation.X;
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (@event is InputEventMouseButton button)
        {
            if (button.ButtonIndex == MouseButton.Right)
            {
                _looking = button.Pressed;
                Input.MouseMode = _looking ? Input.MouseModeEnum.Captured : Input.MouseModeEnum.Visible;
            }
            else if (button.Pressed && button.ButtonIndex == MouseButton.WheelUp)
            {
                Speed = Mathf.Min(Speed * 1.15f, 400.0f);
            }
            else if (button.Pressed && button.ButtonIndex == MouseButton.WheelDown)
            {
                Speed = Mathf.Max(Speed / 1.15f, 1.0f);
            }
        }
        else if (@event is InputEventMouseMotion motion && _looking)
        {
            _yaw -= motion.Relative.X * MouseSensitivity;
            _pitch = Mathf.Clamp(
                _pitch - motion.Relative.Y * MouseSensitivity,
                -Mathf.Pi / 2 + 0.01f,
                Mathf.Pi / 2 - 0.01f);
            Rotation = new Vector3(_pitch, _yaw, 0);
        }
        else if (@event is InputEventKey key && key.Pressed && key.Keycode == Key.Escape)
        {
            _looking = false;
            Input.MouseMode = Input.MouseModeEnum.Visible;
        }
    }

    public override void _Process(double delta)
    {
        var direction = new Vector3(
            Held(Key.D) - Held(Key.A),
            Held(Key.E) - Held(Key.Q),
            Held(Key.S) - Held(Key.W));

        if (direction == Vector3.Zero)
        {
            return;
        }

        float speed = Speed * (Input.IsKeyPressed(Key.Shift) ? BoostMultiplier : 1.0f);
        Position += (Transform.Basis * direction.Normalized()) * speed * (float)delta;
    }

    private static float Held(Key key) => Input.IsKeyPressed(key) ? 1.0f : 0.0f;
}
