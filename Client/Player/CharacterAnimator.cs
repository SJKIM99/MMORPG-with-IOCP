using Godot;

namespace Client.Player;

/// <summary>
/// 캐릭터 애니메이션. Idle / Walk / Run 만 다룬다.
///
/// KayKit 캐릭터 .glb 에는 애니메이션이 0개고, 클립은 별도 파일
/// <c>Rig_Medium_*.glb</c> 에 있다. Adventurers 와 Skeletons 가 같은
/// <c>Rig_Medium</c> 을 쓰므로 **라이브러리 한 벌을 플레이어와 몬스터가 공유한다.**
/// (world/manifest.json 의 animation_libraries 참조)
///
/// FREE 티어에는 공격 모션이 없다. 전투가 붙으면 그때 해결한다 —
/// manifest 의 animation_libraries.$missing.attack 에 적어 두었다.
/// </summary>
public partial class CharacterAnimator : Node
{
    private const string Library = "rig_medium";

    private static readonly string[] AnimationSources =
    {
        "res://assets/kaykit_skeletons/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb",
        "res://assets/kaykit_skeletons/Animations/gltf/Rig_Medium/Rig_Medium_General.glb",
    };

    private static bool _reported;

    private AnimationPlayer? _player;
    private string _current = "";

    /// <summary>라이브러리를 붙인다. 실패하면 false — 애니메이션 없이도 이동은 된다.</summary>
    public bool Bind(Node3D character)
    {
        // 캐릭터 .glb 는 애니메이션이 0개라 Godot 이 AnimationPlayer 를 만들지 않는다.
        // 직접 붙인다. RootNode 기본값은 ".." — 부모(= 캐릭터 루트)다.
        // 클립의 트랙 경로가 원본 파일에서도 씬 루트 기준이라 그대로 맞는다.
        _player = FindPlayer(character);
        if (_player is null)
        {
            _player = new AnimationPlayer { Name = "AnimationPlayer" };
            character.AddChild(_player);
        }

        var library = new AnimationLibrary();
        int added = 0;

        foreach (string path in AnimationSources)
        {
            var scene = ResourceLoader.Load<PackedScene>(path);
            if (scene is null)
            {
                continue;
            }

            Node root = scene.Instantiate();
            AnimationPlayer? source = FindPlayer(root);
            if (source is not null)
            {
                foreach (string name in source.GetAnimationList())
                {
                    Animation? clip = source.GetAnimation(name);
                    if (clip is null || library.HasAnimation(name))
                    {
                        continue;
                    }
                    library.AddAnimation(name, clip);
                    added++;
                }
            }
            root.QueueFree();
        }

        if (added == 0)
        {
            GD.PushWarning("[anim] 클립을 하나도 못 읽었다");
            return false;
        }

        _player.AddAnimationLibrary(Library, library);

        // 트랙 경로가 이 캐릭터에서 실제로 풀리는지 확인한다.
        // 리그가 같아도 노드 이름이 다르면 조용히 아무 일도 일어나지 않는다.
        int resolved = CountResolvedTracks(library);
        // NPC 13명이 각자 붙이므로 매번 찍으면 로그가 도배된다. 리그가 같아
        // 결과도 같으니 처음 한 번만 남긴다.
        if (!_reported)
        {
            _reported = true;
            GD.Print($"[anim] 클립 {added}개, 첫 클립 트랙 {resolved}개 연결");
        }
        if (resolved == 0)
        {
            GD.PushWarning("[anim] 트랙이 하나도 연결되지 않았다 — 노드 경로 불일치");
            return false;
        }
        return true;
    }

    private int CountResolvedTracks(AnimationLibrary library)
    {
        if (_player is null)
        {
            return 0;
        }

        Node root = _player.GetNode(_player.RootNode);
        foreach (StringName name in library.GetAnimationList())
        {
            Animation? clip = library.GetAnimation(name);
            if (clip is null)
            {
                continue;
            }

            int ok = 0;
            for (int i = 0; i < clip.GetTrackCount(); i++)
            {
                NodePath path = clip.TrackGetPath(i);
                if (root.GetNodeOrNull(new NodePath(path.GetConcatenatedNames())) is not null)
                {
                    ok++;
                }
            }
            return ok;   // 첫 클립만 봐도 충분하다
        }
        return 0;
    }

    /// <summary>속도에 따라 Idle / Walk / Run 을 고른다.</summary>
    public void UpdateLocomotion(float speed, bool onFloor, float walkSpeed)
    {
        if (!onFloor)
        {
            Play("Jump_Idle", 0.15f);
            return;
        }

        if (speed < 0.35f)
        {
            Play("Idle_A", 0.25f);
        }
        else if (speed < walkSpeed * 1.15f)
        {
            Play("Walking_A", 0.18f);
        }
        else
        {
            Play("Running_A", 0.18f);
        }
    }

    public void Play(string clip, float blend = 0.2f)
    {
        if (_player is null || _current == clip)
        {
            return;
        }

        string full = $"{Library}/{clip}";
        if (!_player.HasAnimation(full))
        {
            return;
        }

        Animation? anim = _player.GetAnimation(full);
        if (anim is not null)
        {
            anim.LoopMode = Animation.LoopModeEnum.Linear;
        }

        _player.Play(full, blend);
        _current = clip;
    }

    private static AnimationPlayer? FindPlayer(Node node)
    {
        if (node is AnimationPlayer player)
        {
            return player;
        }
        foreach (Node child in node.GetChildren())
        {
            AnimationPlayer? found = FindPlayer(child);
            if (found is not null)
            {
                return found;
            }
        }
        return null;
    }
}
