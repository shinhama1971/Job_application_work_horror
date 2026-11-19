#ifndef WATER_CAUSTICS_LAYOUT_INCLUDED
#define WATER_CAUSTICS_LAYOUT_INCLUDED

// Renderer.h の WATER_CAUSTICS_BUFFER と同じ配列長にする。光の筋も同じ位置から人影を読む。
#define WATER_CAUSTICS_MAX_HANDPRINTS 4
// 反射用の箱1件につき、中心・半分の寸法・色をfloat4で渡す。
#define REFLECTION_PROP_STRIDE 3

// レイ反射で水面に映す小物の箱の最大数（Renderer.h の WATER_CAUSTICS_BUFFER::MaxReflectionProps と同じ）
#define MAX_REFLECTION_PROPS 16

// Renderer.hのWATER_CAUSTICS_BUFFERと同じ並び（b12）。Groundが毎フレーム設定している
cbuffer WaterCausticsBuffer : register(b12)
{
    // プレイヤーの視点のビュー行列。懐中電灯はこの視点から照らしているため、反射を描くときもこの行列を使っている
    // （ビュー行列のb1は頂点シェーダーにしか設定していないため、ピクセルシェーダーで使う分はここで受け取っている）
    matrix CausticView;
    // 床一面が水に浸かった範囲（xy = x・zの最小、zw = x・zの最大）。範囲がないときは最小が最大より大きい
    float4 CausticFloodRect;
#if defined(WATER_CAUSTICS_COMPACT_BUFFER)
    // 光の筋では、この4要素の内訳を参照しない。
    float4 CausticParameters;
#else
    float CausticWaterY;        // 水面の高さ
    float CausticTime;          // 揺らぎを動かす時間（秒）
    float CausticStrength;      // 強さ（0なら計算しない）
    float CausticPadding;
#endif
    // 水たまりの波のシミュレーションの範囲と強さ（xy = 範囲の最小のx・z、z = 範囲の一辺の長さ、w = 強さ。0なら使わない）
    float4 CausticRipple;
    // 水面の反射の比較（SSR）で使う、プレイヤーの視点の射影行列
    matrix CausticProjection;
    // 水面の反射の方式（x = 0 反射なし、1 キューブマップ、2 SSR、3 レイ反射（普段））。「反射なし」は床の反射の強さも0にしている。
    // y = SSRの深度と前のフレームの画面が用意されているか（1なら、レイ反射でもSSRを使う）、z = レイ反射で映す小物の箱の数
    float4 ReflectionParams;
    // 2面の「血の手形」の異変で壁に浮かべる手形（x = 壁の表面のx、y = 高さ、z = 奥行き、w = 見え方0〜1。0なら描かない）
    float4 WallHandprints[WATER_CAUSTICS_MAX_HANDPRINTS];
    // 水面にだけ映す人影（xyz = 足もとの位置、w = 見え方0〜1。0なら描かない。負なら天井に逆さまに張り付いた人影（1面の西棟））
    float4 PuddleFigure;
    // 2面の「見えない影」の異変で、影だけを落とす人影（xyz = 足もとの位置、w = 見え方0〜1。0なら影を付けない。figureShadow.hlsli）
    float4 UnseenFigure;
#if !defined(WATER_CAUSTICS_COMPACT_BUFFER)
    // レイ反射の箱。3つずつ中心・半分の大きさ・色。最初の2つの w は扉の幅方向の x・z（小物は0）。数は ReflectionParams.z
    float4 ReflectionProps[MAX_REFLECTION_PROPS * REFLECTION_PROP_STRIDE];
    // 2面の壁の鏡に映す自分の体（xyz = 足もとの位置、w = 映すか（1なら映す）。mirrorPS が光線とカプセルで交差させる。MirrorSelf）
    float4 MirrorSelf;
    // 鏡に映す自分の体の向き（xy = 右の向きのx・z、zw = 前の向きのx・z）
    float4 MirrorSelfBasis;
    // 鏡に映す自分の体の動き（x = 歩く手足の振り（-1〜1）、yzw = 詰め物）
    float4 MirrorSelfMotion;
    // 鏡の異変の、鏡にだけ映る黒い人影（xyz = 足もとの位置、w = 見え方0〜1。0なら描かない。MirrorAnomaly）
    float4 MirrorFigure;
    // プレイヤーの体が照明を遮って落とす影（xyz = 足もとの位置、w = 影を落とすなら1。figureShadow.hlsli の GetPlayerBodyShadow）
    float4 PlayerBody;
    // プレイヤーの体の向きと動き（xy = 前の向きのx・z、z = 歩く手足の振り（-1〜1）、w = 詰め物）
    float4 PlayerBodyPose;
    // 水たまりのレイ反射が読む環境マップを撮った位置（xyz。w = 詰め物）。ReflectionParams.w が1のときだけ使う（rayReflection.hlsli）
    float4 ReflectionProbeCenter;
    // SSRが前のフレームの同じ位置を読むための投影行列と深度の係数（xy）。
    matrix PreviousReflectionViewProjection;
    float4 PreviousReflectionDepth;
    // xy = 排水跡を描く柱の x・z、z = 浸水範囲の手前の端、w = 水面の持ち上げ量（0なら無効）。
    float4 DrainTrace;
#endif
}

#endif
