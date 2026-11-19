#ifndef POST_PROCESS_BUFFER_INCLUDED
#define POST_PROCESS_BUFFER_INCLUDED

// FullScreenQuad.h の TimeBuffer と同じ並び。画面効果の PS は共通の b0 を読む。
cbuffer TimeBuffer : register(b0)
{
    float time;
    float bloomIntensity;
    float noiseAmount;
    float vignetteStrength;
    float screenAspect;
    float volumeIntensity;
    float lensDistortionStrength;
    float horrorPulseStrength;
    float exposure;
    float lensMoisture;
    float corridorTension;
    float filmGradeStrength;
    float lensDirtStrength;
    float signalInterference;
    // コマ落ち（1なら、写し取りを止めた前のフレームの画面を全面に出す）と、そのときの縦のずれ
    float frameHold;
    float frameHoldOffset;
    // 恐怖で視界が狭まる強さ（0〜1）と、心拍の脈（0〜1。鳴った瞬間に1）。PostProcess::SetFearVision
    float fearVision;
    float fearBeat;
    // レンズのしぶきの乱数の種（付くたびに変わる）と、付いてからの秒数。PostProcess::TriggerLensMoisture
    float lensDropSeed;
    float lensDropAge;
    // 影のまわりの空間のゆがみ（xy = 画面の位置 0〜1、z = 半径（縦の高さを1とした長さ）、w = 強さ 0〜1。0なら使わない）。
    // PostProcess::SetPresenceWarps。数は FullScreenQuad::PresenceWarpCount と合わせる
    float4 presenceWarp[2];
    // 残像（x = 強さ 0〜1、yz = 少し前の画面をずらす量（画面の位置 0〜1 の差）、w = 写し取るたびに変わる乱数の種）。
    // PostProcess::SetEcho。少し前の画面は t1 の EchoTexture
    float4 echo;
};

#endif
