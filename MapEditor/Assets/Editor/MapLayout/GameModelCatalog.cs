// ============================================================================
// ファイルの役割: 配置ファイルの物を、ゲームと同じ FBX のモデルで表示するための表を作っている。
// ・モデルの FBX は、ゲームの assets/model から Assets/GameModels へ写している（Git には入れない。元はゲームの側）。
// ・prop 行の model の名前と、FBX・標準の大きさ・長い向きへ回すかは、ゲームの Wall.cpp の表を読んでいる（表を二重に持たない）。
// ・place 行の物の種類（扉・照明・電池など）は、ゲームの Stage1Layout.cpp・Stage2Layout.cpp・Stage3Scene.cpp の builder.Create<種類>("名前") と、
//   名前の配列・作る関数（createDrainValve・createLocker・addCeilingFixture・makeSwitch）から読み取っている。
//   名前が ExtraLight_・ExtraBattery_ で始まる place は、何個でも置ける照明と電池（ゲームの LayoutExtras.h）。
// ・座標の向き：ゲーム（Assimp）は FBX の z を反転して読み、Unity は x を反転して読むので、同じ FBX が
//   y 軸まわりに180度回った向きになる。モデルにはこの分の回転を足している。
// ============================================================================

using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text.RegularExpressions;
using UnityEditor;
using UnityEngine;

namespace MapLayout.Editor
{
    public static class GameModelCatalog
    {
        // Wall.cpp の見た目のモデル（配置ファイルの model の名前で引く）
        public sealed class WallModel
        {
            public string AssetPath;      // Unity の中の FBX
            public Vector3 Size;          // 標準の大きさ（長い物は x が長い向き）
            public bool FollowsLongestAxis;
        }

        // place 行の物の種類ごとのモデル（ゲームの各クラスが読む FBX と、ワールド行列で掛ける標準の拡大率）
        public sealed class PlaceModel
        {
            public string AssetPath;
            public Vector3 Nominal;       // FBX の大きさに対する、そのクラスの標準の拡大率（大きさの割合を求める）
            public float ExtraYaw;        // そのクラスが向きに足している回転（度）
            public Vector3 Offset;        // 置き場所からのずれ（裸電球は、付け根からコードの長さだけ下）
        }

        public const string ModelFolder = "Assets/GameModels";

        static string GameDirectory =>
            Path.GetFullPath(Path.Combine(Application.dataPath, "..", "..", "Job_application_work_horror"));

        // 種類ごとのモデル（Door.cpp・CeilingLight.cpp・BatteryItem.cpp などが読む FBX と拡大率）
        static readonly Dictionary<string, PlaceModel> PlaceModels = new Dictionary<string, PlaceModel>
        {
            { "Door", new PlaceModel { AssetPath = "props/door.fbx", Nominal = new Vector3(30.0f, 50.0f, 4.0f) } },
            { "CeilingLight", new PlaceModel { AssetPath = "props/ceiling_light.fbx", Nominal = new Vector3(22.0f, 2.0f, 10.0f) } },
            { "BatteryItem", new PlaceModel { AssetPath = "props/battery.fbx", Nominal = new Vector3(4.2f, 4.2f, 4.2f) } },
            { "FuseBox", new PlaceModel { AssetPath = "props/fuse_box.fbx", Nominal = new Vector3(12.0f, 18.0f, 4.0f) } },
            { "KeyItem", new PlaceModel { AssetPath = "props/key.fbx", Nominal = new Vector3(4.0f, 4.0f, 4.0f) } },
            { "Item", new PlaceModel { AssetPath = "props/fuse.fbx", Nominal = new Vector3(5.0f, 5.0f, 5.0f) } },
            { "ShadowMan", new PlaceModel { AssetPath = "props/shadow_man.fbx", Nominal = new Vector3(8.0f, 16.0f, 8.0f) } },
            { "DrainValve", new PlaceModel { AssetPath = "props/drain_valve.fbx", Nominal = Vector3.one } },
            // ロッカーの扉の FBX は正面が +X。Locker::Place は、正面の向き（yaw）から90度引いて回している
            { "Locker", new PlaceModel { AssetPath = "props/locker_door.fbx", Nominal = Vector3.one, ExtraYaw = -90.0f } },
            { "HangingBulb", new PlaceModel { AssetPath = "props/hanging_bulb.fbx", Nominal = Vector3.one, Offset = new Vector3(0.0f, -4.0f, 0.0f) } },
        };

        static Dictionary<string, WallModel> s_WallModels;
        static Dictionary<string, string> s_PlaceTypes;

