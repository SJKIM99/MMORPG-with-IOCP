using Godot;

namespace Client.World;

/// <summary>
/// 리전 JSON 의 <c>terrain.water</c> 배열로 수면 메시를 만든다.
///
/// 지면 메시와 **같은 격자**를 쓴다. 따로 만든 평면을 얹으면 물가에서
/// 지형과 어긋나 틈이 벌어진다 — 도로를 타일로 깔았을 때와 같은 문제다.
///
/// 정점 색에 두 가지를 구워 넣는다:
///   R = 수심(m)      — 얕으면 밝고 물가에 거품이 낀다
///   G = 흐름 세기    — 수면이 기울수록 1 에 가깝다. 급류와 폭포가 여기서 나온다
///
/// 수심을 화면공간 깊이 버퍼에서 읽지 않는 이유: 물 위에 뜬 다리와 물레방아
/// 바퀴까지 깊이로 잡혀 그 둘레에 거품 테두리가 생긴다.
/// </summary>
public static class WaterMesh
{
    /// <summary>이 기울기면 흐름 세기가 1 이 된다. 마을 둑이 tan 1.5 다.</summary>
    private const float FullFlowSlope = 0.9f;

    /// <summary>
    /// 정점 색에 수심을 넣을 때 나누는 기준(m). 셰이더가 같은 값으로 되돌린다.
    ///
    /// **정점 색은 RGBA8 로 저장되어 0~1 로 잘린다.** 수심을 미터로 그냥 넣으면
    /// 1m 를 넘는 순간 전부 1.0 이 되어 깊이 정보가 사라진다.
    /// </summary>
    private const float MaxDepth = 4.0f;

    /// <summary>물이 있는 삼각형이 하나도 없으면 null.</summary>
    /// <param name="holes">
    /// 물을 품었는데 꼭짓점 하나가 비어 **그리지 못한** 칸의 수.
    /// 0 이 아니면 화면에 구멍이 뚫려 바닥이 들여다보인다.
    /// </param>
    public static ArrayMesh? Build(Terrain terrain, out int holes)
    {
        holes = 0;
        int res = terrain.Resolution;
        if (terrain.Water.Length != res * res)
        {
            return null;
        }

        float cell = terrain.Cell;
        float none = terrain.NoWater;

        var vertices = new Vector3[res * res];
        var normals = new Vector3[res * res];
        var uvs = new Vector2[res * res];
        var colors = new Color[res * res];

        for (int iz = 0; iz < res; iz++)
        {
            for (int ix = 0; ix < res; ix++)
            {
                int i = iz * res + ix;
                float w = terrain.Water[i];
                bool wet = w > none;

                // 물이 없는 정점도 배열에는 넣는다. 인덱스를 건너뛰며 다시
                // 매기는 것보다 이쪽이 단순하고, 삼각형을 안 만들면 그만이다.
                float y = wet ? w : terrain.Heights[i];
                vertices[i] = new Vector3(ix * cell, y, iz * cell);
                uvs[i] = new Vector2(ix * cell, iz * cell);
                normals[i] = Vector3.Up;

                float depth = wet ? Mathf.Max(0.0f, w - terrain.Heights[i]) : 0.0f;
                colors[i] = new Color(
                    Mathf.Min(depth / MaxDepth, 1.0f),
                    wet ? Flow(terrain, ix, iz, none) : 0.0f,
                    0, 1);
            }
        }

        // 네 꼭짓점이 모두 젖은 칸만 그린다. 하나라도 마르면 그 칸은 물가다.
        // 물길을 팔 때 수면을 물가 밖으로 한 칸 넘겨 뒀으므로 이래도 땅이 비지 않는다.
        var indices = new System.Collections.Generic.List<int>();
        int n = res - 1;
        for (int iz = 0; iz < n; iz++)
        {
            for (int ix = 0; ix < n; ix++)
            {
                int a = iz * res + ix;
                int b = a + 1;
                int c = a + res;
                int d = c + 1;

                if (terrain.Water[a] <= none || terrain.Water[b] <= none
                    || terrain.Water[c] <= none || terrain.Water[d] <= none)
                {
                    // **실제로 물이 잠긴** 꼭짓점이 하나라도 있으면 그려져야 할
                    // 칸이었다 = 구멍이다.
                    //
                    // 판정 기준을 "물 값이 있다"로 두면 안 된다. 물가 줄은
                    // 지형 아래에 잠긴 채 값만 가지고 있어서, 물 밖의 칸까지
                    // 구멍으로 세어 298칸이 나왔다(진짜 구멍은 0칸이었다).
                    if (Submerged(terrain, a) || Submerged(terrain, b)
                        || Submerged(terrain, c) || Submerged(terrain, d))
                    {
                        holes++;
                    }
                    continue;
                }

                // 지면 메시와 같은 방향으로 감는다.
                indices.Add(a); indices.Add(b); indices.Add(c);
                indices.Add(b); indices.Add(d); indices.Add(c);
            }
        }

        if (indices.Count == 0)
        {
            return null;
        }

        var arrays = new Godot.Collections.Array();
        arrays.Resize((int)Mesh.ArrayType.Max);
        arrays[(int)Mesh.ArrayType.Vertex] = vertices;
        arrays[(int)Mesh.ArrayType.Normal] = normals;
        arrays[(int)Mesh.ArrayType.TexUV] = uvs;
        arrays[(int)Mesh.ArrayType.Color] = colors;
        arrays[(int)Mesh.ArrayType.Index] = indices.ToArray();

        var mesh = new ArrayMesh();
        mesh.AddSurfaceFromArrays(Mesh.PrimitiveType.Triangles, arrays);
        return mesh;
    }

