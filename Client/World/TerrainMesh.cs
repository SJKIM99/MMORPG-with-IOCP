using Godot;

namespace Client.World;

/// <summary>
/// 리전 JSON 의 높이맵으로 지면 메시를 만든다.
///
/// 생성 함수를 다시 돌리지 않고 **저장된 배열을 그대로 읽는다.**
/// 서버의 Recast 입력 메시도 같은 배열에서 나오므로
/// "클라에선 언덕, 서버에선 평지" 가 구조적으로 불가능하다 (CLAUDE.md 5장).
/// </summary>
public static class TerrainMesh
{
    /// <summary>높이맵과 같은 면을 돌려주는 쌍선형 샘플링. Python 쪽과 식이 같다.</summary>
    public static float Sample(Terrain terrain, float size, float x, float z)
    {
        int n = terrain.Resolution - 1;
        float fx = Mathf.Clamp(x, 0, size) / terrain.Cell;
        float fz = Mathf.Clamp(z, 0, size) / terrain.Cell;

        int ix = Mathf.Clamp((int)fx, 0, n);
        int iz = Mathf.Clamp((int)fz, 0, n);
        float tx = fx - ix;
        float tz = fz - iz;

        float h00 = At(terrain, ix, iz);
        float h10 = At(terrain, ix + 1, iz);
        float h01 = At(terrain, ix, iz + 1);
        float h11 = At(terrain, ix + 1, iz + 1);

        return h00 * (1 - tx) * (1 - tz)
             + h10 * tx * (1 - tz)
             + h01 * (1 - tx) * tz
             + h11 * tx * tz;
    }

    private static float At(Terrain terrain, int ix, int iz)
    {
        int n = terrain.Resolution - 1;
        ix = Mathf.Clamp(ix, 0, n);
        iz = Mathf.Clamp(iz, 0, n);
        return terrain.Heights[iz * terrain.Resolution + ix];
    }

    /// <summary>
    /// 지면 메시를 만든다. <paramref name="grass"/> 와 <paramref name="road"/> 를
    /// <c>terrain.surface</c> 로 섞어 **정점 색**에 굽는다.
    ///
    /// 도로를 타일 모델로 깔면 평평한 타일이 경사에서 뜨고 턱이 생긴다.
    /// 지면을 직접 칠하면 틈도 턱도 없다 — 콜리전도 지형 하나로 끝난다.
    /// </summary>
    public static ArrayMesh Build(Terrain terrain, float size, Color grass, Color road)
    {
        int res = terrain.Resolution;
        int n = res - 1;
        float cell = terrain.Cell;
        bool painted = terrain.Surface.Length == res * res;

        var vertices = new Vector3[res * res];
        var normals = new Vector3[res * res];
        var uvs = new Vector2[res * res];
        var colors = new Color[res * res];

        for (int iz = 0; iz < res; iz++)
        {
            for (int ix = 0; ix < res; ix++)
            {
                int i = iz * res + ix;
                float h = terrain.Heights[i];
                vertices[i] = new Vector3(ix * cell, h, iz * cell);
                // UV 를 미터 단위로 두면 타일링 배율이 리전 크기와 무관해진다.
                uvs[i] = new Vector2(ix * cell, iz * cell);

                // 중앙차분으로 법선을 낸다. 가장자리는 한쪽으로 치우쳐도 무방하다.
                float hl = At(terrain, ix - 1, iz);
                float hr = At(terrain, ix + 1, iz);
                float hd = At(terrain, ix, iz - 1);
                float hu = At(terrain, ix, iz + 1);
                normals[i] = new Vector3(hl - hr, 2.0f * cell, hd - hu).Normalized();

                colors[i] = painted ? grass.Lerp(road, terrain.Surface[i]) : grass;
            }
        }

        var indices = new int[n * n * 6];
        int k = 0;
        for (int iz = 0; iz < n; iz++)
        {
            for (int ix = 0; ix < n; ix++)
            {
                int a = iz * res + ix;
                int b = a + 1;
                int c = a + res;
                int d = c + 1;

                // 위에서 내려다볼 때 앞면이 되도록 감는다.
                // 반대로 감으면 지형이 백페이스 컬링으로 통째로 사라지고
                // 그 자리에 하늘(지평선 아래 색)이 비쳐 보인다 — 조명 문제로 착각하기 쉽다.
                indices[k++] = a; indices[k++] = b; indices[k++] = c;
                indices[k++] = b; indices[k++] = d; indices[k++] = c;
            }
        }

        var arrays = new Godot.Collections.Array();
        arrays.Resize((int)Mesh.ArrayType.Max);
        arrays[(int)Mesh.ArrayType.Vertex] = vertices;
        arrays[(int)Mesh.ArrayType.Normal] = normals;
        arrays[(int)Mesh.ArrayType.TexUV] = uvs;
        arrays[(int)Mesh.ArrayType.Color] = colors;
        arrays[(int)Mesh.ArrayType.Index] = indices;

        var mesh = new ArrayMesh();
        mesh.AddSurfaceFromArrays(Mesh.PrimitiveType.Triangles, arrays);
        return mesh;
    }
}
