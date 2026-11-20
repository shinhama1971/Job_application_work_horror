// ============================================================================
// ファイルの役割: 地点ごとのCPU・GPU時間を測り、ライトの点灯・消灯を比較する。
// ============================================================================

#include "CaptureBenchmark.h"
#include "Application.h"
#include "Camera.h"
#include "CaptureMode.h"
#include "CaptureTours.h"
#include "ForestGrass.h"
#include "ForestScene.h"
#include "Game.h"
#include "GpuTimer.h"
#include "Player.h"
#include "Renderer.h"

#include <SimpleMath.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX::SimpleMath;

namespace
{
    using namespace Tools::CaptureTours;

    // 計測の表：1%遅いフレーム（99パーセンタイル）、ミリ秒から FPS への換算
    constexpr double SlowFramePercentile = 0.99;
    constexpr double MillisecondsPerSecond = 1000.0;
    // 計測の表の列の幅（値・差）
    constexpr int BenchmarkValueWidth = 9;
    constexpr int BenchmarkDiffWidth = 8;

    // ------------------------------------------------------------------------
    // 処理の重さの計測（--benchmark）
    // 1面の決まった地点ごとに「ライトOFF → ライトON」の順で同じ景色を測り、差を比べている。
    // ------------------------------------------------------------------------
    // 計測する地点（名前・立つ位置・向き）
    struct BenchmarkSpot
    {
        const char* Name;
        Vector3 Position;
        float Yaw;
        float Pitch;
        SceneName Scene = SceneName::Stage;
    };