        // ゲームのモデルを Unity の中へ写している（新しい物と、変わった物だけ）
        public static void SyncModels()
        {
            var source = Path.Combine(GameDirectory, "assets", "model");
            var copied = false;
            foreach (var folder in new[] { "props", "pipe" })
            {
                var target = Path.Combine(Application.dataPath, "GameModels", folder);
                Directory.CreateDirectory(target);
                foreach (var file in Directory.GetFiles(Path.Combine(source, folder)))
                {
                    var extension = Path.GetExtension(file).ToLowerInvariant();
                    if (extension != ".fbx" && extension != ".jpg" && extension != ".png")
                    {
                        continue;
                    }
                    var destination = Path.Combine(target, Path.GetFileName(file));
                    if (!File.Exists(destination) || File.GetLastWriteTimeUtc(destination) < File.GetLastWriteTimeUtc(file))
                    {
                        File.Copy(file, destination, true);
                        copied = true;
                    }
                }
            }
            if (copied)
            {
                AssetDatabase.Refresh();
            }
        }

        public static GameObject LoadModel(string relativePath)
        {
            return AssetDatabase.LoadAssetAtPath<GameObject>(ModelFolder + "/" + relativePath);
        }

        // Wall.cpp の表: { "名前", "assets/model/props/xxx.fbx", Vector3(幅, 高さ, 奥行き), true/false, ...
        public static Dictionary<string, WallModel> WallModels
        {
            get
            {
                if (s_WallModels != null)
                {
                    return s_WallModels;
                }
                s_WallModels = new Dictionary<string, WallModel>();
                var text = File.ReadAllText(Path.Combine(GameDirectory, "Wall.cpp"));
                var pattern = new Regex(@"\{\s*""(\w+)"",\s*""assets/model/([^""]+)"",\s*Vector3\(([^)]*)\),\s*(true|false)");
                foreach (Match match in pattern.Matches(text))
                {
                    s_WallModels[match.Groups[1].Value] = new WallModel
                    {
                        AssetPath = match.Groups[2].Value,
                        Size = ParseVector(match.Groups[3].Value),
                        FollowsLongestAxis = match.Groups[4].Value == "true",
                    };
                }
                return s_WallModels;
            }
        }

        // place の名前 → 物の種類（Stage1Layout.cpp・Stage2Layout.cpp から読み取る）
        public static Dictionary<string, string> PlaceTypes
        {
            get
            {
                if (s_PlaceTypes != null)
                {
                    return s_PlaceTypes;
                }
                s_PlaceTypes = new Dictionary<string, string>();
                foreach (var file in new[] { "Stage1Layout.cpp", "Stage2Layout.cpp", "Stage3Scene.cpp" })
                {
                    var text = File.ReadAllText(Path.Combine(GameDirectory, file));
                    // builder.Create<種類>("名前") と、名前の配列を使う builder.Create<種類>(配列[番号])
                    foreach (Match match in Regex.Matches(text, @"builder\.Create<(\w+)>\(([^;]*?)\);"))
                    {
                        var type = match.Groups[1].Value;
                        var argument = match.Groups[2].Value;
                        foreach (Match literal in Regex.Matches(argument, @"""(\w+)"""))
                        {
                            s_PlaceTypes[literal.Groups[1].Value] = type;
                        }
                        var array = Regex.Match(argument, @"^\s*(\w+)\[");
                        if (array.Success)
                        {
                            var declaration = Regex.Match(text, array.Groups[1].Value + @"\[\]\s*=\s*\{([^}]*)\}");
                            foreach (Match literal in Regex.Matches(declaration.Groups[1].Value, @"""(\w+)"""))
                            {
                                s_PlaceTypes[literal.Groups[1].Value] = type;
                            }
                        }
                    }
                    foreach (Match match in Regex.Matches(text, @"createDrainValve\(""(\w+)"""))
                    {
                        s_PlaceTypes[match.Groups[1].Value] = "DrainValve";
                    }
                    foreach (Match match in Regex.Matches(text, @"createLocker\(""(\w+)"""))
                    {
                        s_PlaceTypes[match.Groups[1].Value] = "Locker";
                    }
                    // 1面の部屋の入口の照明（addCeilingFixture）と、3面のスイッチ（makeSwitch。配電盤の形）
                    foreach (Match match in Regex.Matches(text, @"addCeilingFixture\(""(\w+)"""))
                    {
                        s_PlaceTypes[match.Groups[1].Value] = "CeilingLight";
                    }
                    foreach (Match match in Regex.Matches(text, @"makeSwitch\(""(\w+)"""))
                    {
                        s_PlaceTypes[match.Groups[1].Value] = "FuseBox";
                    }
                }
                return s_PlaceTypes;
            }
        }

        static Dictionary<string, Dictionary<string, Color>> s_PartColors;

