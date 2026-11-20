// ============================================================================
// ファイルの役割: 天井照明の形・点灯の状態・故障したときのちらつきを管理している。
// 主な技術: 点光源の登録、自己発光（エミッシブ）の表現、器具ごとにずらした周期、時間で変わる蛍光灯の演出
// ============================================================================

#include "Game.h"
#include "Application.h"
#include "CeilingLight.h"
#include "PropModel.h"

#include <cmath>

using namespace DirectX::SimpleMath;

namespace
{
    // 照明器具の標準の大きさ（モデルはこの大きさを1にした空間で作っている）
    const Vector3 FixtureModelSize = CeilingLight::StandardSize;

    // --- 電力が戻ったときの点き方：器具ごとに最大0.5秒ずらし（5段階 × 0.1秒）、0.58秒のあいだ1秒に24回の決まった並びで明滅する
    constexpr float StartupStaggerSteps = 5.0f;
    constexpr float StartupStaggerSeconds = 0.10f;
    constexpr float StartupSeconds = 0.58f;
    constexpr float StartupPulsesPerSecond = 24.0f;
    // 点き始めの明滅の並び：最初に点く刻み（0・3・4）と、安定して点き始める刻み（ここから3刻みごとに1回だけ消える）
    constexpr int StartupFirstOnPulse = 0;
    constexpr int StartupSecondOnPulse = 3;
    constexpr int StartupThirdOnPulse = 4;
    constexpr int StartupSteadyPulse = 7;
    // 安定してからの明滅の周期（刻み）と、その中で消える刻み
    constexpr int FlickerPulseCycle = 3;
    constexpr int FlickerOffPulseInCycle = 1;
    constexpr float StartupWarmupBrightness = 0.40f;  // 点き始めの明るさ（ここから、この幅だけ上げて1にする）
    constexpr float StartupWarmupRise = 0.60f;
    constexpr float StartupOffBrightness = 0.025f;    // 明滅で消えている間の明るさ
    // 点き始めの間（この秒数まで）は、明るさを速く追いつかせる
    constexpr float StartupFastResponseSeconds = 1.1f;
    // 目標の明るさがこれより低いときは、電圧の低下として速く追いつかせる
    constexpr float VoltageDipBrightness = 0.75f;

    // --- 停電中の非常灯：この周期で、この秒数だけ一瞬消える
    constexpr float EmergencyBlackoutCycle = 4.3f;
    constexpr float EmergencyBlackoutSeconds = 0.12f;

    // --- 進む先の灯の明滅：新しい灯へ移ってから最初の明滅まで（最短＋乱数の幅）、長く暗くなる確率、
    // 1回の明滅の長さ（最短＋乱数の幅。長く暗くなるときは少し長い）、次の明滅までの間（最短＋乱数の幅）、
    // 長く暗くなった後に点き直す長さ、明滅で点いている間・消えている間の明るさの倍率
    constexpr float RouteFirstDelay = 0.35f;
    constexpr float RouteFirstDelayRange = 0.40f;
    constexpr float RouteDropoutChance = 0.22f;
    constexpr float RouteFlickerSeconds = 0.65f;
    constexpr float RouteFlickerRange = 0.20f;
    constexpr float RouteDropoutRange = 0.30f;
    constexpr float RouteGapSeconds = 0.80f;
    constexpr float RouteGapRange = 1.00f;
    constexpr float RouteRelightSeconds = 0.22f;
    constexpr float RouteOnBrightness = 1.40f;
    constexpr float RouteOffBrightness = 0.12f;

    // --- 故障した蛍光灯：器具ごとの周期（最短＋ずらし）、周期の初めに長く消える秒数、不安定に消えることがある秒数、
    // 不安定に消える揺れのしきい値、消えている間の明るさ、点いている間の明るさの倍率
    constexpr float FaultCycleSeconds = 3.1f;
    constexpr float FaultCycleRange = 1.4f;
    constexpr float FaultLongDropoutSeconds = 0.36f;
    constexpr float FaultUnstableSeconds = 1.15f;
    constexpr float FaultUnstableThreshold = -0.32f;
    constexpr float FaultOffBrightness = 0.018f;
    constexpr float FaultDimRate = 0.58f;

