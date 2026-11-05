// ============================================================================
// ファイルの役割: コマンドラインから、読み込みと書き出しを確かめている（Unity をバッチモードで起動して使う）。
//   Unity.exe -batchmode -projectPath MapEditor -executeMethod MapLayout.Editor.MapLayoutBatch.Check -outputDir <フォルダ> -quit
// ・1面と2面の配置ファイルを読み込み、何も動かさずに書き出して、元のファイルと1文字も違わないかを比べている。
// ・続けて、上から見下ろした画像を撮り、出力のフォルダへ保存している（読み込んだ配置が正しい形かを目で確かめる）。
// 違いがあれば、終了コード1で終わる。
// ============================================================================

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;

namespace MapLayout.Editor
{
    public static class MapLayoutBatch
    {
        public static void Check()
        {
            var outputDirectory = GetArgument("-outputDir") ?? Path.Combine(Application.dataPath, "..", "Temp");
            Directory.CreateDirectory(outputDirectory);
            var failed = false;
            foreach (var fileName in new[] { "stage1_props.txt", "stage2_props.txt" })
            {
                MapLayoutEditor.NewEmptyScene();
                var source = Path.Combine(MapLayoutEditor.LayoutDirectory, fileName);
                var root = MapLayoutEditor.Import(source);
                var exported = Path.Combine(outputDirectory, "roundtrip_" + fileName);
                MapLayoutEditor.Export(root, exported);
                var original = File.ReadAllText(source, Encoding.UTF8);
                var written = File.ReadAllText(exported, Encoding.UTF8);
                var counts = CountItems(root);
                if (original == written)
                {
                    Debug.Log($"MAPLAYOUT OK {fileName}: 読み込んで書き出したファイルが元と同じ（{counts}）");
                }
                else
                {
                    failed = true;
                    Debug.LogError($"MAPLAYOUT DIFF {fileName}: 書き出したファイルが元と違う（{FirstDifference(original, written)}）");
                }
                // ゲームの自動撮影と同じ視点（目の高さは床から40、縦の画角60度）から見た画像。ゲームの画面と見比べる
                foreach (var view in Views)
                {
                    if (view.File == fileName)
                    {
                        RenderView(root, view.Position, view.YawToward, view.Pitch, Path.Combine(outputDirectory, "view_" + view.Name + ".png"));
                    }
                }
                RenderTopView(root, Path.Combine(outputDirectory, "topview_" + Path.GetFileNameWithoutExtension(fileName) + ".png"));
            }

            // 動かして書き出したときに、変えた行だけが変わるか（壁を動かす・小物をコピーして動かす・扉を回す・候補の場所を動かす）
            MapLayoutEditor.NewEmptyScene();
            var editRoot = MapLayoutEditor.Import(Path.Combine(MapLayoutEditor.LayoutDirectory, "stage1_props.txt"));
            foreach (var item in editRoot.GetComponentsInChildren<MapLayoutItem>())
            {
                switch (item.Key)
                {
                    case "Wall1":
                        item.transform.position += new Vector3(0.0f, 0.0f, 10.0f);
                        break;
                    case "PropCabinetLeft":
                        var copy = UnityEngine.Object.Instantiate(item.gameObject, item.transform.parent);
                        copy.name = item.name + " (1)";
                        copy.transform.position += new Vector3(30.0f, 0.0f, 0.0f);
                        break;
                    case "Door":
                        item.transform.rotation = Quaternion.Euler(0.0f, 45.0f, 0.0f);
                        break;
                    case "Stage1FirstFuse":
                        if (item.name.EndsWith(" 1"))
                        {
                            item.transform.position += new Vector3(5.0f, 0.0f, 0.0f);
                        }
                        break;
                }
            }
            var editedPath = Path.Combine(outputDirectory, "edited_stage1_props.txt");
            MapLayoutEditor.Export(editRoot, editedPath);
            var before = File.ReadAllLines(Path.Combine(MapLayoutEditor.LayoutDirectory, "stage1_props.txt"), Encoding.UTF8);
            var after = File.ReadAllLines(editedPath, Encoding.UTF8);
            var changed = new StringBuilder();
            var cursor = 0;
            foreach (var line in after)
            {
                if (cursor < before.Length && before[cursor] == line)
                {
                    ++cursor;
                    continue;
                }
                if (cursor < before.Length && Array.IndexOf(after, before[cursor]) < 0)
                {
                    changed.AppendLine("  - " + before[cursor]);
                    ++cursor;
                }
                changed.AppendLine("  + " + line);
            }
            Debug.Log("MAPLAYOUT EDIT 変わった行:\n" + changed);
            failed |= !CheckRepeatedExport(outputDirectory);
            EditorApplication.Exit(failed ? 1 : 0);
        }