    const std::vector<BenchmarkSpot> StageBenchmarkSpots =
    {
        { "開始地点（壁の文字の前）", Vector3(0.0f, FloorY, -128.0f), Pi, -0.12f },
        { "開始地点から廊下の奥", Vector3(0.0f, FloorY, -110.0f), 0.0f, 0.0f },
        { "中央ホール", Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f), -0.05f },
        { "左の倉庫", Vector3(-150.0f, FloorY, -110.0f), 0.0f, -0.26f },
        { "暗証番号の扉", Vector3(12.0f, FloorY, 112.0f), Pi * 0.5f, -0.05f },
        { "ループ廊下", Vector3(0.0f, FloorY, 185.0f), 0.0f, 0.0f },
        // 床一面が水に浸かった西棟。水面の反射（レイ反射）が画面の広い範囲に出る場所。
        { "西棟（浸水した広間）", Vector3(-233.0f, FloorY, -21.0f), YawToward(-233.0f, -21.0f, -330.0f, 140.0f), -0.16f },
        { "2面（裸電球の下）", Vector3(12.0f, FloorY, -25.0f), 0.0f, 0.04f, SceneName::Stage2 },
        { "2面（廊下の奥）", Vector3(-12.0f, FloorY, 60.0f), Pi, -0.10f, SceneName::Stage2 },
        // 左の壁の古い鏡を正面から見る所。鏡は、鏡で反射した光線をシェーダーの中で飛ばして映している（自分の体も映る）
        { "2面（壁の鏡の前）", Vector3(0.0f, FloorY, 37.0f), -Pi * 0.5f, -0.06f, SceneName::Stage2 },
    };

    // --forest のときは森の同じ地点を測る。草なしとの比較でも、位置と向きは変えない。
    const std::vector<BenchmarkSpot>& GetBenchmarkSpots()
    {
        static const bool forest = ForestScene::IsRequestedByCommandLine();
        static const std::vector<BenchmarkSpot> forestSpots =
        {
            { "森（開始地点の小道）", Vector3(0.0f, FloorY, -398.0f), 0.0f, -0.20f, SceneName::Forest },
            { "森（街灯の横の草むら）", Vector3(-6.0f, FloorY, -98.0f), -0.65f, -0.32f, SceneName::Forest },
            { "森（研究所の手前）", Vector3(0.0f, FloorY, 125.0f), 0.0f, -0.15f, SceneName::Forest },
        };
        return forest ? forestSpots : StageBenchmarkSpots;
    }

    // 各段階のフレーム数。切り替えた直後はGPU時間の結果が数フレーム遅れて届くため、最初の45フレームは捨てている。
    constexpr int BenchmarkWarmupFrames = 45;
    constexpr int BenchmarkMeasureFrames = 180;
    // 同じ地点を機能OFFとONの2通りで測る。
    constexpr int BenchmarkPhaseCount = 2;
    // --figures のとき：水たまりの人影を正面のこの距離に、見えない影を正面のこの距離の少し右に置く（足もとは床の高さ）
    constexpr float BenchmarkPuddleFigureAhead = 26.0f;
    constexpr float BenchmarkUnseenFigureAhead = 34.0f;
    constexpr float BenchmarkUnseenFigureRight = 8.0f;
    constexpr float BenchmarkFigureFloorY = -100.0f;

    // 1つの条件（地点×ライトの状態）で集めた値。
    struct BenchmarkSamples
    {
        std::vector<double> FrameMs;    // 前のフレームからの実際の経過時間
        std::vector<double> CpuMs;      // FrameMsからPresentの待ち時間を引いたもの（CPU側で使った時間の目安）
        // 描画の段階ごとのGPU時間の合計と、取れた回数（平均を出すのに使っている）
        std::array<double, static_cast<size_t>(GpuPass::Count)> GpuSum{};
        std::array<int, static_cast<size_t>(GpuPass::Count)> GpuCount{};
    };

    // 1つの地点の、ライトOFFとライトONの結果
    struct BenchmarkResult
    {
        const BenchmarkSpot* Spot;
        BenchmarkSamples Off;
        BenchmarkSamples On;
    };

    // 今の地点の番号、地点の中での経過フレーム、結果
    size_t g_BenchmarkSpot = 0;
    int g_BenchmarkFrame = 0;           // 地点の中での経過フレーム
    std::vector<BenchmarkResult> g_BenchmarkResults;
    // 前のフレームの時刻、直前のPresentにかかった時間、GPUの名前
    std::chrono::steady_clock::time_point g_LastFrameTime;
    bool g_HasLastFrameTime = false;
    double g_LastPresentMs = 0.0;
    std::string g_AdapterName;

    // 平均値を求めている
    double Average(const std::vector<double>& values)
    {
        if (values.empty())
        {
            return 0.0;
        }
        double sum = 0.0;
        for (double value : values)
        {
            sum += value;
        }
        return sum / static_cast<double>(values.size());
    }

    // 遅い方から(1 - ratio)の位置の値を求めている（0.99なら遅い方から1%）。かくつきの目安に使っている。
    double Percentile(std::vector<double> values, double ratio)
    {
        if (values.empty())
        {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const size_t index = (std::min)(values.size() - 1,
            static_cast<size_t>(ratio * static_cast<double>(values.size() - 1) + 0.5));
        return values[index];
    }

    // 標準偏差を求めている（フレーム時間のばらつき。大きいほどFPSが上下して見える）
    double StandardDeviation(const std::vector<double>& values)
    {
        if (values.size() < 2)
        {
            return 0.0;
        }
        const double average = Average(values);
        double sum = 0.0;
        for (double value : values)
        {
            sum += (value - average) * (value - average);
        }
        return std::sqrt(sum / static_cast<double>(values.size() - 1));
    }

    // ある描画段階のGPU時間の平均を求めている（一度も取れていなければ0）
    double GpuAverage(const BenchmarkSamples& samples, GpuPass pass)
    {
        const size_t index = static_cast<size_t>(pass);
        return samples.GpuCount[index] == 0
            ? 0.0
            : samples.GpuSum[index] / samples.GpuCount[index];
    }

    // GPUの名前を記録している（結果を見る人が、どの環境の数字か分かるように）。
    void RecordAdapterName(ID3D11DeviceContext* context)
    {
        ComPtr<ID3D11Device> device;
        context->GetDevice(&device);
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        DXGI_ADAPTER_DESC description{};
        if (FAILED(device.As(&dxgiDevice)) ||
            FAILED(dxgiDevice->GetAdapter(&adapter)) ||
            FAILED(adapter->GetDesc(&description)))
        {
            g_AdapterName = "不明";
            return;
        }
        // GPUの名前はワイド文字なので、ファイルに書くためUTF-8へ変換している
        const int size = WideCharToMultiByte(CP_UTF8, 0, description.Description, -1,
            nullptr, 0, nullptr, nullptr);
        std::string name(static_cast<size_t>((std::max)(size - 1, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name.data(), size,
            nullptr, nullptr);
        g_AdapterName = name;
    }

    // 全地点の結果を表にして benchmark_result.txt に書き出している
    bool WriteBenchmarkResult(const std::filesystem::path& outputDirectory, bool vsync, bool figures)
    {
        Core::Game* game = Core::Game::GetInstance();
        std::ostringstream text;
        text << std::fixed << std::setprecision(2);
        // 先頭に、画面の大きさ・描画解像度・エフェクト設定・GPUの名前を書いている
        text << (ForestScene::IsRequestedByCommandLine() ? "処理の重さの計測結果（森・ライトOFFとONの比較）\n" : "処理の重さの計測結果（1・2面・ライトOFFとONの比較）\n");
        text << "画面: " << Application::GetWindowWidth() << "x" << Application::GetWindowHeight()
             << "  描画解像度: " << Application::GetWidth() << "x" << Application::GetHeight()
             << "  垂直同期: " << (vsync ? "あり" : "なし")
             << "  人影: " << (figures ? "あり（水たまりの人影と見えない影をカメラの前に置く）" : "なし")
             << "  エフェクト設定: " << (game->GetEffectLevel() + 1)
             << "  GPU: " << g_AdapterName << "\n";
        text << "各条件 " << BenchmarkMeasureFrames << " フレームの平均（ms）。"
                "1%遅は遅い方から1%のフレーム時間で、かくつきの目安です。\n\n";

        // GPU時間を表に出す描画段階と、その見出し
        const std::pair<const char*, GpuPass> passes[] =
        {
            { "GPU合計", GpuPass::Total },
            { "影", GpuPass::Shadow },
            { "水面反射", GpuPass::Reflection },
            { "本描画", GpuPass::MainScene },
            { "ブルーム", GpuPass::Bloom },
            { "画面効果", GpuPass::PostProcess },
            { "水たまりの波", GpuPass::WaterRipples },
            { "環境マップ", GpuPass::EnvironmentProbe },
            { "電球の影", GpuPass::BulbShadow },
            { "環境遮蔽", GpuPass::AmbientOcclusion },
            { "間接光", GpuPass::GlobalIllumination },
        };

        for (const BenchmarkResult& result : g_BenchmarkResults)
        {
            text << "■ " << result.Spot->Name << "\n";
            text << "  項目        ライトOFF   ライトON       差\n";
            // 1行分（項目名・ライトOFF・ライトON・差）を書く関数。差には+-の符号を付けている
            const auto row = [&text](const char* label, double off, double on)
            {
                text << "  " << label << "\t" << std::setw(BenchmarkValueWidth) << off << "   " << std::setw(BenchmarkValueWidth) << on
                     << "   " << std::showpos << std::setw(BenchmarkDiffWidth) << (on - off) << std::noshowpos << "\n";
            };
            const double offFrame = Average(result.Off.FrameMs);
            const double onFrame = Average(result.On.FrameMs);
            row("フレーム", offFrame, onFrame);
            row("1%遅", Percentile(result.Off.FrameMs, SlowFramePercentile), Percentile(result.On.FrameMs, SlowFramePercentile));
            row("最大", Percentile(result.Off.FrameMs, 1.0), Percentile(result.On.FrameMs, 1.0));
            row("ばらつき", StandardDeviation(result.Off.FrameMs), StandardDeviation(result.On.FrameMs));
            row("CPU", Average(result.Off.CpuMs), Average(result.On.CpuMs));
            for (const auto& [label, pass] : passes)
            {
                row(label, GpuAverage(result.Off, pass), GpuAverage(result.On, pass));
            }
            row("FPS換算", offFrame > 0.0 ? MillisecondsPerSecond / offFrame : 0.0,
                onFrame > 0.0 ? MillisecondsPerSecond / onFrame : 0.0);
            text << "\n";
        }

        std::ofstream file(outputDirectory / L"benchmark_result.txt", std::ios::binary);
        const std::string body = text.str();
        // メモ帳で文字化けしないよう、UTF-8のバイト順マークだけを書き込む。
        constexpr char Utf8Bom[] = "\xEF\xBB\xBF";
        file.write(Utf8Bom, sizeof(Utf8Bom) - 1);
        file.write(body.data(), static_cast<std::streamsize>(body.size()));
        file.close();
        return static_cast<bool>(file);
    }
}

namespace Tools::CaptureBenchmark
{
    std::wstring GetScreenshotName(ID3D11DeviceContext* context)
    {
        if (g_AdapterName.empty() && context != nullptr)
        {
            RecordAdapterName(context);
        }
        // 最初の地点のライトOFFと、各地点のライトONで1枚ずつ画面を保存している。描画解像度とHUDの見え方に加え、
        // 描画の処理を変えたときに見た目が変わっていないかを、地点ごとに比べられるようにしている。
        // どちらも捨てる区間の中なので、読み戻しの時間は計測に混ざらない。
        const int phaseLength = BenchmarkWarmupFrames + BenchmarkMeasureFrames;
        std::wstring screenName;
        if (g_BenchmarkSpot == 0 && g_BenchmarkFrame == BenchmarkWarmupFrames / 2)
        {
            screenName = L"benchmark_screen.png";
        }
        else if (g_BenchmarkSpot < GetBenchmarkSpots().size() &&
            g_BenchmarkFrame == phaseLength + BenchmarkWarmupFrames / 2)
        {
            screenName = L"benchmark_spot" + std::to_wstring(g_BenchmarkSpot) + L"_light_on.png";
        }
        return screenName;
    }

    // 計測モードの1フレーム分。地点に立たせ、ライトを切り替えながら値を集めている。
    bool Update(Core::Game* game, const std::filesystem::path& outputDirectory,
        bool vsync, bool figures, void (*log)(const std::string&))
    {
        // 前のフレームからの経過時間を測っている（最初の1回は0）
        const auto now = std::chrono::steady_clock::now();
        const double frameMs = g_HasLastFrameTime
            ? std::chrono::duration<double, std::milli>(now - g_LastFrameTime).count()
            : 0.0;
        g_LastFrameTime = now;
        g_HasLastFrameTime = true;

        // タイトルからの切り替えが終わり、1面が始まるまでは何もしていない
        const SceneName benchmarkScene = g_BenchmarkSpot < GetBenchmarkSpots().size()
            ? GetBenchmarkSpots()[g_BenchmarkSpot].Scene : GetBenchmarkSpots().back().Scene;
        if (game->GetCurrentSceneName() != benchmarkScene)
        {
            game->RequestSceneChange(benchmarkScene);
            return false;
        }
        Player* player = game->GetObj<Player>("Player");
        Camera* camera = game->GetCamera();
        if (player == nullptr || camera == nullptr)
        {
            return false;
        }

        // 全地点を測り終えたら結果を書き出し、数フレーム後に終了している
        if (g_BenchmarkSpot >= GetBenchmarkSpots().size())
        {
            if (!WriteBenchmarkResult(outputDirectory, vsync, figures))
            {
                Tools::CaptureMode::Abort("計測結果を書き出せません。");
                return false;
            }
            log("benchmark finished");
            return true;
        }
        // 新しい地点に来たら、結果を入れる場所を用意している
        if (g_BenchmarkResults.size() <= g_BenchmarkSpot)
        {
            g_BenchmarkResults.push_back({ &GetBenchmarkSpots()[g_BenchmarkSpot], {}, {} });
            log("benchmark spot " + std::to_string(g_BenchmarkSpot));
        }

        // 段階: [OFFの捨てる区間][OFFの計測][ONの捨てる区間][ONの計測]
        const int phaseLength = BenchmarkWarmupFrames + BenchmarkMeasureFrames;
        const bool lightOn = g_BenchmarkFrame >= phaseLength;
        const int frameInPhase = g_BenchmarkFrame % phaseLength;
        if (frameInPhase == BenchmarkWarmupFrames)
        {
            if (const ForestGrass* grass = game->GetObj<ForestGrass>("ForestGrass"))
                log("grass: planted=" + std::to_string(grass->GetBladeCount()) +
                    " drawn=" + std::to_string(grass->GetDrawnBladeCount()) +
                    " chunks=" + std::to_string(grass->GetDrawnChunkCount()));
        }
        // ここで測れるのは直前のフレームの値。条件を切り替えた直後のフレームは捨てる区間に入るようにしている。
        if (frameInPhase >= BenchmarkWarmupFrames && frameMs > 0.0)
        {
            BenchmarkResult& result = g_BenchmarkResults[g_BenchmarkSpot];
            BenchmarkSamples& samples = lightOn ? result.On : result.Off;
            samples.FrameMs.push_back(frameMs);
            samples.CpuMs.push_back((std::max)(frameMs - g_LastPresentMs, 0.0));
            // 結果が届いている描画段階だけ、GPU時間を足している
            if (GpuTimer* timer = game->GetGpuTimer())
            {
                for (size_t pass = 0; pass < static_cast<size_t>(GpuPass::Count); ++pass)
                {
                    const GpuTiming timing = timer->GetTiming(static_cast<GpuPass>(pass));
                    if (timing.Status == GpuTimingStatus::Available)
                    {
                        samples.GpuSum[pass] += timing.Milliseconds;
                        ++samples.GpuCount[pass];
                    }
                }
            }
        }

        // 毎フレーム、地点の位置と向きに立たせ直し、ライトの状態を決めている
        const BenchmarkSpot& spot = GetBenchmarkSpots()[g_BenchmarkSpot];
        player->SetPosition(spot.Position);
        player->SetFlashlightOn(lightOn);
        player->RefillBattery();    // 電池切れの点滅で結果が揺れないよう、電池を満タンに保っている
        camera->SetMouseLookEnabled(false);
        camera->SetCameraDirection(spot.Yaw);
        camera->SetCameraPitch(spot.Pitch);
        // --figures のときは、水たまりの人影を正面26先に、見えない影を正面34先の少し右に置き、どちらも見え方1で描かせている
        if (figures)
        {
            const Vector3 forward(std::sin(spot.Yaw), 0.0f, std::cos(spot.Yaw));
            const Vector3 right(forward.z, 0.0f, -forward.x);
            Vector3 puddleFoot = spot.Position + forward * BenchmarkPuddleFigureAhead;
            Vector3 unseenFoot = spot.Position + forward * BenchmarkUnseenFigureAhead + right * BenchmarkUnseenFigureRight;
            puddleFoot.y = BenchmarkFigureFloorY;
            unseenFoot.y = BenchmarkFigureFloorY;
            Renderer::SetFigureOverride(true, puddleFoot, unseenFoot);
        }

        // OFFとONの両方を測り終えたら、次の地点へ進んでいる
        if (++g_BenchmarkFrame >= phaseLength * BenchmarkPhaseCount)
        {
            g_BenchmarkFrame = 0;
            ++g_BenchmarkSpot;
        }
        return false;
    }
}

namespace Tools::CaptureMode
{
    // Presentにかかった時間を記録している（CPU時間を求めるのに使っている）
    void OnPresentTimed(double milliseconds)
    {
        g_LastPresentMs = milliseconds;
    }
}