    // --- 影が出たときなどの演出の明滅：1秒あたりの切り替えの回数、消えている間の明るさ
    constexpr float EventPulsesPerSecond = 26.0f;
    // 出来事の明滅の並び：最初に点く刻み（0・2）と、点いたり消えたりを続け始める刻み
    constexpr int EventFirstOnPulse = 0;
    constexpr int EventSecondOnPulse = 2;
    constexpr int EventSteadyPulse = 4;
    constexpr float EventOffBrightness = 0.008f;

    // --- 点光源：ほとんど消えている器具は登録しない明るさ、発光パネルから下げる距離、照らす範囲（電力が戻った後・停電中）
    constexpr float MinimumLightBrightness = 0.01f;
    constexpr float LightBelowPanel = 3.0f;
    constexpr float PoweredLightRange = 140.0f;
    constexpr float EmergencyLightRange = 135.0f;

    // --- 白熱電球の電圧の揺れ：短く沈む周期と、沈んでいる秒数
    constexpr float WarmBulbDipCycle = 6.3f;
    constexpr float WarmBulbDipSeconds = 0.18f;
    // ゆっくりした揺れの速さと幅、短く沈む深さ、器具ごとに時間をずらす倍率
    constexpr float WarmBulbSwayRate = 1.3f;
    constexpr float WarmBulbSwayDepth = 0.04f;
    constexpr float WarmBulbDipDepth = 0.10f;
    constexpr float WarmBulbTimeOffsetScale = 3.7f;

    // --- 器具の見た目：本体と発光パネルの頂点の色、本体のくすんだ灰色の金属、発光パネルの地の色（読み込み時・描くとき）
    const Color BodyVertexColor(0.16f, 0.17f, 0.16f, 1.0f);
    const Color PanelVertexColor(0.72f, 0.70f, 0.62f, 1.0f);
    const Color BodyDiffuse(0.42f, 0.44f, 0.41f, 1.0f);
    const Color BodySpecular(0.12f, 0.12f, 0.10f, 1.0f);
    constexpr float BodyShininess = 12.0f;
    const Color PanelInitDiffuse(0.25f, 0.24f, 0.20f, 1.0f);
    const Color PanelDiffuse(0.22f, 0.21f, 0.18f, 1.0f);
    // 発光パネルの光る色（明るさを掛ける）：暖かい白熱電球、故障した灯（黄緑がかった色）、普段（青白）、停電中の非常灯（赤）
    const Color WarmBulbPanelColor(0.95f, 0.62f, 0.30f, 1.0f);
    const Color FaultedPanelColor(0.52f, 0.62f, 0.45f, 1.0f);
    const Color PoweredPanelColor(0.68f, 0.76f, 0.88f, 1.0f);
    const Color EmergencyPanelColor(1.60f, 0.050f, 0.025f, 1.0f);
    // 照らす光の色と強さ（明るさに掛ける）：故障した灯、普段（少し緑がかった古い蛍光灯）、白熱電球、停電中の非常灯
    const Vector3 FaultedLightColor(0.70f, 0.78f, 0.56f);
    constexpr float FaultedLightIntensity = 0.80f;
    const Vector3 PoweredLightColor(0.88f, 0.94f, 0.84f);
    constexpr float PoweredLightIntensity = 0.95f;
    const Vector3 WarmBulbLightColor(1.0f, 0.66f, 0.36f);
    constexpr float WarmBulbLightIntensity = 1.40f;
    const Vector3 EmergencyLightColor(1.0f, 0.055f, 0.025f);
    constexpr float EmergencyLightIntensity = 3.4f;

