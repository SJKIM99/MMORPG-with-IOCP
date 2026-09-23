using Client.World;
using Godot;

namespace Client.Player;

/// <summary>
/// 플레이어 캐릭터와 3인칭 카메라를 만든다.
///
/// 모델은 manifest 의 <c>chr_player_knight</c> 를 쓴다. 직업 선택이 붙으면
/// 이 에셋 ID 만 바꾸면 되도록 밖에서 받는다 — 컨트롤러는 모델을 모른다.
///
/// 스폰 좌표는 리전 JSON 의 <c>spawn_point</c> 다. 서버가 붙으면 서버가
/// 정해 주는 값으로 바뀐다 (CLAUDE.md 9장).
/// </summary>
public static class PlayerSpawner
{
    /// <summary>캐릭터 캡슐 크기. KayKit 캐릭터는 키 2.2~2.6m 로 큼직하다.</summary>
    private const float CapsuleHeight = 2.2f;
    private const float CapsuleRadius = 0.42f;

    public static (PlayerController Player, ThirdPersonCamera Camera) Spawn(
        Node parent, Manifest manifest, Region region, string assetId = "chr_player_knight")
    {
        var player = new PlayerController { Name = "Player" };

        // 캡슐 중심이 원점이라 절반 올려야 발이 바닥에 닿는다.
        player.AddChild(new CollisionShape3D
        {
            Name = "Body",
            Shape = new CapsuleShape3D { Height = CapsuleHeight, Radius = CapsuleRadius },
            Position = new Vector3(0, CapsuleHeight * 0.5f, 0),
        });

        // 지형 삼각형과 스치며 끼는 것을 막는다.
        player.FloorMaxAngle = Mathf.DegToRad(52.0f);
        player.FloorSnapLength = 0.5f;
        player.SafeMargin = 0.02f;

        parent.AddChild(player);
        player.GlobalPosition = SpawnPosition(region);

        // 모델은 컨트롤러 아래에 둔다. 컨트롤러가 회전하지 않고 모델만 돌린다
        // — 캡슐이 같이 돌면 벽에 끼는 양상이 방향마다 달라진다.
        Node3D? model = LoadModel(manifest, assetId);
        CharacterAnimator? animator = null;
        if (model is not null)
        {
            animator = new CharacterAnimator { Name = "Animator" };
            player.AddChild(animator);
            player.AttachModel(model, animator);
            if (!animator.Bind(model))
            {
                animator = null;
            }
        }

        var camera = new ThirdPersonCamera { Name = "PlayerCamera" };
        parent.AddChild(camera);
        camera.SetTarget(player);
        player.CameraPivot = camera;

        GD.Print($"[player] {assetId} 스폰 {player.GlobalPosition}  애니메이션={(animator is not null)}");
        return (player, camera);
    }

    private static Vector3 SpawnPosition(Region region)
    {
        float[] p = region.SpawnPoint;
        // 지형/콜리전이 아직 안 올라왔을 수 있으니 살짝 띄워 떨어뜨린다.
        return new Vector3(p[0], p[1] + 1.2f, p[2]);
    }

    private static Node3D? LoadModel(Manifest manifest, string assetId)
    {
        if (!manifest.Assets.ContainsKey(assetId))
        {
            GD.PushWarning($"[player] manifest 에 없는 에셋: {assetId}");
            return null;
        }

        string path = manifest.ResourcePath(assetId);
        var scene = ResourceLoader.Load<PackedScene>(path);
        if (scene is null)
        {
            GD.PushWarning($"[player] 모델을 열 수 없다: {path}");
            return null;
        }

        var model = scene.Instantiate<Node3D>();
        model.Name = "Model";
        model.Scale = Vector3.One * manifest.Assets[assetId].Scale;

        // 모델 앞면은 +Z 다. 카메라는 뒤(+Z)에서 보므로 그대로 두면
        // 가만히 서 있을 때 캐릭터가 카메라를 쳐다본다. 등을 보이게 돌려 둔다.
        model.Rotation = new Vector3(0, Mathf.Pi, 0);
        return model;
    }
}
