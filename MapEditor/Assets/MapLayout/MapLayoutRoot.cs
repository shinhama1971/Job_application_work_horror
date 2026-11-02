// ============================================================================
// ファイルの役割: 読み込んだ配置ファイル1つ分の、シーンの一番上の GameObject に付ける印（どのファイルから読んだか）。
// ============================================================================

using UnityEngine;

namespace MapLayout
{
    public sealed class MapLayoutRoot : MonoBehaviour
    {
        // 読み込んだ配置ファイル（ゲームの assets/layout/ の中の場所）
        public string FilePath;
    }
}
