using System.Globalization;
using Godot;

namespace Client.World;

/// <summary>
/// 커맨드라인으로 한 장 찍고 종료한다. 배치와 배율을 GUI 없이 확인하려고 만든 개발 도구다.
///
///   godot --path Client -- --shot out.png
///   godot --path Client -- --region field_01 --shot out.png --eye 256,8,40 --target 256,2,120
///
/// --eye / --target 을 주지 않으면 리전 전경을 잡는다.
/// 헤드리스(--headless)에서는 렌더링이 없어 동작하지 않는다. 창 모드로 실행해야 한다.
/// </summary>
public sealed class ScreenshotRequest
{
    public string Path { get; init; } = "";
    public Vector3? Eye { get; init; }
    public Vector3? Target { get; init; }
    public int WarmupFrames { get; init; } = 12;

    /// <summary>인자가 없으면 null. 있으면 요청을 만든다.</summary>
    public static ScreenshotRequest? Parse(string[] args)
    {
        string? path = Value(args, "--shot");
        if (path is null)
        {
            return null;
        }

        return new ScreenshotRequest
        {
            Path = path,
            Eye = Vec(Value(args, "--eye")),
            Target = Vec(Value(args, "--target")),
            WarmupFrames = int.TryParse(Value(args, "--warmup"), out int n) ? n : 12,
        };
    }

    private static string? Value(string[] args, string name)
    {
        for (int i = 0; i < args.Length - 1; i++)
        {
            if (args[i] == name)
            {
                return args[i + 1];
            }
        }
        return null;
    }

    private static Vector3? Vec(string? text)
    {
        if (text is null)
        {
            return null;
        }

        string[] parts = text.Split(',');
        if (parts.Length != 3)
        {
            GD.PushWarning($"[shot] 좌표 형식이 x,y,z 가 아니다: {text}");
            return null;
        }

        var v = new float[3];
        for (int i = 0; i < 3; i++)
        {
            if (!float.TryParse(parts[i], NumberStyles.Float, CultureInfo.InvariantCulture, out v[i]))
            {
                GD.PushWarning($"[shot] 숫자가 아니다: {parts[i]}");
                return null;
            }
        }
        return new Vector3(v[0], v[1], v[2]);
    }
}
