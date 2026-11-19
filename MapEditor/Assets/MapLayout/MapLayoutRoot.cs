// ============================================================================
// ファイルの役割: 読み込んだ配置ファイル1つ分の、シーンの一番上の GameObject に付ける印（どのファイルから読んだか）。
// ============================================================================

using System.Collections.Generic;
using UnityEngine;

namespace MapLayout
{
    public sealed class MapLayoutRoot : MonoBehaviour
    {
        // 読み込んだ配置ファイル（ゲームの assets/layout/ の中の場所）
        public string FilePath;
        // 外部編集による既存行の変更を、古いシーンからの書き出し前に検出する。
        [HideInInspector] public string SourceLayoutSnapshot;
        // 読み込んだ prop・place・spot と、シーンから書き出した行の中身。
        // 書き出すとき、シーンに物がない行のうち、ここにある行だけを「シーンで消した」として消している
        // （読み込んだ後にゲームの配置エディターや手で足された行は、シーンにまだ無いだけなので消さない）
        public List<string> KnownLines = new List<string>();
    }
}
