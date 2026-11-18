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
};

#endif