    // --- 器具ごとの乱数：sin を使う決まった式の係数（0〜1の値を作る）と、器具ごとの値を作る倍率とずらし
    constexpr float HashFrequency = 12.9898f;
    constexpr float HashAmplitude = 43758.5453f;
    constexpr float RouteSeedScale = 3.17f;
    constexpr float RouteSeedOffset = 0.37f;
    // 明滅の回ごとに別の乱数を引くための倍率（最初の明滅までの間・長さ・長く暗くなるか・次までの間）
    constexpr float RouteFirstDelayHashStep = 1.37f;
    constexpr float RouteLengthHashStep = 7.31f;
    constexpr float RouteDropoutHashStep = 3.77f;
    constexpr float RouteGapHashStep = 1.93f;
    // 長く暗くなる瞬間の電気の音：高さ（最低＋長さに応じた幅）と音量
    constexpr float RouteSparkPitch = 0.55f;
    constexpr float RouteSparkPitchRange = 0.20f;
    constexpr float RouteSparkVolume = 0.16f;
    // 1回の明滅の中で点いている区間（始め〜この割合、この割合〜この割合）
    constexpr float RouteFirstOnEnd = 0.30f;
    constexpr float RouteSecondOnStart = 0.50f;
    constexpr float RouteSecondOnEnd = 0.84f;
    // 明滅していない間の電球のうなり（速さと幅）
    constexpr float RouteHumRate = 47.0f;
    constexpr float RouteHumDepth = 0.04f;
    // 停電中の進む先の灯の明るさ（非常灯・普通の灯）と、ゆっくりした揺れ（速さと幅）
    constexpr float RouteEmergencyBrightness = 0.56f;
    constexpr float RouteUnpoweredBrightness = 0.36f;
    constexpr float RouteIdleSwayRate = 11.7f;
    constexpr float RouteIdleSwayDepth = 0.006f;

    // --- 電力が戻った後の蛍光灯の揺れ：器具ごとの傷み具合（倍率とずらし）、電源のうなり（速さ＋傷みの幅、揺れの幅）、
    // 高い音のうなり（速さ・器具ごとのずらしの倍率・幅）
    constexpr float WearScale = 0.371f;
    constexpr float WearOffset = 0.17f;
    constexpr float HumRate = 15.0f;
    constexpr float HumWearRate = 4.0f;
    constexpr float HumDepth = 0.012f;
    constexpr float BuzzRate = 43.0f;
    constexpr float BuzzOffsetScale = 7.0f;
    constexpr float BuzzDepth = 0.004f;
    // まれに起きる電圧の低下：周期（最短＋傷みの幅）、器具ごとのずらし、長さ（最短＋傷みの幅）、そのときの明るさ（最低＋傷みの幅）
    constexpr float DipCycleSeconds = 8.5f;
    constexpr float DipCycleWearRange = 5.5f;
    constexpr float DipPhaseOffsetScale = 1.91f;
    constexpr float DipSeconds = 0.045f;
    constexpr float DipWearSeconds = 0.035f;
    constexpr float DipBrightness = 0.28f;
    constexpr float DipWearBrightness = 0.18f;

    // --- 停電中の非常灯の揺れ：2つの速さの sin を掛けた揺れ、一瞬消えている間の明るさ、普段の明るさ（最低＋揺れの幅）
    constexpr float EmergencyWobbleRateA = 13.0f;
    constexpr float EmergencyWobbleRateB = 29.0f;
    constexpr float EmergencyBlackoutBrightness = 0.015f;
    constexpr float EmergencyBaseBrightness = 0.48f;
    constexpr float EmergencyWobbleDepth = 0.10f;

    // --- 故障した蛍光灯の時間のずらし（器具ごと）と、安定器の雑音の2つの速さ
    constexpr float FaultTimeOffsetScale = 0.73f;
    constexpr float FaultCycleOffsetScale = 0.41f;
    constexpr float BallastNoiseRateA = 17.0f;
    constexpr float BallastNoiseRateB = 31.0f;

    // --- 演出の明滅で点いている間の明るさ（最低＋強さに応じた幅）
    constexpr float EventFlareBrightness = 0.38f;
    constexpr float EventFlareRange = 0.48f;