        // 読み込み直さずに何度も書き出す、ふだんの使い方を確かめている（写したファイルで行い、ゲームのファイルは変えない）。
        // 1回目：小物をコピーして書き出す（行が1つ増える）。2回目：別の壁と扉を動かして書き出す。
        // ゲームの PropLayoutFile と同じ決まり（名前が重ならない・知らない指定がない）で読めて、動かした値が正しい行に入るか
        static bool CheckRepeatedExport(string outputDirectory)
        {
            var path = Path.Combine(outputDirectory, "repeated_stage1_props.txt");
            File.Copy(Path.Combine(MapLayoutEditor.LayoutDirectory, "stage1_props.txt"), path, true);
            MapLayoutEditor.NewEmptyScene();
            var root = MapLayoutEditor.Import(path);
            MapLayoutItem Find(string key) => root.GetComponentsInChildren<MapLayoutItem>().First(item => item.Key == key);

            var original = Find("Wall8");
            var copy = UnityEngine.Object.Instantiate(original.gameObject, original.transform.parent);
            copy.name = original.name + " (copy)";
            copy.transform.position += new Vector3(0.0f, 0.0f, 40.0f);
            MapLayoutEditor.Export(root, path);

            Find("Wall9").transform.position += new Vector3(5.0f, 0.0f, 0.0f);
            Find("Door").transform.rotation = Quaternion.Euler(0.0f, 30.0f, 0.0f);
            copy.transform.position += new Vector3(1.0f, 0.0f, 0.0f);
            MapLayoutEditor.Export(root, path);

            var problems = new List<string>();
            var lines = File.ReadAllLines(path, Encoding.UTF8);
            var names = new HashSet<string>();
            string FindLine(string command, string name) =>
                lines.FirstOrDefault(line => Regex.IsMatch(line, "^\\s*" + command + "\\s+" + Regex.Escape(name) + "\\s"));
            for (var index = 0; index < lines.Length; ++index)
            {
                var tokens = Regex.Matches(lines[index].Split('#')[0], @"\S+").Cast<Match>().Select(match => match.Value).ToArray();
                if (tokens.Length < 2 || (tokens[0] != "prop" && tokens[0] != "place"))
                {
                    continue;
                }
                if (!names.Add(tokens[0] + " " + tokens[1]))
                {
                    problems.Add($"{index + 1}行目: 同じ名前が2回ある {tokens[1]}");
                }
                var allowed = tokens[0] == "prop"
                    ? new[] { "noshadow", "weathering", "room", "glow", "model" }
                    : new[] { "yaw", "size" };
                var options = tokens.Skip(tokens[0] == "prop" ? 12 : 5).Where(token => !float.TryParse(token,
                    System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out _));
                foreach (var option in options.Where(option => !allowed.Contains(option) && tokens[0] == "place"))
                {
                    problems.Add($"{index + 1}行目: place の知らない指定 {option}");
                }
                if (tokens[0] == "prop" && tokens.Skip(12).Contains("yaw"))
                {
                    problems.Add($"{index + 1}行目: prop に yaw が書かれた");
                }
            }
            string Expect(string command, string name, Func<string[], bool> check, string what)
            {
                var line = FindLine(command, name);
                var tokens = line == null ? null : Regex.Matches(line.Split('#')[0], @"\S+").Cast<Match>().Select(match => match.Value).ToArray();
                return tokens != null && check(tokens) ? null : $"{name} の {what} が違う: {line}";
            }
            problems.Add(Expect("prop", "Wall8_copy", tokens => tokens[2] == "136" && tokens[4] == "-30", "位置（コピーを2回目に動かした値）"));
            problems.Add(Expect("prop", "Wall8", tokens => tokens[2] == "135" && tokens[4] == "-70", "位置（コピーの元）"));
            problems.Add(Expect("prop", "Wall9", tokens => tokens[2] == "-85", "位置"));
            problems.Add(Expect("prop", "Wall10", tokens => tokens[2] == "90", "位置（動かしていない）"));
            problems.Add(Expect("place", "Door", tokens => tokens.Contains("yaw") && tokens[Array.IndexOf(tokens, "yaw") + 1] == "30", "向き"));
            problems.RemoveAll(problem => problem == null);

            if (problems.Count == 0)
            {
                Debug.Log("MAPLAYOUT REPEAT OK: 読み込み直さずに2回書き出しても、ゲームが読めて、動かした値が正しい行に入っている");
                return true;
            }
            Debug.LogError("MAPLAYOUT REPEAT NG:\n" + string.Join("\n", problems));
            return false;
        }

