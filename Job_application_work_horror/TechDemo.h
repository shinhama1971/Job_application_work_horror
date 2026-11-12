// ゲーム本編の描画で使う方式。描画側の呼び出しは単独の技術デモとの共通化のために残す。
#pragma once

namespace Tools::TechDemo
{
    // 本描画で、タイルごとに絞った光源のリストを使うか（falseなら全部の光源を画素ごとに計算する）
    bool UsesTiledLighting();
    // 本描画の前に、深度プリパスを描くか
    bool UsesDepthPrepass();
    // 水面の反射の方式（0 なし、1 キューブマップ、2 SSR、3 レイ反射（普段））
    int GetReflectionMethod();
    // 懐中電灯の影の方式（0=影なし、1=硬い影、2=PCSSの柔らかい影）
    int GetShadowMode();
    // 水面の光の揺らぎの強さに掛ける倍率（0なら出さない）
    float GetWaterCausticsScale();
    // 水たまりの波（足音と水滴の波の式）を計算して映すか
    bool UsesWaterRipples();
    // 水たまりの描き方（0 = 床全体のシェーダーで計算する前の方式、1 = 水たまりごとのメッシュ（どれも一番細かく）、
    // 2 = メッシュと、遠いほど軽くする LOD（普段））
    int GetPuddleMeshMode();
    // 画面空間の環境遮蔽（SSAO）を計算して使うか
    bool UsesAmbientOcclusion();
    // 物理ベースの材質（PBR）で描くか（falseなら拡散反射だけで、環境マップも撮らない）
    bool UsesPbr();
    // 天井の照明の光の筋を描くか
    bool UsesLightShafts();
    // 揺れる電球の影（2面）を描くか
    bool UsesBulbShadow();
    // 外の森の草の LOD（0 = LODなし、1 = LODあり（普段）、2 = LODを色で表示）
    int GetGrassLodMode();
    // 外の森の草を、見下ろしたときにカメラの上の向きへ曲げるか
    bool UsesGrassCameraBend();

}