    // --- 明るさが目標へ近づく割合（60fpsで1フレームあたり）：進む先の明滅中、点き始め、電圧の低下、普段、停電中
    constexpr float RouteFlickerResponse = 0.72f;
    constexpr float StartupResponse = 0.48f;
    constexpr float VoltageDipResponse = 0.24f;
    constexpr float PoweredResponse = 0.08f;
    constexpr float UnpoweredResponse = 0.32f;
    constexpr float ResponseReferenceFps = 60.0f;

    // 色の赤・緑・青に明るさを掛ける（不透明度は1のまま）
    Color ScaleColor(const Color& color, float scale)
    {
        return Color(color.R() * scale, color.G() * scale, color.B() * scale, 1.0f);
    }
}
// 照明器具の形（本体と縁・格子のルーバー、下に付いた発光パネル）をMayaで作ったモデルから読み込み、マテリアルを作っている。
void CeilingLight::Init()
{
    // モデルは 22 × 0.56 × 10 の標準の大きさなので、その拡大率で割って読み込んでいる（器具ごとの大きさは m_Scale で変わる）。
    // 描くときにマテリアルを変えるため、本体のインデックス数を覚えている
    const std::vector<PropModel::IndexRange> ranges = PropModel::Append(
        "assets/model/props/ceiling_light.fbx",
        {
            { "Body", BodyVertexColor },
            { "Panel", PanelVertexColor },
        },
        FixtureModelSize, m_Vertices, m_Indices);
    m_BodyIndexCount = ranges[0].Count;

    m_VertexBuffer.Create(m_Vertices);
    m_IndexBuffer.Create(m_Indices);
    m_Shader.CreateRequired("shader/litTextureVS.hlsl", "shader/litTexturePS.hlsl");

    // 本体のマテリアル：くすんだ灰色の金属
    MATERIAL body{};
    body.Diffuse = BodyDiffuse;
    body.Specular = BodySpecular;
    body.Shininess = BodyShininess;
    body.TextureEnable = FALSE;
    m_BodyMaterial = std::make_unique<Material>();
    m_BodyMaterial->CreateRequired(body);

    // 発光パネルのマテリアル。光る色は明るさに合わせてDrawで毎回変えている
    MATERIAL panel{};
    panel.Diffuse = PanelInitDiffuse;
    panel.TextureEnable = FALSE;
    m_LightMaterial = std::make_unique<Material>();
    m_LightMaterial->CreateRequired(panel);
}

// 進む先の灯は、不ぞろいな間隔で少し長く2回光る。器具と周囲の赤い光だまりに同じ明暗を付ける。
// ときどき長く暗くなってから点き直し、そのときだけ小さな電気の音を鳴らす。
void CeilingLight::UpdateRouteFlicker(float& targetBrightness)
{
    // 0〜1の乱数。器具ごとの値と回数から決まるので、同じ回の中では同じ並びになる
    const auto noise = [](float seed)
    {
        const float value = std::sin(seed * HashFrequency) * HashAmplitude;
        return value - std::floor(value);
    };
    const float seed = m_FlickerOffset * RouteSeedScale + RouteSeedOffset;

    // 新しい灯へ移ったら、短い間を置いてから最初の明滅を始める。
    if (m_RouteNextFlicker < 0.0f)
    {
        m_RouteNextFlicker = m_Time + RouteFirstDelay + noise(seed + m_RouteFlickerSerial * RouteFirstDelayHashStep) * RouteFirstDelayRange;
    }
    if (m_Time >= m_RouteNextFlicker && m_Time >= m_RouteFlickerEnd)
    {
        m_RouteFlickerSerial += 1.0f;
        const float length = noise(seed + m_RouteFlickerSerial * RouteLengthHashStep);
        m_RouteDropout = noise(seed + m_RouteFlickerSerial * RouteDropoutHashStep) < RouteDropoutChance;
        m_RouteFlickerStart = m_Time;
        m_RouteFlickerEnd = m_Time + (m_RouteDropout ? RouteFlickerSeconds + length * RouteDropoutRange : RouteFlickerSeconds + length * RouteFlickerRange);
        m_RouteNextFlicker = m_RouteFlickerEnd + RouteGapSeconds + noise(seed + m_RouteFlickerSerial * RouteGapHashStep) * RouteGapRange;
        if (m_RouteDropout)
        {
            // 消える瞬間に、電気の接点がはじけるような小さな音
            Core::Game::GetInstance()->PlayAudioCueAt(
                SOUND_CUE_FLASHLIGHT, m_Position, RouteSparkPitch + length * RouteSparkPitchRange, RouteSparkVolume);
        }
    }

    if (m_Time < m_RouteFlickerEnd)
    {
        const float phase = (m_Time - m_RouteFlickerStart) / (m_RouteFlickerEnd - m_RouteFlickerStart);
        const bool on = m_RouteDropout
            ? m_RouteFlickerEnd - m_Time < RouteRelightSeconds
            : phase < RouteFirstOnEnd || (phase >= RouteSecondOnStart && phase < RouteSecondOnEnd);
        targetBrightness *= on ? RouteOnBrightness : RouteOffBrightness;
        m_RouteFlickering = true;
    }
    else
    {
        // 間の間も、電球のうなりで明るさが少し揺れている
        targetBrightness *= 1.0f + std::sin(m_Time * RouteHumRate + m_FlickerOffset) * RouteHumDepth;
    }
}