        // ゲームの CaptureMode と同じ視点（足もとの位置・見る先の x と z・上下の向き（ラジアン、負で下））
        struct View
        {
            public string File;
            public string Name;
            public Vector3 Position;
            public Vector2 YawToward;
            public float Pitch;
        }

        static readonly View[] Views =
        {
            new View { File = "stage1_props.txt", Name = "05d_archive", Position = new Vector3(-62.0f, -99.0f, 115.0f), YawToward = new Vector2(-200.0f, 118.0f), Pitch = -0.12f },
            new View { File = "stage1_props.txt", Name = "05h_southwest_hall", Position = new Vector3(-160.0f, -99.0f, -196.0f), YawToward = new Vector2(-300.0f, -270.0f), Pitch = -0.14f },
            new View { File = "stage2_props.txt", Name = "06_stage2_corridor", Position = new Vector3(0.0f, -99.0f, -118.0f), YawToward = new Vector2(0.0f, 0.0f), Pitch = 0.0f },
        };

        static void RenderView(MapLayoutRoot root, Vector3 foot, Vector2 target, float pitch, string path)
        {
            RenderSettings.ambientMode = UnityEngine.Rendering.AmbientMode.Flat;
            RenderSettings.ambientLight = new Color(0.55f, 0.55f, 0.58f);
            foreach (var sceneLight in UnityEngine.Object.FindObjectsByType<Light>(FindObjectsSortMode.None))
            {
                sceneLight.shadows = LightShadows.None;
            }
            var cameraObject = new GameObject("撮影用のカメラ");
            var camera = cameraObject.AddComponent<Camera>();
            camera.fieldOfView = 60.0f;
            camera.nearClipPlane = 1.0f;
            camera.farClipPlane = 1000.0f;
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(0.05f, 0.05f, 0.06f);
            var eye = foot + new Vector3(0.0f, 40.0f, 0.0f);
            var yaw = Mathf.Atan2(target.x - foot.x, target.y - foot.z) * Mathf.Rad2Deg;
            camera.transform.SetPositionAndRotation(eye, Quaternion.Euler(-pitch * Mathf.Rad2Deg, yaw, 0.0f));

            const int width = 1280;
            const int height = 720;
            var renderTarget = new RenderTexture(width, height, 24);
            camera.targetTexture = renderTarget;
            camera.aspect = (float)width / height;
            camera.Render();
            RenderTexture.active = renderTarget;
            var image = new Texture2D(width, height, TextureFormat.RGB24, false);
            image.ReadPixels(new Rect(0, 0, width, height), 0, 0);
            image.Apply();
            File.WriteAllBytes(path, image.EncodeToPNG());
            RenderTexture.active = null;
            camera.targetTexture = null;
            UnityEngine.Object.DestroyImmediate(renderTarget);
            UnityEngine.Object.DestroyImmediate(cameraObject);
        }

