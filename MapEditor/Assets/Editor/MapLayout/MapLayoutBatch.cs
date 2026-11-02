// ============================================================================
// ファイルの役割: コマンドラインから、読み込みと書き出しを確かめている（Unity をバッチモードで起動して使う）。
//   Unity.exe -batchmode -projectPath MapEditor -executeMethod MapLayout.Editor.MapLayoutBatch.Check -outputDir <フォルダ> -quit
// ・1面と2面の配置ファイルを読み込み、何も動かさずに書き出して、元のファイルと1文字も違わないかを比べている。
// ・続けて、上から見下ろした画像を撮り、出力のフォルダへ保存している（読み込んだ配置が正しい形かを目で確かめる）。
// 違いがあれば、終了コード1で終わる。
// ============================================================================

using System;
using System.IO;
using System.Text;
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
            EditorApplication.Exit(failed ? 1 : 0);
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