// 電力の状態と演出から、このフレームの照明の明るさを決めている。
// 目標の明るさを決めてから、実際の明るさを少しずつ近づけている（急に変わらないように）。
void CeilingLight::Update()
{
    const float deltaTime = Application::GetDeltaTime();
    m_Time += deltaTime;

    // 電力が復旧した瞬間から、点灯までの経過時間を数えている
    const bool powerRestored =
        Core::Game::GetInstance()->IsPowerRestored();

    if (powerRestored && !m_WasPowerRestored)
    {
        m_PowerOnTimer = 0.0f;
    }
    else if (powerRestored)
    {
        m_PowerOnTimer += deltaTime;
    }
    else
    {
        m_PowerOnTimer = 0.0f;
    }

    m_WasPowerRestored = powerRestored;

    float targetBrightness = 0.0f;
    if (powerRestored)
    {
        targetBrightness = ComputePoweredBrightness();
    }
    else if (m_IsEmergencyLight)
    {
        targetBrightness = ComputeEmergencyBrightness();
    }

    // 進む先の候補の照明。停電中も非常回路の弱い光を残し、候補どうしは同じ明るさ・同じ色にしている。
    // 今進む先の1灯だけ、発光パネルと周囲の光だまりに明滅を重ねる。
    m_RouteFlickering = false;
    if (m_IsRouteFixture)
    {
        if (!powerRestored)
        {
            targetBrightness = (m_IsEmergencyLight ? RouteEmergencyBrightness : RouteUnpoweredBrightness) +
                std::sin(m_Time * RouteIdleSwayRate + m_FlickerOffset) * RouteIdleSwayDepth;
        }
        if (!m_RouteFlickerEnabled)
        {
            // ちかちかしない灯は落ち着いて点けたまま。次に選ばれたら、少し間を置いてから始める
            m_RouteNextFlicker = -1.0f;
            m_RouteFlickerEnd = -1.0f;
        }
        else
        {
            UpdateRouteFlicker(targetBrightness);
        }
    }

    // 故障した蛍光灯も時々点いて進む方向を示すが、不規則に消えるようにしている。
    // この自然な揺らぎの後からイベント用の明滅を重ね、怖い演出を確実に見せている。
    if (m_IsFaulted && powerRestored)
    {
        targetBrightness = ApplyFaultDropout(targetBrightness);
    }

    // イベント用の明滅（影が出たときなど）。決まった並びで点滅させ、点いている間は普段より明るくしている
    if (m_EventFlickerTimer > 0.0f)
    {
        targetBrightness = ApplyEventFlicker(targetBrightness, deltaTime);
    }

    // 演出で強制的に消しているときは、何があっても真っ暗にしている
    if (m_IsForcedOff)
    {
        targetBrightness = 0.0f;
    }

    // 明るさの追いつく速さ：点き始めと電圧低下は速く、普段はゆっくり変えている
    const bool startingUp = powerRestored && m_PowerOnTimer < StartupFastResponseSeconds;
    const bool voltageDip =
        powerRestored && !startingUp && targetBrightness < VoltageDipBrightness;
    // 進む先の灯は、明滅の山を見分けられる速さで明るさを追従させる。
    const float response = m_RouteFlickering
        ? RouteFlickerResponse
        : (powerRestored
            ? (startingUp ? StartupResponse : (voltageDip ? VoltageDipResponse : PoweredResponse))
            : UnpoweredResponse);
    // フレームレートが変わっても同じ速さで近づくよう、60fpsを基準にした指数で補正している
    const float deltaResponse = 1.0f - std::pow(
        1.0f - response, deltaTime * ResponseReferenceFps);
    m_Brightness +=
        (targetBrightness - m_Brightness) * deltaResponse;
}