        static string CountItems(MapLayoutRoot root)
        {
            int props = 0, places = 0, spots = 0;
            foreach (var item in root.GetComponentsInChildren<MapLayoutItem>())
            {
                switch (item.Kind)
                {
                    case MapLayoutKind.Prop: ++props; break;
                    case MapLayoutKind.Place: ++places; break;
                    case MapLayoutKind.Spot: ++spots; break;
                }
            }
            return $"prop {props}・place {places}・spot {spots}";
        }

        static string FirstDifference(string a, string b)
        {
            var length = Math.Min(a.Length, b.Length);
            for (var index = 0; index < length; ++index)
            {
                if (a[index] != b[index])
                {
                    var line = a.Substring(0, index).Split('\n').Length;
                    return $"{line}行目";
                }
            }
            return $"長さ {a.Length} と {b.Length}";
        }

        // 置いた物すべてが入るよう、真上から平行投影で撮っている
        static void RenderTopView(MapLayoutRoot root, string path)
        {
            var bounds = new Bounds(root.transform.position, Vector3.zero);
            foreach (var renderer in root.GetComponentsInChildren<Renderer>())
            {
                bounds.Encapsulate(renderer.bounds);
                // 天井の板・照明・配管（床から45より上）は、上から見ると間取りを隠すので、撮るときだけ隠している
                if (renderer.bounds.center.y > -55.0f)
                {
                    renderer.enabled = false;
                }
            }
            var lightObject = new GameObject("撮影用の光");
            var light = lightObject.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = 1.1f;
            lightObject.transform.rotation = Quaternion.Euler(60.0f, -30.0f, 0.0f);

            var cameraObject = new GameObject("撮影用のカメラ");
            var camera = cameraObject.AddComponent<Camera>();
            camera.orthographic = true;
            camera.orthographicSize = Mathf.Max(bounds.extents.z, bounds.extents.x) + 10.0f;
            camera.transform.position = bounds.center + Vector3.up * (bounds.extents.y + 100.0f);
            camera.transform.rotation = Quaternion.Euler(90.0f, 0.0f, 0.0f);
            camera.clearFlags = CameraClearFlags.SolidColor;
            camera.backgroundColor = new Color(0.08f, 0.08f, 0.09f);
            camera.nearClipPlane = 1.0f;
            camera.farClipPlane = bounds.size.y + 300.0f;

            const int size = 1600;
            var target = new RenderTexture(size, size, 24);
            camera.targetTexture = target;
            camera.Render();
            RenderTexture.active = target;
            var image = new Texture2D(size, size, TextureFormat.RGB24, false);
            image.ReadPixels(new Rect(0, 0, size, size), 0, 0);
            image.Apply();
            File.WriteAllBytes(path, image.EncodeToPNG());
            RenderTexture.active = null;
            camera.targetTexture = null;
            UnityEngine.Object.DestroyImmediate(target);
            UnityEngine.Object.DestroyImmediate(cameraObject);
            UnityEngine.Object.DestroyImmediate(lightObject);
        }

        static string GetArgument(string name)
        {
            var arguments = Environment.GetCommandLineArgs();
            for (var index = 0; index + 1 < arguments.Length; ++index)
            {
                if (arguments[index] == name)
                {
                    return arguments[index + 1];
                }
            }
            return null;
        }
    }
}
