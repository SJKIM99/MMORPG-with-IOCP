using Client.Player;
using Client.World;
using Godot;

namespace Client;

/// <summary>
/// 월드 배치 확인용 진입점. 서버 연결이 붙기 전까지의 뷰어다.
///
/// 조명·환경·카메라도 코드로 만든다. 씬 파일에 손으로 놓지 않는다는
/// CLAUDE.md 5장 원칙을 클라이언트 쪽에서도 지키기 위해서다.
///
/// 조작 — WASD 이동, 마우스 시점, Shift 달리기, Space 점프, Esc 마우스 잠금 해제
/// 1 = 마을 / 2 = 필드 / R = 다시 로드 / Tab = 관전(자유 비행) 전환 / F = 전경(관전 중에만)
/// </summary>
[Tool]
public partial class Main : Node3D
{
    private static readonly string[] Regions = { "town", "field_01" };

    private FlyCamera _camera = null!;
    private WorldLoader? _world;
    private int _index;

    private PlayerController? _player;
    private ThirdPersonCamera? _playerCamera;
    private bool _spectator;

    private bool _moveTest;
    private bool _selfTest;
    private int _selfTestFailures;
    private ScreenshotRequest? _shot;
    private int _shotFrames;
    private bool _shotTaken;

    public override void _Ready()
    {
        GD.Print($"Client boot | Godot {Engine.GetVersionInfo()["string"]} | .NET {System.Environment.Version}");

        // 씬 루트는 반드시 단위 변환이어야 한다.
        //
        // 에디터 3D 뷰포트에서 루트를 실수로 끌면 그 변환이 씬 파일에 저장되고
        // **월드 전체가 기울어진다.** 실제로 약 0.6도 회전이 들어가 있었고,
        // 원점에서 130m 떨어진 광장에서 높이가 0.6m 어긋났다.
        // 눈으로는 알아채기 어렵고 "계단이 떠 보인다" 같은 증상으로만 나타난다.
        if (!Transform.IsEqualApprox(Transform3D.Identity))
        {
            GD.PushWarning("[main] 씬 루트에 변환이 걸려 있다 — 월드가 기울어진다. "
                           + $"단위 변환으로 되돌린다. (저장된 값: {Transform})");
            Transform = Transform3D.Identity;
        }

        string[] args = OS.GetCmdlineUserArgs();
        _shot = ScreenshotRequest.Parse(args);
        _selfTest = System.Array.IndexOf(args, "--selftest") >= 0;
        _moveTest = System.Array.IndexOf(args, "--movetest") >= 0;
        // 자유 비행 카메라로 시작한다. --eye/--target 으로 원하는 곳을 찍을 때 쓴다.
        _spectator = System.Array.IndexOf(args, "--spectator") >= 0;

        SetupEnvironment();
        SetupCamera();
        LoadRegion(StartRegionFromCommandLine());

        if (_shot?.Eye is { } eye)
        {
            _camera.Position = eye;
            _camera.LookAt(_shot.Target ?? Vector3.Zero, Vector3.Up);
            _camera.SyncRotation();
        }
    }

    public override void _Process(double delta)
    {
        if (_shot is null || _shotTaken || ++_shotFrames < _shot.WarmupFrames)
        {
            return;
        }

        _shotTaken = true;
        RunChecksThenCapture(_shot.Path);
    }

    /// <summary>요청된 자가진단을 먼저 돌리고 나서 화면을 찍는다.</summary>
    private async void RunChecksThenCapture(string path)
    {
        if (_selfTest && _world is not null)
        {
            _selfTestFailures += CollisionSelfTest.Run(this, _world.Region);
            _selfTestFailures += await Spinner.SelfTest(_world, GetTree(), _world.SpinningCount);
            _selfTestFailures += Smoke.SelfTest(_world, _world.SmokeCount);
            _selfTestFailures += WaterMesh.SelfTest(
                _world.WaterHoles, _world.WaterCells, _world.WaterMeshBuilt);
        }

        if (_moveTest && _player is not null)
        {
            _selfTestFailures += await MovementSelfTest.Run(
                _player, GetTree(), _world?.Region.Id ?? "");
        }

        if (_player is not null)
        {
            GD.Print($"[player] 위치 {_player.GlobalPosition}  접지={_player.IsOnFloor()}  " +
                     $"속도 {_player.Velocity.Length():F2}");
        }

        CaptureAndQuit(path);
    }