// 電力が戻った後の明るさ
float CeilingLight::ComputePoweredBrightness() const
{
    // 照明を順番に点け、各蛍光灯が完全に点く直前に短く明滅させている。
    // 一斉に点くのを避け、古い設備が復旧するときの不安定さを出している。
    const float startupDelay =
        std::fmod(std::fabs(m_FlickerOffset), StartupStaggerSteps) * StartupStaggerSeconds;
    const float startupTime = m_PowerOnTimer - startupDelay;

    if (startupTime >= 0.0f && startupTime < StartupSeconds)
    {
        // 1/24秒ごとに、点いているか消えているかを決まった並びで切り替えている（点き始めの蛍光灯の明滅）
        const int pulse = static_cast<int>(startupTime * StartupPulsesPerSecond);
        const bool tubeIsOn =
            (pulse == StartupFirstOnPulse) || (pulse == StartupSecondOnPulse) || (pulse == StartupThirdOnPulse) ||
            (pulse >= StartupSteadyPulse && (pulse % FlickerPulseCycle) != FlickerOffPulseInCycle);
        const float warmup =
            StartupWarmupBrightness + (startupTime / StartupSeconds) * StartupWarmupRise;
        return tubeIsOn ? warmup : StartupOffBrightness;
    }
    if (startupTime >= StartupSeconds)
    {
        // 器具ごとに安定器の個体差を持たせ、まれに起きる電圧の低下を再現している。
        // 一定すぎる光を避け、古い施設らしさを出している。
        const float fixtureWear = std::fmod(
            std::fabs(m_FlickerOffset) * WearScale + WearOffset,
            1.0f);
        // 電源のうなりによる小さな明るさの揺れ（器具ごとに周期を変えている）
        const float electricalHum =
            std::sin((m_Time + m_FlickerOffset) *
                (HumRate + fixtureWear * HumWearRate)) * HumDepth;
        const float highFrequencyBuzz =
            std::sin((m_Time * BuzzRate) + m_FlickerOffset * BuzzOffsetScale) * BuzzDepth;
        // 器具ごとの周期で、まれに短く暗くなる（電圧の低下）
        const float dipCycle = DipCycleSeconds + fixtureWear * DipCycleWearRange;
        const float dipPhase = std::fmod(
            m_Time + std::fabs(m_FlickerOffset) * DipPhaseOffsetScale,
            dipCycle);
        const bool rareVoltageDip =
            dipPhase < DipSeconds + fixtureWear * DipWearSeconds;
        const float voltage = rareVoltageDip
            ? DipBrightness + fixtureWear * DipWearBrightness
            : 1.0f;
        return (1.0f + electricalHum + highFrequencyBuzz) * voltage;
    }
    return 0.0f;
}

