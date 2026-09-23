using Client.Player;
using Godot;

namespace Client.World;

/// <summary>
/// 마을 NPC 한 명. **클라이언트 전용 연출만** 한다.
///
/// 하는 일은 두 가지뿐이다:
///   1. 역할에 맞는 애니메이션을 재생한다 (JSON 의 animation 필드).
///   2. 플레이어가 가까이 오면 그쪽으로 고개를 돌린다.
///
/// 하지 않는 것: 이동. 배회는 내비메시 위 wander FSM 이고 그건 Week 2 에
/// 몬스터와 **같은 코드**로 만든다 (CLAUDE.md 8장 — idle_movement: wander).
/// 여기서 클라 전용으로 만들면 서버가 이동 권위를 가져가는 순간 버린다(9장).
///
/// 시선은 게임플레이에 영향을 주지 않으므로 클라가 정해도 된다 — 이펙트가
/// 클라 전용인 것과 같은 이유다(6장). 서버는 NPC 가 누굴 보는지 알 필요가 없다.
/// </summary>
public partial class NpcActor : Node3D
{
    /// <summary>이 거리 안에 플레이어가 들어오면 쳐다본다.</summary>
    [Export] public float LookRadius { get; set; } = 7.0f;

    /// <summary>고개를 돌리는 속도(rad/s). 낮으면 굼뜨고 높으면 홱 돈다.</summary>
    [Export] public float TurnSpeed { get; set; } = 3.5f;

    /// <summary>배치 JSON 이 준 원래 방향. 플레이어가 멀어지면 여기로 돌아온다.</summary>
    public float HomeYaw { get; set; }

    /// <summary>역할별 대기 동작. 값이 없으면 Idle_A.</summary>
    public string IdleClip { get; set; } = "Idle_A";

    /// <summary>
    /// 가끔 바꿔 재생할 동작들. 배치 JSON 의 idle_variants 다.
    ///
    /// 13명이 전부 같은 클립을 같은 위상으로 돌리면 인형 진열대처럼 보인다.
    /// 움직이지 않아도 하는 일이 바뀌면 살아 있어 보인다.
    /// </summary>
    public string[] IdleVariants { get; set; } = System.Array.Empty<string>();

    private CharacterAnimator? _animator;
    private Node3D? _player;
    private bool _searched;
    private RandomNumberGenerator _rng = null!;
    private float _nextSwitch;

    public void Bind(Node3D model)
    {
        // NPC 별 고정 시드. 볼 때마다 동작 순서가 달라지면 스크린샷 비교가 안 된다.
        _rng = new RandomNumberGenerator { Seed = (ulong)Name.ToString().GetHashCode() };
        _nextSwitch = _rng.RandfRange(2.0f, DwellMax);

        var animator = new CharacterAnimator { Name = "Animator" };
        AddChild(animator);
        if (animator.Bind(model))
        {
            _animator = animator;
            _animator.Play(IdleClip, 0.0f);
        }
        else
        {
            // 붙이지 못하면 T-포즈로 서 있게 된다. 조용히 넘어가면
            // "왜 13명이 전부 팔 벌리고 있지" 를 나중에 다시 추적해야 한다.
            GD.PushWarning($"[npc] 애니메이션을 붙이지 못했다: {Name}");
            animator.QueueFree();
        }
    }

    /// <summary>동작을 바꾸기까지 기다리는 시간의 상한(초).</summary>
    private const float DwellMax = 13.0f;

    public override void _Process(double delta)
    {
        SwitchClipOccasionally((float)delta);

        // 플레이어는 나중에 생길 수도 있고(--spectator 면 영영 없다) 죽을 수도 있다.
        // 매 프레임 트리를 뒤지지 않도록 한 번만 찾고, 사라지면 다시 찾지 않는다.
        if (!_searched)
        {
            _searched = true;
            _player = GetTree().GetFirstNodeInGroup(PlayerController.GroupName) as Node3D;
        }

        float wanted = HomeYaw;
        if (GodotObject.IsInstanceValid(_player))
        {
            Vector3 to = _player!.GlobalPosition - GlobalPosition;
            to.Y = 0.0f;
            if (to.LengthSquared() <= LookRadius * LookRadius && to.LengthSquared() > 0.01f)
            {
                // KayKit 캐릭터의 앞면은 **+Z** 다. 플레이어 모델에서 이미 겪은
                // 함정이라 여기서도 atan2(x, z) 를 그대로 쓴다.
                wanted = Mathf.Atan2(to.X, to.Z);
            }
        }

        float yaw = Mathf.LerpAngle(Rotation.Y, wanted, (float)delta * TurnSpeed);
        Rotation = new Vector3(0, yaw, 0);
    }

    private void SwitchClipOccasionally(float delta)
    {
        if (_animator is null || IdleVariants.Length == 0)
        {
            return;
        }

        _nextSwitch -= delta;
        if (_nextSwitch > 0.0f)
        {
            return;
        }
        _nextSwitch = _rng.RandfRange(5.0f, DwellMax);

        // 기본 동작으로 돌아오는 빈도를 높인다. 계속 물건만 집고 있으면
        // 그것대로 이상하다.
        int pick = _rng.RandiRange(-1, IdleVariants.Length - 1);
        _animator.Play(pick < 0 ? IdleClip : IdleVariants[pick], 0.35f);
    }
}