    /// <summary>이 꼭짓점이 실제로 물에 잠겼는가. 물가 줄은 지형 아래라 false.</summary>
    private static bool Submerged(Terrain terrain, int i) =>
        terrain.Water[i] > terrain.Heights[i] + 0.02f;

    /// <summary>
    /// 수면 자체의 기울기. 물은 원래 수평이므로 기울어 있다면 흐르는 중이다.
    /// 마을 둑(격자 한 칸에 3m)이 여기서 1.0 으로 나와 폭포가 된다.
    /// </summary>
    private static float Flow(Terrain terrain, int ix, int iz, float none)
    {
        int res = terrain.Resolution;
        float here = terrain.Water[iz * res + ix];
        float worst = 0.0f;

        for (int k = 0; k < 4; k++)
        {
            int jx = ix + (k == 0 ? 1 : k == 1 ? -1 : 0);
            int jz = iz + (k == 2 ? 1 : k == 3 ? -1 : 0);
            if (jx < 0 || jz < 0 || jx >= res || jz >= res)
            {
                continue;
            }

            float other = terrain.Water[jz * res + jx];
            if (other <= none)
            {
                continue;
            }
            worst = Mathf.Max(worst, Mathf.Abs(other - here) / terrain.Cell);
        }

        return Mathf.Clamp(worst / FullFlowSlope, 0.0f, 1.0f);
    }

    /// <summary>
    /// 수면에 구멍이 없는지 확인한다.
    ///
    /// 사용자가 "물이 뚫려 보인다"고 지적한 바로 그 버그다. 원인은
    /// 물가 줄을 4방향으로만 찾아 사각형의 대각선 모서리가 빠진 것이었고,
    /// 지형 생성기 쪽 한 줄이라 클라 코드를 아무리 봐도 안 나온다.
    /// 그리는 쪽에서 세는 것이 가장 확실하다.
    /// </summary>
    public static int SelfTest(int holes)
    {
        bool ok = holes == 0;
        GD.Print($"[water] {(ok ? "OK  " : "실패")} 수면 구멍 {holes}칸 (기대 0칸)");
        if (!ok)
        {
            GD.Print("           -> 물가 줄이 모자라다. terrain.finish_water() 가 "
                     + "대각선 이웃까지 보는지 확인할 것");
        }
        return ok ? 0 : 1;
    }

    /// <summary>수면 머티리얼. 색은 리전 JSON 에서 온다.</summary>
    public static ShaderMaterial Material(WaterInfo info)
    {
        var material = new ShaderMaterial
        {
            Shader = GD.Load<Shader>("res://Shaders/water.gdshader"),
        };
        material.SetShaderParameter("shallow_color", new Color(info.Color));
        material.SetShaderParameter("deep_color", new Color(info.DeepColor));
        material.SetShaderParameter("foam_color", new Color(info.FoamColor));
        material.SetShaderParameter("max_depth", MaxDepth);
        return material;
    }
}