        // モデルの部位の色（FBX の場所 → 部位の名前 → 色）。ゲームの PropModel::Append の呼び出しと、Wall.cpp の部位の表から読んでいる。
        // 配置ファイルの色で塗る部位（Wall の Main・Lens）は、ここには入れず、呼ぶ側が配置ファイルの色を使う
        public static Dictionary<string, Color> PartColors(string assetPath)
        {
            if (s_PartColors == null)
            {
                s_PartColors = new Dictionary<string, Dictionary<string, Color>>();
                var colorPattern = new Regex(@"\{\s*""(\w+)"",\s*(?:Color\(([^)]*)\)|(\w+))\s*[,}]");
                foreach (var file in Directory.GetFiles(GameDirectory, "*.cpp"))
                {
                    var text = File.ReadAllText(file);
                    // 名前の付いた色（const Color darkMetal(0.10f, ...);）
                    var named = new Dictionary<string, Color>();
                    foreach (Match match in Regex.Matches(text, @"const Color (\w+)\(([^)]*)\);"))
                    {
                        if (TryParseColor(match.Groups[2].Value, out var namedColor))
                        {
                            named[match.Groups[1].Value] = namedColor;
                        }
                    }
                    foreach (Match call in Regex.Matches(text, @"PropModel::Append\(\s*""assets/model/([^""]+)""(.*?)\);", RegexOptions.Singleline))
                    {
                        var parts = new Dictionary<string, Color>();
                        foreach (Match part in colorPattern.Matches(call.Groups[2].Value))
                        {
                            if (part.Groups[2].Success)
                            {
                                if (TryParseColor(part.Groups[2].Value, out var literalColor))
                                {
                                    parts[part.Groups[1].Value] = literalColor;
                                }
                            }
                            else if (named.TryGetValue(part.Groups[3].Value, out var color))
                            {
                                parts[part.Groups[1].Value] = color;
                            }
                        }
                        s_PartColors[call.Groups[1].Value] = parts;
                    }
                }
                // Wall.cpp の部位の表（{ "Binder", Color(...), false } と、HandlePart などの名前の付いた部位）
                var wallParts = new Dictionary<string, Color>();
                var wallText = File.ReadAllText(Path.Combine(GameDirectory, "Wall.cpp"));
                foreach (Match match in Regex.Matches(wallText, @"\{\s*""(\w+)"",\s*Color\(([^)]*)\),\s*false"))
                {
                    if (TryParseColor(match.Groups[2].Value, out var wallColor))
                    {
                        wallParts[match.Groups[1].Value] = wallColor;
                    }
                }
                s_PartColors["#wall"] = wallParts;
            }
            return s_PartColors.TryGetValue(assetPath, out var colors) ? colors : s_PartColors["#wall"];
        }

        // 色の値に式が書いてある所（red.R() * 0.6f など）は読まずに飛ばしている（その部位は FBX の色のまま）
        static bool TryParseColor(string text, out Color color)
        {
            color = Color.white;
            var parts = text.Split(',');
            if (parts.Length < 3 ||
                !float.TryParse(parts[0].Trim().TrimEnd('f'), NumberStyles.Float, CultureInfo.InvariantCulture, out var r) ||
                !float.TryParse(parts[1].Trim().TrimEnd('f'), NumberStyles.Float, CultureInfo.InvariantCulture, out var g) ||
                !float.TryParse(parts[2].Trim().TrimEnd('f'), NumberStyles.Float, CultureInfo.InvariantCulture, out var b))
            {
                return false;
            }
            color = new Color(r, g, b);
            return true;
        }

        public static PlaceModel FindPlaceModel(string placeName)
        {
            // 名前の頭で種類が決まる、何個でも置ける物（ゲームの LayoutExtras.h）。シーンでコピーした物は名前の後ろに番号が付く
            if (placeName.StartsWith("ExtraLight_", System.StringComparison.Ordinal))
            {
                return PlaceModels["CeilingLight"];
            }
            if (placeName.StartsWith("ExtraBattery_", System.StringComparison.Ordinal))
            {
                return PlaceModels["BatteryItem"];
            }
            return PlaceTypes.TryGetValue(placeName, out var type) && PlaceModels.TryGetValue(type, out var model) ? model : null;
        }

        // ゲームとUnityで、同じFBXが180度回った向きになる分（上の説明）
        public static readonly Quaternion AxisCorrection = Quaternion.Euler(0.0f, 180.0f, 0.0f);

        static Vector3 ParseVector(string text)
        {
            var parts = text.Split(',');
            return new Vector3(ParseFloat(parts[0]), ParseFloat(parts[1]), ParseFloat(parts[2]));
        }

        static float ParseFloat(string text)
        {
            return float.Parse(text.Trim().TrimEnd('f'), CultureInfo.InvariantCulture);
        }
    }

    // 写したモデルの読み込みの設定：FBX の単位（cm）で縮めず、ゲームと同じ数値の大きさのまま使う
    public sealed class GameModelImportSettings : AssetPostprocessor
    {
        void OnPreprocessModel()
        {
            if (!assetPath.StartsWith(GameModelCatalog.ModelFolder))
            {
                return;
            }
            var importer = (ModelImporter)assetImporter;
            importer.useFileScale = false;
            importer.globalScale = 1.0f;
            importer.animationType = ModelImporterAnimationType.None;
            importer.importAnimation = false;
            importer.importCameras = false;
            importer.importLights = false;
        }
    }
}
