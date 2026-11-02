// ============================================================================
// ファイルの役割: 配置ファイルをシーンに並べ（読み込み）、動かした結果を配置ファイルへ書き戻している（書き出し）。
// メニュー「Map Layout」から使う。
// ・prop 行（箱の壁・小物）は、配置ファイルの色を付けた箱にしている。位置と大きさ（Scale）を動かせる。
// ・place 行（扉・照明・電池など、進行のコードが作る物の置き場所）は、紫の印と、正面の向きを示す細い棒にしている。
//   位置と、y 軸まわりの回転（yaw）と、size が書いてある物は大きさ（Scale）を動かせる。
// ・spot 行（ヒューズと鍵の候補の場所）は、組ごとにまとめた黄色の球にしている。位置を動かせる。
// ・prop をコピー（Ctrl+D）すると、書き出すときに、元の行をひな形にして新しい行を足している（名前の空白と括弧は _ にする）。
//   シーンから消した物の行は、間違って消さないよう、ファイルに残している（コンソールに知らせる）。
// ============================================================================

using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace MapLayout.Editor
{
    public static class MapLayoutEditor
    {
        // ゲームの配置ファイルのフォルダ（この Unity プロジェクトは、リポジトリの直下の MapEditor にある）
        public static string LayoutDirectory =>
            Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "Job_application_work_horror", "assets", "layout"));

        static readonly Color PlaceColor = new Color(0.75f, 0.25f, 0.95f);
        static readonly Color SpotColor = new Color(1.0f, 0.85f, 0.15f);
        static readonly Color SizedPlaceColor = new Color(0.42f, 0.38f, 0.50f);
        static readonly Color GroundColor = new Color(0.20f, 0.20f, 0.21f);

        [MenuItem("Map Layout/1面を読み込む")]
        static void ImportStage1() => Import(Path.Combine(LayoutDirectory, "stage1_props.txt"));

        [MenuItem("Map Layout/2面を読み込む")]
        static void ImportStage2() => Import(Path.Combine(LayoutDirectory, "stage2_props.txt"));

        [MenuItem("Map Layout/配置ファイルへ書き出す")]
        static void ExportAll()
        {
            foreach (var root in Object.FindObjectsByType<MapLayoutRoot>(FindObjectsSortMode.None))
            {
                Export(root, root.FilePath);
                Debug.Log("配置ファイルへ書き出しました: " + root.FilePath);
            }
        }

        // ---------------------------------------------------------------------
        // 読み込み
        // ---------------------------------------------------------------------
        public static MapLayoutRoot Import(string path)
        {
            var document = MapLayoutDocument.Load(path);
            // ゲームと同じモデルで表示するため、ゲームの FBX を写している（変わった物だけ）
            GameModelCatalog.SyncModels();
            var rootObject = new GameObject("MapLayout " + Path.GetFileNameWithoutExtension(path));
            var root = rootObject.AddComponent<MapLayoutRoot>();
            root.FilePath = path;
            var props = new GameObject("Props").transform;
            var places = new GameObject("Places").transform;
            var spots = new GameObject("Spots").transform;
            props.SetParent(rootObject.transform, false);
            places.SetParent(rootObject.transform, false);
            spots.SetParent(rootObject.transform, false);
            var materials = new Dictionary<string, Material>();

            for (var index = 0; index < document.Lines.Count; ++index)
            {
                var line = document.Lines[index];
                switch (line.Command)
                {
                    case "prop":
                        CreateProp(line, index, props, document, materials);
                        break;
                    case "place":
                        CreatePlace(line, index, places, materials);
                        break;
                    case "spot":
                        CreateSpot(line, index, spots, materials);
                        break;
                }
            }
            Undo.RegisterCreatedObjectUndo(rootObject, "配置ファイルの読み込み");
            return root;
        }

        static Material GetMaterial(Dictionary<string, Material> cache, string key, Color color, Color emission)
        {
            if (!cache.TryGetValue(key, out var material))
            {
                material = new Material(Shader.Find("Standard")) { name = key, color = color };
                if (emission.maxColorComponent > 0.0f)
                {
                    material.EnableKeyword("_EMISSION");
                    material.SetColor("_EmissionColor", emission);
                }
                cache[key] = material;
            }
            return material;
        }

        static Vector3 ReadVector(MapLayoutDocument.Line line, int index)
        {
            return new Vector3(line.Number(index), line.Number(index + 1), line.Number(index + 2));
        }

        static MapLayoutItem AddItem(GameObject target, MapLayoutKind kind, int lineIndex, string key)
        {
            var item = target.AddComponent<MapLayoutItem>();
            item.Kind = kind;
            item.LineIndex = lineIndex;
            item.Key = key;
            return item;
        }

        static void CreateProp(MapLayoutDocument.Line line, int index, Transform parent, MapLayoutDocument document,
            Dictionary<string, Material> materials)
        {
            var name = line.Tokens[1].Text;
            var box = GameObject.CreatePrimitive(PrimitiveType.Cube);
            box.name = name;
            box.transform.SetParent(parent, false);
            box.transform.localPosition = ReadVector(line, 2);
            box.transform.localScale = ReadVector(line, 5);
            var colorName = line.Tokens[8].Text;
            var emissionName = line.Tokens[9].Text;
            document.Colors.TryGetValue(colorName, out var color);
            document.Colors.TryGetValue(emissionName, out var emission);
            var surface = GetMaterial(materials, colorName + "/" + emissionName, color, emission);
            box.GetComponent<MeshRenderer>().sharedMaterial = surface;
            AddItem(box, MapLayoutKind.Prop, index, name);

            // model の指定があれば、箱の代わりにゲームと同じモデルを、ゲームと同じ伸ばし方・回し方で表示している（Wall::RebuildVisualModel）
            var modelIndex = line.IndexOf("model", 12);
            if (modelIndex >= 0 && GameModelCatalog.WallModels.TryGetValue(line.Tokens[modelIndex + 1].Text, out var model))
            {
                var size = box.transform.localScale;
                var orientation = Quaternion.identity;
                var nominal = model.Size;
                if (model.FollowsLongestAxis)
                {
                    var absolute = new Vector3(Mathf.Abs(size.x), Mathf.Abs(size.y), Mathf.Abs(size.z));
                    if (absolute.z > absolute.x && absolute.z >= absolute.y)
                    {
                        // モデルの x を z へ
                        orientation = Quaternion.Euler(0.0f, -90.0f, 0.0f);
                        nominal = new Vector3(model.Size.z, model.Size.y, model.Size.x);
                    }
                    else if (absolute.y > absolute.x && absolute.y > absolute.z)
                    {
                        // モデルの x を y へ
                        orientation = Quaternion.Euler(0.0f, 0.0f, 90.0f);
                        nominal = new Vector3(model.Size.y, model.Size.x, model.Size.z);
                    }
                }
                if (AttachModel(box.transform, model.AssetPath, new Vector3(1.0f / nominal.x, 1.0f / nominal.y, 1.0f / nominal.z),
                        orientation, Vector3.zero, surface, materials))
                {
                    box.GetComponent<MeshRenderer>().enabled = false;
                }
            }
        }

        // FBX を、大きさをそろえる子（scale）と、向きをそろえる孫（rotation）の下に置いている。
        // ゲームは「モデルを回す → 標準の大きさで割る → 置いた大きさを掛ける」の順なので、割る分を子の scale にしている。
        // 部位の色は、ゲームの表の色にし、配置ファイルの色で塗る部位（Main・Lens）は surface にしている
        static bool AttachModel(Transform parent, string assetPath, Vector3 normalize, Quaternion orientation, Vector3 offset,
            Material surface, Dictionary<string, Material> materials)
        {
            var prefab = GameModelCatalog.LoadModel(assetPath);
            if (prefab == null)
            {
                Debug.LogWarning("モデルを読めませんでした: " + assetPath);
                return false;
            }
            var scaler = new GameObject("モデルの大きさ").transform;
            scaler.SetParent(parent, false);
            scaler.localPosition = offset;
            scaler.localScale = normalize;
            var instance = (GameObject)PrefabUtility.InstantiatePrefab(prefab, scaler);
            instance.transform.localRotation = orientation * GameModelCatalog.AxisCorrection;
            var partColors = GameModelCatalog.PartColors(assetPath);
            foreach (var renderer in instance.GetComponentsInChildren<Renderer>())
            {
                var shared = renderer.sharedMaterials;
                for (var slot = 0; slot < shared.Length; ++slot)
                {
                    var part = shared[slot] != null ? shared[slot].name : string.Empty;
                    if (surface != null && (part == "Main" || part == "Lens"))
                    {
                        shared[slot] = surface;
                    }
                    else if (partColors.TryGetValue(part, out var color))
                    {
                        shared[slot] = GetMaterial(materials, "#part/" + assetPath + "/" + part, color, Color.black);
                    }
                }
                renderer.sharedMaterials = shared;
            }
            return true;
        }

        static void CreatePlace(MapLayoutDocument.Line line, int index, Transform parent, Dictionary<string, Material> materials)
        {
            var name = line.Tokens[1].Text;
            var placeObject = new GameObject(name);
            placeObject.transform.SetParent(parent, false);
            placeObject.transform.localPosition = ReadVector(line, 2);
            var item = AddItem(placeObject, MapLayoutKind.Place, index, name);
            var yaw = line.IndexOf("yaw", 5);
            if (yaw >= 0)
            {
                item.HasYaw = true;
                placeObject.transform.localRotation = Quaternion.Euler(0.0f, line.Number(yaw + 1), 0.0f);
            }
            var size = line.IndexOf("size", 5);
            if (size >= 0)
            {
                item.HasSize = true;
                placeObject.transform.localScale = ReadVector(line, size + 1);
            }
            // 印（大きさのある物は、その大きさの箱。ない物は小さな箱）と、正面（+Z）の向きを示す細い棒。
            // 大きさのある物（肖像画・時計・目印・傷など、色をコードが決める板）は、目立ちすぎないよう落ち着いた色にしている
            var material = item.HasSize
                ? GetMaterial(materials, "#placeSized", SizedPlaceColor, Color.black)
                : GetMaterial(materials, "#place", PlaceColor, PlaceColor * 0.4f);
            var marker = GameObject.CreatePrimitive(PrimitiveType.Cube);
            marker.name = "印";
            Object.DestroyImmediate(marker.GetComponent<Collider>());
            marker.transform.SetParent(placeObject.transform, false);
            marker.transform.localScale = item.HasSize ? Vector3.one : new Vector3(3.0f, 3.0f, 3.0f);
            marker.GetComponent<MeshRenderer>().sharedMaterial = material;
            var facing = GameObject.CreatePrimitive(PrimitiveType.Cube);
            facing.name = "正面";
            Object.DestroyImmediate(facing.GetComponent<Collider>());
            facing.transform.SetParent(placeObject.transform, false);
            facing.transform.localPosition = new Vector3(0.0f, 0.0f, item.HasSize ? 0.75f : 4.0f);
            facing.transform.localScale = item.HasSize ? new Vector3(0.1f, 0.1f, 0.5f) : new Vector3(0.5f, 0.5f, 5.0f);
            facing.GetComponent<MeshRenderer>().sharedMaterial = material;

            // 床（Ground）は、50×50の板を大きさで伸ばしている（ゲームの床は、置いた大きさの50倍の広さ）。印の代わりに実際の広さで表示している
            if (GameModelCatalog.PlaceTypes.TryGetValue(name, out var placeType) && placeType == "Ground")
            {
                marker.transform.localScale = new Vector3(50.0f, 0.02f, 50.0f);
                marker.GetComponent<MeshRenderer>().sharedMaterial = GetMaterial(materials, "#ground", GroundColor, Color.black);
                facing.SetActive(false);
                return;
            }

            // 種類の分かる物（扉・照明・電池・配電盤・鍵・ヒューズ・人影・ロッカー・バルブ・裸電球・配管）は、ゲームと同じモデルで表示している。
            // size の書いてある物は、置いた大きさをモデルの標準の大きさで割った割合で伸ばしている
            var model = GameModelCatalog.FindPlaceModel(name);
            if (model != null)
            {
                var normalize = item.HasSize
                    ? new Vector3(1.0f / model.Nominal.x, 1.0f / model.Nominal.y, 1.0f / model.Nominal.z)
                    : Vector3.one;
                if (AttachModel(placeObject.transform, model.AssetPath, normalize,
                        Quaternion.Euler(0.0f, model.ExtraYaw, 0.0f), model.Offset, null, materials))
                {
                    marker.SetActive(false);
                }
            }
        }

        static void CreateSpot(MapLayoutDocument.Line line, int index, Transform parent, Dictionary<string, Material> materials)
        {
            var group = line.Tokens[1].Text;
            var groupTransform = parent.Find(group);
            if (groupTransform == null)
            {
                groupTransform = new GameObject(group).transform;
                groupTransform.SetParent(parent, false);
            }
            var sphere = GameObject.CreatePrimitive(PrimitiveType.Sphere);
            sphere.name = group + " " + (groupTransform.childCount + 1);
            sphere.transform.SetParent(groupTransform, false);
            sphere.transform.localPosition = ReadVector(line, 2);
            sphere.transform.localScale = new Vector3(3.0f, 3.0f, 3.0f);
            sphere.GetComponent<MeshRenderer>().sharedMaterial = GetMaterial(materials, "#spot", SpotColor, SpotColor * 0.5f);
            AddItem(sphere, MapLayoutKind.Spot, index, group);
        }

        // ---------------------------------------------------------------------
        // 書き出し
        // ---------------------------------------------------------------------
        public static void Export(MapLayoutRoot root, string path)
        {
            var document = MapLayoutDocument.Load(root.FilePath);
            var items = root.GetComponentsInChildren<MapLayoutItem>(true);
            var usedLines = new HashSet<int>();
            var appended = new List<(int after, MapLayoutDocument.Line line)>();
            var names = new HashSet<string>(items.Select(item => item.Key));

            foreach (var item in items)
            {
                var transform = item.transform;
                var position = transform.position - root.transform.position;
                if (item.LineIndex < 0 || item.LineIndex >= document.Lines.Count)
                {
                    continue;
                }
                var template = document.Lines[item.LineIndex];
                // 同じ行から作られた2つ目以降（シーンでコピーした prop）は、元の行をひな形にした新しい行にしている
                var line = template;
                if (!usedLines.Add(item.LineIndex))
                {
                    if (item.Kind != MapLayoutKind.Prop)
                    {
                        Debug.LogWarning("place と spot のコピーは書き出していません（名前で引くため）: " + item.name);
                        continue;
                    }
                    line = MapLayoutDocument.Line.Parse(template.Content.TrimEnd());
                    var name = MakeUniqueName(item.name, names);
                    names.Add(name);
                    line.SetToken(1, name);
                    appended.Add((item.LineIndex, line));
                }

                line.SetNumber(2, position.x);
                line.SetNumber(3, position.y);
                line.SetNumber(4, position.z);
                switch (item.Kind)
                {
                    case MapLayoutKind.Prop:
                        line.SetNumber(5, transform.lossyScale.x);
                        line.SetNumber(6, transform.lossyScale.y);
                        line.SetNumber(7, transform.lossyScale.z);
                        break;
                    case MapLayoutKind.Place:
                        WritePlaceRotationAndSize(line, transform);
                        break;
                }
            }

            // 足した行は、元の行のすぐ後ろへ入れている（後ろの行から入れ、行番号がずれないようにしている）
            foreach (var (after, line) in appended.OrderByDescending(entry => entry.after))
            {
                document.Lines.Insert(after + 1, line);
            }
            for (var index = 0; index < document.Lines.Count; ++index)
            {
                var command = document.Lines[index].Command;
                if ((command == "prop" || command == "place" || command == "spot") && !usedLines.Contains(index) &&
                    !appended.Any(entry => ReferenceEquals(entry.line, document.Lines[index])))
                {
                    Debug.LogWarning("シーンにない行は、そのまま残しています: " + document.Lines[index].Text.Trim());
                }
            }
            document.Save(path);
        }

        static void WritePlaceRotationAndSize(MapLayoutDocument.Line line, Transform transform)
        {
            var yaw = Mathf.DeltaAngle(0.0f, transform.eulerAngles.y);
            var yawIndex = line.IndexOf("yaw", 5);
            if (yawIndex >= 0)
            {
                line.SetNumber(yawIndex + 1, yaw);
            }
            else if (Mathf.Abs(yaw) > 0.01f)
            {
                line.Append("yaw " + MapLayoutDocument.FormatNumber(yaw));
            }

            var scale = transform.lossyScale;
            var sizeIndex = line.IndexOf("size", 5);
            if (sizeIndex >= 0)
            {
                line.SetNumber(sizeIndex + 1, scale.x);
                line.SetNumber(sizeIndex + 2, scale.y);
                line.SetNumber(sizeIndex + 3, scale.z);
            }
            else if ((scale - Vector3.one).sqrMagnitude > 1e-6f)
            {
                line.Append("size " + MapLayoutDocument.FormatNumber(scale.x) + " " +
                    MapLayoutDocument.FormatNumber(scale.y) + " " + MapLayoutDocument.FormatNumber(scale.z));
            }
        }

        // 配置ファイルの名前に使えるよう、空白と括弧を _ にし、重ならない名前にしている（"Shelf (1)" → "Shelf_1"）
        static string MakeUniqueName(string name, HashSet<string> used)
        {
            var baseName = Regex.Replace(name.Trim(), @"[\s()]+", "_").Trim('_');
            var candidate = baseName;
            for (var number = 2; used.Contains(candidate); ++number)
            {
                candidate = baseName + "_" + number;
            }
            return candidate;
        }

        // 新しいシーンを開いている（バッチの確認用）
        public static void NewEmptyScene()
        {
            EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
        }
    }
}