    private async void CaptureAndQuit(string path)
    {
        // 이번 프레임의 그리기가 끝난 뒤에 백버퍼를 읽어야 빈 화면이 찍히지 않는다.
        await ToSignal(RenderingServer.Singleton, RenderingServer.SignalName.FramePostDraw);

        Image image = GetViewport().GetTexture().GetImage();
        Error error = image.SavePng(path);
        GD.Print(error == Error.Ok
            ? $"[shot] 저장: {path} ({image.GetWidth()}x{image.GetHeight()})"
            : $"[shot] 실패: {error}");

        GetTree().Quit(error != Error.Ok || _selfTestFailures > 0 ? 1 : 0);
    }

    /// <summary>커맨드라인에서 "--name value" 의 value 를 꺼낸다. 없으면 null.</summary>
    private static string? OptionValue(string name)
    {
        string[] args = OS.GetCmdlineUserArgs();
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == name)
            {
                return args[i + 1];
            }
        }
        return null;
    }

    /// <summary>
    /// godot -- --region field_01  으로 시작 리전을 고른다.
    /// 헤드리스 검증과 자동화용이며, 없으면 마을로 시작한다.
    /// </summary>
    private static int StartRegionFromCommandLine()
    {
        string[] args = OS.GetCmdlineUserArgs();
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (args[i] != "--region")
            {
                continue;
            }

            int found = System.Array.IndexOf(Regions, args[i + 1]);
            if (found >= 0)
            {
                return found;
            }

            GD.PushWarning($"[main] 알 수 없는 리전: {args[i + 1]}");
        }
        return 0;
    }

    public override void _UnhandledInput(InputEvent @event)
    {
        if (@event is not InputEventKey { Pressed: true, Echo: false } key)
        {
            return;
        }

        switch (key.Keycode)
        {
            case Key.Key1: LoadRegion(0); break;
            case Key.Key2: LoadRegion(1); break;
            case Key.R: LoadRegion(_index); break;
            case Key.Tab: ToggleSpectator(); break;
            case Key.F:
                if (_spectator)
                {
                    FrameRegion();
                }
                break;
        }
    }

    // -----------------------------------------------------------------------

    private void LoadRegion(int index)
    {
        _index = index;

        if (_world is not null)
        {
            RemoveChild(_world);
            _world.QueueFree();
        }
        if (_player is not null)
        {
            _player.QueueFree();
            _player = null;
        }
        if (_playerCamera is not null)
        {
            _playerCamera.QueueFree();
            _playerCamera = null;
        }

        _world = new WorldLoader
        {
            Name = "World",
            RegionId = Regions[index],
            // --wind off 로 끌 수 있다. 흔들림이 형태를 망가뜨리는지 가릴 때 쓴다.
            WindEnabled = OptionValue("--wind") != "off",
            CollisionEnabled = OptionValue("--collision") != "off",
        };
        AddChild(_world);

        if (_spectator)
        {
            FrameRegion();
        }
        else
        {
            SpawnPlayer();
        }
    }

    /// <summary>플레이어를 스폰하고 3인칭 카메라로 전환한다.</summary>
    private void SpawnPlayer()
    {
        if (_world is null)
        {
            return;
        }

        (_player, _playerCamera) = PlayerSpawner.Spawn(this, _world.Manifest, _world.Region);
        _camera.Current = false;
    }

    /// <summary>자유 비행 카메라와 플레이어 시점을 오간다. 배치 확인용.</summary>
    private void ToggleSpectator()
    {
        _spectator = !_spectator;
        if (_spectator)
        {
            _player?.QueueFree();
            _player = null;
            _playerCamera?.QueueFree();
            _playerCamera = null;
            _camera.Current = true;
            Input.MouseMode = Input.MouseModeEnum.Visible;
            FrameRegion();
        }
        else
        {
            SpawnPlayer();
        }
        GD.Print($"[main] {(_spectator ? "관전(자유 비행)" : "플레이어")} 모드");
    }

    /// <summary>리전 전체가 보이는 위치로 카메라를 옮긴다.</summary>
    private void FrameRegion()
    {
        if (_world?.Region is null)
        {
            return;
        }

        float w = _world.Region.Size[0];
        float h = _world.Region.Size[1];
        _camera.Position = new Vector3(w * 0.5f, Mathf.Max(w, h) * 0.55f, h * 1.15f);
        _camera.LookAt(new Vector3(w * 0.5f, 0, h * 0.5f), Vector3.Up);
        _camera.SyncRotation();
        _camera.Speed = Mathf.Max(w, h) / 12.0f;
    }

    private void SetupCamera()
    {
        _camera = new FlyCamera { Name = "FlyCamera", Current = true };
        AddChild(_camera);
    }

    private void SetupEnvironment()
    {
        var sun = new DirectionalLight3D
        {
            Name = "Sun",
            // 오후의 비스듬한 광. 회색박스 단계에서도 형태와 그림자가 읽히는 각도다.
            //
            // 방향광이라 위치는 조명에 영향을 주지 않는다. 다만 원점(0,0,0)에 두면
            // 지형(그 지점 표고 약 19m) 아래에 파묻혀 에디터에서 지도 밑에 조명이
            // 있는 것처럼 보인다. 보기 좋게 하늘로 올려 둔다.
            Position = new Vector3(128, 260, 128),
            RotationDegrees = new Vector3(-38, -128, 0),
            LightColor = new Color("#fff3df"),
            LightEnergy = 1.25f,
            ShadowEnabled = true,
            DirectionalShadowMode = DirectionalLight3D.ShadowMode.Parallel4Splits,
            DirectionalShadowMaxDistance = 380.0f,
            DirectionalShadowBlendSplits = true,
        };
        AddChild(sun);

        // 구름과 태양이 있는 하늘. ProceduralSkyMaterial 에는 구름이 없어 셰이더를 쓴다.
        var shader = GD.Load<Shader>("res://Shaders/sky.gdshader");
        var skyMaterial = new ShaderMaterial { Shader = shader };

        // 태양 방향을 광원 노드에서 직접 계산해 넘긴다.
        //
        // 셰이더의 LIGHT0_DIRECTION 을 쓰면 LIGHT0_ENABLED 가 false 라 태양이
        // 아예 그려지지 않았다. 하늘에 구름만 있고 해가 없던 원인이다.
        // Godot 의 광원은 로컬 -Z 로 비추므로 '태양 쪽' 은 +Z 축이다.
        skyMaterial.SetShaderParameter("sun_direction", sun.GlobalBasis.Z.Normalized());

        var environment = new Godot.Environment
        {
            BackgroundMode = Godot.Environment.BGMode.Sky,
            Sky = new Sky { SkyMaterial = skyMaterial },
            // 하늘이 밝아서 앰비언트를 올리면 중간톤이 바로 날아간다.
            // 잔디가 하얗게 뜨는 원인이었다. 기본값 근처를 유지한다.
            AmbientLightSource = Godot.Environment.AmbientSource.Sky,
            AmbientLightSkyContribution = 0.35f,
            AmbientLightEnergy = 0.8f,

            // white 를 키우면 그만큼 전체가 어두워진다. 1.0 근처를 벗어나지 말 것.
            TonemapMode = Godot.Environment.ToneMapper.Filmic,
            TonemapWhite = 1.0f,
            TonemapExposure = 1.0f,

            // 원경을 옅게 날려 512m 필드에서 거리감이 생기게 한다.
            //
            // FogDensity 는 블렌드 비율이 아니라 지수 감쇠 계수다 (기본 0.01).
            // 0.55 를 넣으면 몇 미터 앞부터 화면이 안개로 덮여 잔디가 회색이 된다.
            // Depth 모드에서는 begin/end 가 실제 거리를 정하므로 density 는 낮게 둔다.
            FogEnabled = true,
            FogMode = Godot.Environment.FogModeEnum.Depth,
            FogLightColor = new Color("#b9cfdd"),
            FogDepthBegin = 180.0f,
            FogDepthEnd = 1100.0f,
            FogDepthCurve = 1.4f,
            FogDensity = 0.004f,
            FogSkyAffect = 0.0f,

            SsaoEnabled = true,
            SsaoRadius = 2.0f,
            SsaoIntensity = 1.6f,
        };

        AddChild(new WorldEnvironment { Name = "Environment", Environment = environment });
    }
}
