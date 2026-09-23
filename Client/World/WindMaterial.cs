using System.Collections.Generic;
using Godot;

namespace Client.World;

/// <summary>
/// 식생에 흔들림 셰이더를 입힌다.
///
/// 원본 머티리얼의 albedo 텍스처를 그대로 물려받으므로 색이 변하지 않는다.
/// 같은 텍스처를 쓰는 메시끼리는 ShaderMaterial 을 공유해 드로우콜 상태 변경을 줄인다.
/// </summary>
public static class WindMaterial
{
    private static Shader? _shader;

    // 텍스처(또는 색)별로 한 벌만 만든다. 마을에만 나무·풀이 1000개가 넘는다.
    private static readonly Dictionary<ulong, ShaderMaterial> Cache = new();

    public static void Clear() => Cache.Clear();

    /// <summary>에셋 ID 가 흔들려야 하는 종류인지.</summary>
    public static bool Applies(string assetId) =>
        assetId.StartsWith("prop_tree_")
        || assetId.StartsWith("prop_med_tree_")
        || assetId.StartsWith("prop_bush_")
        || assetId.StartsWith("prop_grass_")
        || assetId.StartsWith("prop_flower_")
        || assetId.StartsWith("prop_clover_")
        || assetId.StartsWith("prop_fern_")
        || assetId.StartsWith("prop_plant_");

    /// <summary>노드 트리를 훑어 모든 MeshInstance3D 의 머티리얼을 바꾼다.</summary>
    public static void Apply(Node node, float strength)
    {
        _shader ??= GD.Load<Shader>("res://Shaders/wind.gdshader");
        if (_shader is null)
        {
            return;
        }

        if (node is MeshInstance3D instance && instance.Mesh is not null)
        {
            for (int i = 0; i < instance.Mesh.GetSurfaceCount(); i++)
            {
                Material? source = instance.Mesh.SurfaceGetMaterial(i);
                instance.SetSurfaceOverrideMaterial(i, Build(source, strength));
            }
        }

        foreach (Node child in node.GetChildren())
        {
            Apply(child, strength);
        }
    }

    private static ShaderMaterial Build(Material? source, float strength)
    {
        Texture2D? texture = null;
        Color color = Colors.White;
        float roughness = 1.0f;
        // 나뭇잎은 알파 컷아웃 쿼드다. 원본의 컷오프를 그대로 물려받지 않으면
        // 잎 모양이 잘리지 않아 사각 덩어리로 보인다.
        float cutoff = 0.0f;

        if (source is StandardMaterial3D std)
        {
            texture = std.AlbedoTexture;
            color = std.AlbedoColor;
            roughness = std.Roughness;
            if (std.Transparency is BaseMaterial3D.TransparencyEnum.AlphaScissor
                                 or BaseMaterial3D.TransparencyEnum.AlphaDepthPrePass)
            {
                cutoff = std.AlphaScissorThreshold;
            }
        }

        // 같은 텍스처 + 같은 세기면 재사용한다.
        ulong key = (texture?.GetRid().Id ?? 0UL) * 397UL
                    ^ (ulong)color.ToRgba32()
                    ^ (ulong)(strength * 1000f)
                    ^ (ulong)(cutoff * 100f) * 31UL;

        if (Cache.TryGetValue(key, out ShaderMaterial? cached))
        {
            return cached;
        }

        var material = new ShaderMaterial { Shader = _shader };
        material.SetShaderParameter("has_texture", texture is not null);
        if (texture is not null)
        {
            material.SetShaderParameter("albedo_texture", texture);
        }
        material.SetShaderParameter("albedo_color", color);
        material.SetShaderParameter("roughness_value", roughness);
        material.SetShaderParameter("alpha_cutoff", cutoff);
        material.SetShaderParameter("sway_strength", strength);

        Cache[key] = material;
        return material;
    }
}