// 停電中の非常灯：赤く暗い光が不安定に揺れ、4.3秒ごとに一瞬消えている
float CeilingLight::ComputeEmergencyBrightness() const
{
    const float flickerTime = m_Time + m_FlickerOffset;
    const float unstable =
        std::sin(flickerTime * EmergencyWobbleRateA) *
        std::sin(flickerTime * EmergencyWobbleRateB);
    const bool shortBlackout =
        std::fmod(flickerTime, EmergencyBlackoutCycle) < EmergencyBlackoutSeconds;
    // 懐中電灯を消すとほとんど闇なので、赤い光だまりが見えるよう、前より明るくしている
    return shortBlackout
        ? EmergencyBlackoutBrightness
        : EmergencyBaseBrightness + (unstable + 1.0f) * EmergencyWobbleDepth;
}

// 故障した蛍光灯の、不規則に消える揺らぎ
float CeilingLight::ApplyFaultDropout(float targetBrightness) const
{
    const float faultTime = m_Time + std::fabs(m_FlickerOffset) * FaultTimeOffsetScale;
    const float faultCycle = FaultCycleSeconds +
        std::fmod(std::fabs(m_FlickerOffset) * FaultCycleOffsetScale, FaultCycleRange);
    const float faultPhase = std::fmod(faultTime, faultCycle);
    const float ballastNoise =
        std::sin(faultTime * BallastNoiseRateA) * std::sin(faultTime * BallastNoiseRateB);
    const bool longDropout = faultPhase < FaultLongDropoutSeconds;
    const bool unstableDropout =
        faultPhase < FaultUnstableSeconds && ballastNoise < FaultUnstableThreshold;
    return (longDropout || unstableDropout)
        ? FaultOffBrightness
        : targetBrightness * FaultDimRate;
}

// イベント用の明滅（決まった並びで点滅させ、点いている間は普段より明るくしている）
float CeilingLight::ApplyEventFlicker(float targetBrightness, float deltaTime)
{
    m_EventFlickerTimer = (std::max)(
        0.0f,
        m_EventFlickerTimer - deltaTime);
    const float elapsed =
        m_EventFlickerDuration - m_EventFlickerTimer;
    const int pulse = static_cast<int>(elapsed * EventPulsesPerSecond);
    const bool tubeOn =
        (pulse == EventFirstOnPulse) || (pulse == EventSecondOnPulse) ||
        (pulse >= EventSteadyPulse && (pulse % FlickerPulseCycle) != FlickerOffPulseInCycle);
    const float flareBrightness =
        EventFlareBrightness + m_EventFlickerStrength * EventFlareRange;
    return tubeOn
        ? (std::max)(targetBrightness, flareBrightness)
        : EventOffBrightness;
}

// 本体を描き、続けて発光パネルを明るさに応じた色で光らせて描いている
void CeilingLight::Draw(Camera* camera)
{
    camera->SetCamera();

    const Matrix rotation = Matrix::CreateFromYawPitchRoll(
        m_Rotation.y, m_Rotation.x, m_Rotation.z);
    const Matrix scale = Matrix::CreateScale(m_Scale);
    const Matrix translation = Matrix::CreateTranslation(m_Position);
    Matrix world = scale * rotation * translation;
    Renderer::SetWorldMatrix(&world);

    ID3D11DeviceContext* context = Renderer::GetDeviceContext();
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Shader.SetGPU();
    m_VertexBuffer.SetGPU();
    m_IndexBuffer.SetGPU();

    m_BodyMaterial->SetGPU();
    context->DrawIndexed(static_cast<UINT>(m_BodyIndexCount), 0, 0);

    // 発光パネルの光る色：通常は青白、故障した灯は黄緑がかった色、停電中は赤（非常灯）
    MATERIAL panel{};
    panel.Diffuse = PanelDiffuse;
    if (Core::Game::GetInstance()->IsPowerRestored())
    {
        if (m_IsWarmBulb && !m_IsFaulted)
        {
            // 暖かい白熱電球：橙がかった黄色で、電圧の揺れに合わせて少し沈む
            const float warm = m_Brightness * GetWarmBulbSag();
            panel.Emission = ScaleColor(WarmBulbPanelColor, warm);
        }
        else if (m_IsFaulted)
        {
            panel.Emission = ScaleColor(FaultedPanelColor, m_Brightness);
        }
        else
        {
            panel.Emission = ScaleColor(PoweredPanelColor, m_Brightness);
        }
    }
    else
    {
        panel.Emission = ScaleColor(EmergencyPanelColor, m_Brightness);
    }
    panel.TextureEnable = FALSE;
    m_LightMaterial->SetMaterial(panel);
    m_LightMaterial->SetGPU();

    // 本体の後ろに並んだ、発光パネルのインデックスだけを描いている
    const UINT panelIndexCount =
        static_cast<UINT>(m_Indices.size() - m_BodyIndexCount);
    context->DrawIndexed(
        panelIndexCount,
        static_cast<UINT>(m_BodyIndexCount),
        0);
}

