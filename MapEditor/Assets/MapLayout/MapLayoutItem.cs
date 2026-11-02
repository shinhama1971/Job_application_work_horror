// ============================================================================
// ファイルの役割: 配置ファイルの1行（prop・place・spot）と、シーンの GameObject を結び付けている印。
// MapLayoutEditor（Editor フォルダ）が読み込み時に付け、書き出し時にこの印で元の行を探している。
// ============================================================================

using UnityEngine;

namespace MapLayout
{
    public enum MapLayoutKind
    {
        Prop,   // 箱の壁・小物（prop 行。位置・大きさ）
        Place,  // 進行のコードが作る物の置き場所（place 行。位置・向き・大きさ）
        Spot,   // 候補の場所（spot 行。位置だけ）
    }

    // モデルの部品（子）をクリックしても、この印の付いた物が選ばれるようにしている（動かす単位をそろえるため）
    [SelectionBase]
    public sealed class MapLayoutItem : MonoBehaviour
    {
        // 行の種類と、読み込んだときのファイルの行番号（0から）と、名前（spot は組の名前）
        public MapLayoutKind Kind;
        public int LineIndex;
        public string Key;
        // place 行に向き（yaw）・大きさ（size）が書いてあったか
        public bool HasYaw;
        public bool HasSize;
    }
}
