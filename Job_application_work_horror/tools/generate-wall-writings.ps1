# ============================================================================
# 1面の「懐中電灯で照らすと浮かぶ壁の文字」の画像を生成します。
# 出力: assets/texture/writing_*.png（1024x256、白い文字をアルファ値に持つRGBA）
# 実行: powershell -ExecutionPolicy Bypass -File tools/generate-wall-writings.ps1
#
# 文字は行書体（HGGyoshotai。無ければ游明朝）で描き、1文字ずつ傾き・大きさ・位置をずらして手書き風にします。
# さらに垂れた跡とかすれ（ノイズで削る）を加えます。色はシェーダー側で付けるため、
# 画像はアルファ値（文字の形）だけを使います。乱数の種を固定しているので毎回同じ画像になります。
# ============================================================================

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$source = @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Runtime.InteropServices;

public static class WallWritingGenerator
{
    const int Width = 1024;
    const int Height = 256;

    static float Hash(int x, int y, int seed)
    {
        unchecked
        {
            int h = x * 374761393 + y * 668265263 + seed * 144665;
            h = (h ^ (h >> 13)) * 1274126177;
            h ^= h >> 16;
            return (h & 0xFFFFFF) / (float)0xFFFFFF;
        }
    }

    static float ValueNoise(float x, float y, int seed)
    {
        int ix = (int)Math.Floor(x);
        int iy = (int)Math.Floor(y);
        float fx = x - ix;
        float fy = y - iy;
        fx = fx * fx * (3.0f - 2.0f * fx);
        fy = fy * fy * (3.0f - 2.0f * fy);
        float a = Hash(ix, iy, seed);
        float b = Hash(ix + 1, iy, seed);
        float c = Hash(ix, iy + 1, seed);
        float d = Hash(ix + 1, iy + 1, seed);
        return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
    }