// 頂点データを解放している
void CeilingLight::Uninit()
{
    m_Vertices.clear();
    m_Indices.clear();
}

// 明るさが残っている天井照明を点光源として登録し、部屋の壁や床へ光を当てている。
// 以前はPlayerがまとめて作っていた処理で、照明自身が自分の光を申告する形にしている。
void CeilingLight::CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const
{
    const float brightness = GetBrightness();
    // ほとんど消えている照明は登録しない（タイルベースのライティングの計算を減らすため）
    if (brightness <= MinimumLightBrightness)
    {
        return;
    }

    const bool powerRestored = Core::Game::GetInstance()->IsPowerRestored();
    ENVIRONMENT_POINT_LIGHT pointLight{};

    // 発光パネルより少し下へ光源を置き、天井に埋もれず室内を照らすようにしている。
    pointLight.PositionRange = Vector4(
        m_Position.x, m_Position.y - LightBelowPanel, m_Position.z,
        powerRestored ? PoweredLightRange : EmergencyLightRange);

    // 進行先の器具も、周囲と同じ非常灯・蛍光灯の色で照らす。
    // 電力が戻っても、古い蛍光灯のように少し緑がかった弱い光にし、照明と照明の間に暗がりが残るようにしている。
    if (powerRestored)
    {
        pointLight.ColorIntensity = IsFaulted()
            ? Vector4(FaultedLightColor.x, FaultedLightColor.y, FaultedLightColor.z, brightness * FaultedLightIntensity)
            : Vector4(PoweredLightColor.x, PoweredLightColor.y, PoweredLightColor.z, brightness * PoweredLightIntensity);
        if (m_IsWarmBulb && !IsFaulted())
        {
            // 暖かい白熱電球：照らす光も橙がかった色にし、電圧の揺れで少し沈む（P.T. の廊下の光）
            pointLight.ColorIntensity = Vector4(WarmBulbLightColor.x, WarmBulbLightColor.y, WarmBulbLightColor.z,
                brightness * WarmBulbLightIntensity * GetWarmBulbSag());
        }
    }
    else
    {
        // 停電中の非常灯：懐中電灯を消すとほとんど闇なので、赤い光だまりが道しるべになるよう強めにしている
        pointLight.ColorIntensity = Vector4(
            EmergencyLightColor.x, EmergencyLightColor.y, EmergencyLightColor.z, brightness * EmergencyLightIntensity);
    }
    lights.push_back(pointLight);
}

// 白熱電球の電圧の揺れ：ゆっくりした小さな揺れに、数秒に一度だけ短く沈む揺れを重ねている（器具ごとに時間をずらしている）
float CeilingLight::GetWarmBulbSag() const
{
    const float time = m_Time + m_FlickerOffset * WarmBulbTimeOffsetScale;
    const float slow = std::sin(time * WarmBulbSwayRate) * 0.5f + 0.5f;
    const float dipPhase = std::fmod(time, WarmBulbDipCycle);
    const float dip = dipPhase < WarmBulbDipSeconds ? std::sin(dipPhase / WarmBulbDipSeconds * DirectX::XM_PI) : 0.0f;
    return 1.0f - slow * WarmBulbSwayDepth - dip * WarmBulbDipDepth;
}