    public static void Generate(string text, string outputPath, int seed)
    {
        Random random = new Random(seed);
        using (Bitmap bitmap = new Bitmap(Width, Height, PixelFormat.Format32bppArgb))
        {
            using (Graphics g = Graphics.FromImage(bitmap))
            {
                g.Clear(Color.FromArgb(0, 255, 255, 255));
                g.SmoothingMode = SmoothingMode.AntiAlias;
                g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;

                FontFamily family;
                // 行書体（Office付属）があれば筆で書いた形に、無ければWindows標準の游明朝を使います。
                try { family = new FontFamily("HGGyoshotai"); }
                catch (ArgumentException) { family = new FontFamily("Yu Mincho"); }

                // 文字数に合わせて大きさを決め、横幅に収めます。
                float emSize = Math.Min(170.0f, 900.0f / Math.Max(text.Length, 1));
                float step = emSize * 1.02f;
                float totalWidth = step * text.Length;
                float x = (Width - totalWidth) * 0.5f;
                float baseY = (Height - emSize) * 0.42f;

                using (SolidBrush brush = new SolidBrush(Color.White))
                using (StringFormat format = StringFormat.GenericTypographic)
                {
                    foreach (char ch in text)
                    {
                        if (ch == ' ')
                        {
                            x += step * 0.55f;
                            continue;
                        }
                        // 1文字ずつ傾け、大きさと高さをずらして急いで書いたような乱れを出します。
                        float angle = (float)(random.NextDouble() * 14.0 - 7.0);
                        float scale = 0.88f + (float)random.NextDouble() * 0.24f;
                        float offsetY = (float)(random.NextDouble() * 22.0 - 11.0);
                        using (GraphicsPath path = new GraphicsPath())
                        {
                            path.AddString(ch.ToString(), family, (int)FontStyle.Bold,
                                emSize, new PointF(0, 0), format);
                            using (Matrix m = new Matrix())
                            {
                                m.Translate(x + step * 0.5f, baseY + emSize * 0.5f + offsetY);
                                m.Rotate(angle);
                                m.Scale(scale, scale);
                                m.Translate(-emSize * 0.5f, -emSize * 0.5f);
                                path.Transform(m);
                            }
                            g.FillPath(brush, path);
                            // 輪郭を太らせ、筆で塗ったような太さにします。
                            using (Pen pen = new Pen(Color.White, 3.0f))
                            {
                                pen.LineJoin = LineJoin.Round;
                                g.DrawPath(pen, path);
                            }
                        }
                        x += step;
                    }
                }
            }

            // 垂れた跡: 文字のある列から下へ、先細りの線と先端の滴を描きます。
            BitmapData data = bitmap.LockBits(new Rectangle(0, 0, Width, Height),
                ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
            byte[] pixels = new byte[data.Stride * Height];
            Marshal.Copy(data.Scan0, pixels, 0, pixels.Length);

            Func<int, int, byte> alphaAt = (px, py) => pixels[py * data.Stride + px * 4 + 3];
            Action<int, int, byte> maxAlpha = (px, py, a) =>
            {
                if (px < 0 || px >= Width || py < 0 || py >= Height) return;
                int i = py * data.Stride + px * 4 + 3;
                if (pixels[i] < a) pixels[i] = a;
            };

            int dripCount = 7 + random.Next(5);
            for (int n = 0; n < dripCount; ++n)
            {
                int column = 40 + random.Next(Width - 80);
                int start = -1;
                for (int py = Height - 1; py >= 0; --py)
                {
                    if (alphaAt(column, py) > 200) { start = py; break; }
                }
                if (start < 0) continue;
                int length = 18 + random.Next(Math.Max(Height - start - 12, 20));
                float width = 3.0f + (float)random.NextDouble() * 3.0f;
                for (int i = 0; i < length && start + i < Height; ++i)
                {
                    float taper = width * (1.0f - 0.55f * i / (float)length);
                    float wobble = (float)Math.Sin((start + i) * 0.11f + n) * 0.8f;
                    for (int dx = -4; dx <= 4; ++dx)
                    {
                        float d = Math.Abs(dx - wobble);
                        if (d <= taper * 0.5f) maxAlpha(column + dx, start + i, 255);
                    }
                }
                // 先端の滴
                int tipY = Math.Min(start + length, Height - 4);
                for (int dy = -4; dy <= 4; ++dy)
                    for (int dx = -4; dx <= 4; ++dx)
                        if (dx * dx + dy * dy <= 12) maxAlpha(column + dx, tipY + dy, 255);
            }

            // かすれ: 2段階のノイズで塗りを削り、壁に染み込んだようなむらを出します。
            for (int py = 0; py < Height; ++py)
            {
                for (int px = 0; px < Width; ++px)
                {
                    int i = py * data.Stride + px * 4;
                    float a = pixels[i + 3] / 255.0f;
                    if (a <= 0.0f) continue;
                    float coarse = ValueNoise(px * 0.035f, py * 0.035f, seed);
                    float fine = ValueNoise(px * 0.22f, py * 0.22f, seed + 17);
                    float keep = coarse * 0.65f + fine * 0.35f;
                    float erosion = Math.Min(Math.Max((keep - 0.22f) / 0.30f, 0.0f), 1.0f);
                    a *= 0.35f + 0.65f * erosion;
                    pixels[i + 0] = 255;
                    pixels[i + 1] = 255;
                    pixels[i + 2] = 255;
                    pixels[i + 3] = (byte)Math.Round(Math.Min(a, 1.0f) * 255.0f);
                }
            }

            Marshal.Copy(pixels, 0, data.Scan0, pixels.Length);
            bitmap.UnlockBits(data);
            bitmap.Save(outputPath, ImageFormat.Png);
        }
    }
}
'@

Add-Type -TypeDefinition $source -ReferencedAssemblies System.Drawing

$outputDirectory = Join-Path $PSScriptRoot '..\assets\texture'
$outputDirectory = [System.IO.Path]::GetFullPath($outputDirectory)

# 文字の内容と、壁に置く場所（Stage1Layout.cpp）の対応
#   warning  : 開始地点の近く（最初のヒューズの奥の壁）
#   three    : 左の倉庫（2本目のヒューズの部屋）
#   power    : 配電盤の近く
#   turn     : ループ廊下。読んで目を離すと turned に変わります
$writings = @(
    @{ Name = 'writing_warning'; Text = 'あかりを けすな'; Seed = 1971 },
    @{ Name = 'writing_three';   Text = 'みっつ もどせば でられる'; Seed = 1972 },
    @{ Name = 'writing_power';   Text = 'でんきが つくと みえなくなる'; Seed = 1973 },
    @{ Name = 'writing_turn';    Text = 'ふりかえるな'; Seed = 1974 },
    @{ Name = 'writing_turned';  Text = 'ふりかえったな'; Seed = 1975 }
)

foreach ($writing in $writings)
{
    $path = Join-Path $outputDirectory ($writing.Name + '.png')
    [WallWritingGenerator]::Generate($writing.Text, $path, $writing.Seed)
    Write-Host "生成: $path"
}
