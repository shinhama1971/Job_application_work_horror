// ============================================================================
// ファイルの役割: 自動撮影の道順（キーフレーム）のデータ。時刻と時刻の間は、位置と向きを直線で補間する。
// Shot に名前があるキーフレームの時刻で、スクリーンショットを1枚保存する。
// ============================================================================

#include "CaptureTours.h"

#include <algorithm>
#include <iterator>

using namespace DirectX::SimpleMath;

namespace
{
    // 角度の補間。差を-π〜πに直してから補間し、遠回りの方向へ回らないようにしている
    float LerpAngle(float from, float to, float t)
    {
        float delta = std::fmod(to - from, Tools::CaptureTours::Pi * 2.0f);
        if (delta > Tools::CaptureTours::Pi) delta -= Tools::CaptureTours::Pi * 2.0f;
        if (delta < -Tools::CaptureTours::Pi) delta += Tools::CaptureTours::Pi * 2.0f;
        return from + delta * t;
    }
}

namespace Tools::CaptureTours
{
    // 道順の中で、指定した時刻の位置と向きを、前後のキーフレームから補間して求めている
    Keyframe Sample(const std::vector<Keyframe>& tour, float time)
    {
        // 最初のキーフレームより前は最初の位置、最後より後は最後の位置のままにしている
        if (time <= tour.front().Time)
        {
            return tour.front();
        }
        for (size_t index = 1; index < tour.size(); ++index)
        {
            const Keyframe& next = tour[index];
            if (time > next.Time)
            {
                continue;
            }
            const Keyframe& previous = tour[index - 1];
            // 同時刻のキーフレームがあっても、補間率の分母をゼロにしない。
            constexpr float MinimumKeyframeIntervalSeconds = 0.001f;
            const float span = (std::max)(next.Time - previous.Time, MinimumKeyframeIntervalSeconds);
            const float t = (time - previous.Time) / span;
            Keyframe result = previous;
            result.Position = Vector3::Lerp(previous.Position, next.Position, t);
            result.Yaw = LerpAngle(previous.Yaw, next.Yaw, t);
            result.Pitch = previous.Pitch + (next.Pitch - previous.Pitch) * t;
            // 隠れる区間かどうかは補間せず、前のキーフレームの値を使っている
            result.Hide = previous.Hide;
            return result;
        }
        return tour.back();
    }

    // 1面: 開始地点 → 中央ホール → 左の倉庫 → 暗証番号の扉 → ループ廊下 → 西棟（浸水した機械室） → 書類保管室 → 南西の広間 → 光の安全地帯 → 足音の波紋 → 壁の血の手形
    const std::vector<Keyframe> Stage1Tour =
    {
        { 0.0f, Vector3(0.0f, FloorY, -120.0f), Pi, -0.12f, nullptr, false },
        { 4.5f, Vector3(0.0f, FloorY, -128.0f), YawToward(0.0f, -128.0f, -42.0f, -178.0f), -0.12f, L"01_1面_開始地点_照らすと浮かぶ壁の文字", false },
        { 8.0f, Vector3(0.0f, FloorY, -90.0f), 0.0f, 0.0f, nullptr, false },
        { 13.0f, Vector3(0.0f, FloorY, -10.0f), YawToward(0.0f, -10.0f, -180.0f, 35.0f), 0.0f, nullptr, false },
        { 15.5f, Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f), -0.05f, L"02_1面_中央ホールと配電盤", false },
        { 17.5f, Vector3(-10.0f, FloorY, 0.0f), YawToward(-10.0f, 0.0f, -180.0f, 35.0f) + 0.6f, 0.95f, L"02b_1面_中央ホールの天井", false },
        { 19.5f, Vector3(-60.0f, FloorY, -88.0f), YawToward(-60.0f, -88.0f, -150.0f, -140.0f), 0.0f, nullptr, false },
        { 24.0f, Vector3(-150.0f, FloorY, -110.0f), 0.0f, -0.26f, L"03_1面_左の倉庫の壁の文字", false },
        { 28.0f, Vector3(-60.0f, FloorY, -30.0f), YawToward(-60.0f, -30.0f, 0.0f, 40.0f), 0.0f, nullptr, false },
        // 中央ホールで懐中電灯を消した画面（天井の照明の光だまりの外は、ほとんど闇）
        { 29.0f, Vector3(-45.0f, FloorY, -5.0f), YawToward(-45.0f, -5.0f, 120.0f, -20.0f), 0.08f, L"02c_1面_懐中電灯を消した中央ホール", false },
        { 32.0f, Vector3(0.0f, FloorY, 70.0f), 0.0f, 0.0f, nullptr, false },
        { 36.5f, Vector3(12.0f, FloorY, 112.0f), Pi * 0.5f, -0.05f, L"04_1面_暗証番号の扉", false },
        { 41.0f, Vector3(0.0f, FloorY, 185.0f), 0.0f, 0.0f, nullptr, false },
        { 46.5f, Vector3(0.0f, FloorY, 240.0f), 0.0f, -0.10f, L"05_1面_ループ廊下の突き当たり", false },
        { 50.05f, Vector3(0.0f, FloorY, 245.0f), 0.0f, -0.10f, nullptr, false },
        // 西棟（浸水した機械室）。入口の内側へ場面を切り替え、柱の並ぶ広間と排水バルブを写し、仕切りの切れ目から奥のポンプ室へ進んでいる。
        // 切り替えの前後を同じ時刻にして、補間で書類保管室を通り、入室の無線が先に流れるのを防いでいる。
        { 50.05f, Vector3(-232.0f, FloorY, -20.0f), YawToward(-232.0f, -20.0f, -330.0f, 140.0f), -0.16f, nullptr, false },
        { 52.5f, Vector3(-233.0f, FloorY, -21.0f), YawToward(-233.0f, -21.0f, -330.0f, 140.0f), -0.16f, L"05b_1面_西棟の柱の並ぶ浸水した広間", false },
        { 55.0f, Vector3(-292.0f, FloorY, -66.0f), Pi, -0.62f, nullptr, false },
        { 57.0f, Vector3(-292.0f, FloorY, -67.0f), Pi, -0.62f, L"05b2_1面_西棟の排水バルブ", false },
        { 58.5f, Vector3(-295.0f, FloorY, -70.0f), 0.0f, -0.08f, nullptr, false },
        // 水の中を奥へ進みながら足元の水面をのぞくと、水面にだけ、少し先の天井に逆さまに張り付いた人影が映る。見上げると何もいない
        { 59.6f, Vector3(-298.0f, FloorY, 20.0f), 0.0f, -0.08f, nullptr, false },
        { 60.2f, Vector3(-298.0f, FloorY, 20.0f), 0.0f, -1.00f, nullptr, false },
        { 61.4f, Vector3(-298.0f, FloorY, 20.0f), 0.0f, -1.00f, L"05b3_1面_西棟の水面に映る天井の人影", false },
        { 61.9f, Vector3(-298.0f, FloorY, 20.0f), 0.0f, 0.35f, nullptr, false },
        { 62.6f, Vector3(-298.0f, FloorY, 20.0f), 0.0f, 0.35f, L"05b4_1面_見上げると何もいない", false },
        { 64.0f, Vector3(-300.0f, FloorY, 150.0f), 0.0f, -0.08f, nullptr, false },
        { 67.0f, Vector3(-300.0f, FloorY, 195.0f), YawToward(-300.0f, 195.0f, -345.0f, 235.0f), -0.12f, nullptr, false },
        { 72.5f, Vector3(-262.0f, FloorY, 196.0f), YawToward(-262.0f, 196.0f, -320.0f, 240.0f), -0.30f, nullptr, false },
        { 75.0f, Vector3(-262.0f, FloorY, 197.0f), YawToward(-262.0f, 197.0f, -320.0f, 240.0f), -0.30f, L"05c_1面_西棟のポンプ室と排水口", false },
        { 77.0f, Vector3(-262.0f, FloorY, 197.0f), YawToward(-262.0f, 197.0f, -320.0f, 240.0f), -0.30f, nullptr, false },
        // 書類保管室（棚が並ぶ迷路）。棚の間から奥を向き、懐中電灯の影（手元から作る影と、距離で柔らかくなる縁）を写している。
        { 77.05f, Vector3(-60.0f, FloorY, 115.0f), YawToward(-60.0f, 115.0f, -200.0f, 118.0f), -0.12f, nullptr, false },
        { 79.5f, Vector3(-62.0f, FloorY, 115.0f), YawToward(-62.0f, 115.0f, -200.0f, 118.0f), -0.12f, L"05d_1面_書類保管室の棚と影", false },
        // 入ってから4秒で「照らすと止まる影」が部屋の奥に現れ、光の外を回り込んで近づき、ライトの中に入ると止まる。
        { 81.0f, Vector3(-62.0f, FloorY, 115.0f), YawToward(-62.0f, 115.0f, -200.0f, 118.0f), -0.12f, nullptr, false },
        { 84.0f, Vector3(-62.0f, FloorY, 115.0f), YawToward(-62.0f, 115.0f, -200.0f, 118.0f), -0.12f, L"05e_1面_書類保管室の照らすと止まる影", false },
        { 87.0f, Vector3(-62.0f, FloorY, 115.0f), YawToward(-62.0f, 115.0f, -200.0f, 118.0f), -0.12f, L"05f_1面_照らされて止まった影", false },
        { 88.5f, Vector3(-62.0f, FloorY, 115.0f), YawToward(-62.0f, 115.0f, -200.0f, 118.0f), -0.12f, nullptr, false },
        // 南西の広間（左の倉庫の南の入口から入る部屋）。入口から柱の並ぶ広間を見渡し、奥の棚と机の一角へ進む。
        { 88.55f, Vector3(-160.0f, FloorY, -150.0f), Pi, -0.10f, nullptr, false },
        { 90.5f, Vector3(-160.0f, FloorY, -196.0f), YawToward(-160.0f, -196.0f, -300.0f, -270.0f), -0.14f, nullptr, false },
        { 92.5f, Vector3(-160.0f, FloorY, -196.0f), YawToward(-160.0f, -196.0f, -300.0f, -270.0f), -0.14f, L"05g_1面_南西の広間", false },
        { 95.5f, Vector3(-280.0f, FloorY, -262.0f), YawToward(-280.0f, -262.0f, -350.0f, -290.0f), -0.20f, nullptr, false },
        { 97.0f, Vector3(-280.0f, FloorY, -262.0f), YawToward(-280.0f, -262.0f, -350.0f, -290.0f), -0.20f, L"05h_1面_南西の広間の棚と机", false },
        { 98.0f, Vector3(-280.0f, FloorY, -262.0f), YawToward(-280.0f, -262.0f, -350.0f, -290.0f), -0.20f, nullptr, false },
        // 光の安全地帯（中央ホールの西の裸電球）。少し離れた暗がりから暖かい灯りを見て、灯りの下まで歩き、落ち着いた画面を写している。
        { 98.05f, Vector3(-150.0f, FloorY, -30.0f), YawToward(-150.0f, -30.0f, -198.0f, 12.0f), 0.02f, nullptr, false },
        { 100.5f, Vector3(-151.0f, FloorY, -29.0f), YawToward(-151.0f, -29.0f, -198.0f, 12.0f), 0.02f, L"05i_1面_光の安全地帯の裸電球", false },
        { 103.5f, Vector3(-204.0f, FloorY, 18.0f), YawToward(-204.0f, 18.0f, -150.0f, -40.0f), -0.18f, nullptr, false },
        { 106.0f, Vector3(-204.0f, FloorY, 17.0f), YawToward(-204.0f, 17.0f, -150.0f, -40.0f), -0.18f, L"05j_1面_光の安全地帯の灯りの下", false },
        { 107.0f, Vector3(-204.0f, FloorY, 17.0f), YawToward(-204.0f, 17.0f, -150.0f, -40.0f), -0.18f, nullptr, false },
        // 足音の波紋（西棟の浸水した広間を歩いて振り返る）。歩いた跡から波が広がって重なり、懐中電灯の光の中で輪が光る。
        { 107.05f, Vector3(-300.0f, FloorY, -88.0f), 0.0f, -0.32f, nullptr, false },
        { 111.0f, Vector3(-300.0f, FloorY, -40.0f), 0.0f, -0.32f, nullptr, false },
        { 112.0f, Vector3(-300.0f, FloorY, -39.0f), Pi, -0.45f, nullptr, false },
        { 112.6f, Vector3(-300.0f, FloorY, -39.0f), Pi, -0.45f, L"05k_1面_足音の波紋と波打つ反射", false },
        { 113.5f, Vector3(-300.0f, FloorY, -39.0f), Pi, -0.45f, nullptr, false },
        // 壁のホラーらしい跡（右の倉庫の東の壁の血の手形と、下へ引きずった跡）。懐中電灯で照らすと赤黒く浮かぶ。
        { 113.55f, Vector3(168.0f, FloorY, -136.0f), YawToward(168.0f, -136.0f, 218.0f, -121.6f), -0.30f, nullptr, false },
        { 116.0f, Vector3(170.0f, FloorY, -135.0f), YawToward(170.0f, -135.0f, 218.0f, -121.6f), -0.30f, L"05l_1面_右の倉庫の壁の血の手形", false },
        { 117.0f, Vector3(170.0f, FloorY, -135.0f), YawToward(170.0f, -135.0f, 218.0f, -121.6f), -0.30f, nullptr, false },
        // ループ廊下の突き当たりの最後のドア。開けた瞬間（Stage1EndDoorOpenTime）に開始地点の奥のドアの裏側へ移り、
        // 開いていくドアの向こうに開始地点が見える（P.T.のように、ドアの先が最初の場所へそのまま続いている）。
        // 部屋へ入ってドアに背を向けると、後ろでドアが閉まり、振り返ると閉じたドアだけが残っている。
        // 戻ってきた部屋の壁には、1周目の文字「おかえり」がじわりと浮かんでいる。
        { 117.05f, Vector3(171.0f, FloorY, 255.0f), Pi * 0.5f, 0.02f, nullptr, false },
        { 118.5f, Vector3(171.0f, FloorY, 255.0f), Pi * 0.5f, 0.02f, L"05m_1面_ループ廊下の突き当たりのドア", false },
        { 119.0f, Vector3(171.0f, FloorY, 255.0f), Pi * 0.5f, 0.02f, nullptr, false },
        { 119.05f, Vector3(0.0f, FloorY, -205.0f), 0.0f, 0.02f, nullptr, false },
        { 119.8f, Vector3(0.0f, FloorY, -205.0f), 0.0f, 0.02f, L"05n_1面_ドアの先は開始地点", false },
        { 121.5f, Vector3(0.0f, FloorY, -172.0f), 0.0f, 0.0f, nullptr, false },
        { 122.8f, Vector3(0.0f, FloorY, -140.0f), 0.0f, 0.0f, nullptr, false },
        { 123.8f, Vector3(0.0f, FloorY, -140.0f), YawToward(0.0f, -140.0f, 34.0f, -178.0f), -0.05f, nullptr, false },
        { 124.4f, Vector3(0.0f, FloorY, -140.0f), YawToward(0.0f, -140.0f, 34.0f, -178.0f), -0.05f, L"05o_1面_振り返ると閉じたドアと浮かんだ文字", false },
        { 125.0f, Vector3(0.0f, FloorY, -140.0f), YawToward(0.0f, -140.0f, 34.0f, -178.0f), -0.05f, nullptr, false },
        // 1周した後、南北の廊下の奥で来た方（南のドア）を振り返る。振り返った直後と、見つめ続けて廊下が伸びた後を写している
        { 125.05f, Vector3(0.0f, FloorY, 200.0f), Pi, -0.02f, nullptr, false },
        { 125.6f, Vector3(0.0f, FloorY, 200.0f), Pi, -0.02f, L"05p_1面_振り返ったループ廊下", false },
        { 131.0f, Vector3(0.0f, FloorY, 200.0f), Pi, -0.02f, L"05q_1面_見つめている間に伸びた廊下", false },
        { 131.5f, Vector3(0.0f, FloorY, 200.0f), Pi, -0.02f, nullptr, false },
        // 1周した後のループ廊下の東の区間。南の壁（LoopWall3）が、息をするように膨らんでは戻る。
        // 壁に沿って奥を見た画面と、壁を正面から照らした画面（光の当たった所だけ平らに止まる）を、膨らみ切った時刻に写している
        { 131.55f, Vector3(178.0f, FloorY, 264.0f), YawToward(178.0f, 264.0f, 60.0f, 241.0f), -0.06f, nullptr, false },
        { 135.7f, Vector3(178.0f, FloorY, 264.0f), YawToward(178.0f, 264.0f, 60.0f, 241.0f), -0.06f, L"05r_1面_息をする壁", false },
        { 136.5f, Vector3(120.0f, FloorY, 262.0f), Pi, -0.12f, nullptr, false },
        { 140.3f, Vector3(120.0f, FloorY, 262.0f), Pi, -0.12f, L"05s_1面_照らしても止まらない壁", false },
        { 140.8f, Vector3(120.0f, FloorY, 262.0f), Pi, -0.12f, nullptr, false },
        // 3周目で3本目のヒューズを拾った後（BreathingBuildingRequestTime）。中央ホールに戻ると、部屋を囲む壁と外周の壁が
        // 面全体で息をしている。膨らむ量は少しずつ大きくなるので、しばらく待ってから写している
        { 141.05f, Vector3(-60.0f, FloorY, -40.0f), YawToward(-60.0f, -40.0f, -200.0f, 40.0f), -0.04f, nullptr, false },
        { 152.0f, Vector3(-60.0f, FloorY, -40.0f), YawToward(-60.0f, -40.0f, -200.0f, 40.0f), -0.04f, L"05t_1面_ヒューズ3本の後に息をする中央ホール", false },
        { 154.3f, Vector3(-60.0f, FloorY, -40.0f), YawToward(-60.0f, -40.0f, -200.0f, 40.0f), -0.04f, L"05u_1面_息を吐いた中央ホール", false },
        { 154.8f, Vector3(-60.0f, FloorY, -40.0f), YawToward(-60.0f, -40.0f, -200.0f, 40.0f), -0.04f, nullptr, false },
        // ヒューズ3本を持った状態でループ廊下に入ると、通常の条件で赤い水が押し寄せる。
        // 右の通路から流れ込む瞬間、南へ曲がった波、接触後の水没をそれぞれ撮る。
        { 155.05f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, nullptr, false },
        { 156.0f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, L"05v_1面_廊下の照明が消える", false },
        // 角を曲がる前後は、廊下の西寄り（角が見える所）の同じ地点から見て、波が少しずつ向きを変えるのを写す。
        { 157.35f, Vector3(-35.0f, FloorY, 185.0f), YawToward(-35.0f, 185.0f, 35.0f, 250.0f), -0.16f, L"05w_1面_右の通路から水が流れ込む", false },
        { 157.60f, Vector3(-35.0f, FloorY, 185.0f), YawToward(-35.0f, 185.0f, 35.0f, 250.0f), -0.16f, L"05wa_1面_右から来た波が角を曲がる", false },
        { 157.88f, Vector3(-35.0f, FloorY, 185.0f), YawToward(-35.0f, 185.0f, 35.0f, 250.0f), -0.16f, L"05waa_1面_波頭が南へ向く", false },
        { 158.15f, Vector3(-35.0f, FloorY, 185.0f), YawToward(-35.0f, 185.0f, 35.0f, 250.0f), -0.16f, L"05wab_1面_向きを変えた波が迫る", false },
        { 159.20f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, L"05wb_1面_廊下を押し寄せる濁流", false },
        { 160.25f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, L"05x_1面_目の前まで来た赤い水", false },
        { 161.05f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, L"05y_1面_赤い水に呑まれる視界", false },
        { 161.55f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, L"05z_1面_水の中に沈んだ視界", false },
        { 162.5f, Vector3(0.0f, FloorY, 150.0f), 0.0f, -0.16f, nullptr, false },
        // 時間が止まった部屋（右の倉庫）。入ると世界の時計がゆっくり止まり、天井から落ちる水滴と床の波が宙で止まる。
        // 止まりきって少しすると、一番遠い隅に人影が現れ、止まった部屋の中を近づいてくる。
        // 人影を正面から照らすと崩れて時間が戻るので、人影の写真は、視線を少し横（約20度）へずらして写している
        { 162.55f, Vector3(110.0f, FloorY, -90.0f), YawToward(110.0f, -90.0f, 150.0f, -128.0f), -0.40f, nullptr, false },
        { 165.2f, Vector3(110.0f, FloorY, -90.0f), YawToward(110.0f, -90.0f, 150.0f, -128.0f), -0.40f, L"05za_1面_時間が止まった部屋", false },
        { 165.6f, Vector3(110.0f, FloorY, -90.0f), YawToward(110.0f, -90.0f, 204.0f, -164.0f) + 0.35f, -0.10f, nullptr, false },
        { 168.6f, Vector3(110.0f, FloorY, -90.0f), YawToward(110.0f, -90.0f, 204.0f, -164.0f) + 0.35f, -0.10f, L"05zb_1面_止まった部屋で近づく人影", false },
        { 169.0f, Vector3(110.0f, FloorY, -90.0f), YawToward(110.0f, -90.0f, 204.0f, -164.0f) + 0.35f, -0.10f, nullptr, false },
        // 次の場面へ飛ぶ間の補間が、開始地点の部屋（音で輪郭が浮かぶ部屋）を通らないよう、中央ホールを回っている
        { 169.02f, Vector3(110.0f, FloorY, -40.0f), 0.0f, -0.05f, nullptr, false },
        { 169.04f, Vector3(-330.0f, FloorY, -40.0f), 0.0f, -0.05f, nullptr, false },
        // 傾く部屋（南西の広間）。入口から奥へ行くほど、視界がゆっくり傾いていく
        { 169.05f, Vector3(-330.0f, FloorY, -228.0f), YawToward(-330.0f, -228.0f, -200.0f, -300.0f), -0.05f, nullptr, false },
        { 172.8f, Vector3(-330.0f, FloorY, -228.0f), YawToward(-330.0f, -228.0f, -200.0f, -300.0f), -0.05f, L"05zc_1面_奥へ行くほど傾く広間", false },
        { 173.3f, Vector3(-330.0f, FloorY, -228.0f), YawToward(-330.0f, -228.0f, -200.0f, -300.0f), -0.05f, nullptr, false },
        // 音で輪郭が浮かぶ真っ暗な部屋（開始地点の部屋。2周した後に戻ると起きる）。照明も懐中電灯も消えた真っ暗な部屋を、
        // 北へ歩くと一歩ごとに足元から音の輪が広がって部屋の形が浮かび、立ち止まると、奥の人影の足音の輪だけが見える
        { 173.35f, Vector3(0.0f, FloorY, -166.0f), 0.0f, -0.12f, nullptr, false },
        { 174.4f, Vector3(0.0f, FloorY, -166.0f), 0.0f, -0.12f, L"05zd_1面_真っ暗になった開始地点の部屋", false },
        // 歩き出して最初の一歩の輪が、床の見える所（足元から30〜60）まで広がった瞬間を、少し下を向いて写している
        { 174.5f, Vector3(0.0f, FloorY, -166.0f), 0.0f, -0.55f, nullptr, false },
        { 175.75f, Vector3(0.0f, FloorY, -142.25f), 0.0f, -0.55f, L"05ze_1面_足音で浮かぶ部屋の輪郭", false },
        { 176.5f, Vector3(0.0f, FloorY, -128.0f), 0.0f, -0.12f, nullptr, false },
        { 178.6f, Vector3(0.0f, FloorY, -128.0f), 0.45f, -0.08f, L"05zf_1面_足音の輪だけで見える人影", false },
        { 179.0f, Vector3(0.0f, FloorY, -128.0f), 0.45f, -0.08f, nullptr, false },
    };

    // 2面: ループ廊下 → 時計 → 肖像画 → ロッカー（中に隠れる）→ 奥の扉 → 鏡（普段・鏡の異変で人影が映る・振り返ると誰もいない）
    const std::vector<Keyframe> Stage2Tour =
    {
        { 0.0f, Vector3(0.0f, FloorY, -125.0f), 0.0f, 0.0f, nullptr, false },
        { 4.5f, Vector3(0.0f, FloorY, -118.0f), 0.0f, 0.0f, L"06_2面_ループ廊下", false },
        // 天井から吊るした裸電球の下へ近づき、電球の光が作る影（ロッカーや人の影が、揺れに合わせて床と壁の上を動く）を写している
        { 8.0f, Vector3(-8.0f, FloorY, -38.0f), YawToward(-8.0f, -38.0f, 10.0f, 30.0f), -0.30f, nullptr, false },
        { 9.0f, Vector3(-8.0f, FloorY, -37.0f), YawToward(-8.0f, -37.0f, 10.0f, 30.0f), -0.30f, L"06b_2面_揺れる電球の影", false },
        // 電球を背にして足元を見ると、電球の光を遮る自分の体の影が床に伸びている（figureShadow.hlsli の GetPlayerBodyShadow）
        { 9.6f, Vector3(0.0f, FloorY, -6.0f), Pi, -1.00f, nullptr, false },
        { 11.0f, Vector3(0.0f, FloorY, -6.0f), Pi, -1.00f, L"06c_2面_電球を背に伸びる自分の影", false },
        { 12.0f, Vector3(0.0f, FloorY, -25.0f), -Pi * 0.5f, -0.10f, L"07_2面_時計", false },
        { 15.5f, Vector3(0.0f, FloorY, -25.0f), Pi * 0.5f, -0.10f, L"08_2面_肖像画", false },
        { 19.5f, Vector3(14.0f, FloorY, 40.0f), Pi * 0.5f, -0.12f, L"09_2面_ロッカー", false },
        { 20.5f, Vector3(14.0f, FloorY, 40.0f), Pi * 0.5f, -0.12f, nullptr, true },
        { 24.5f, Vector3(14.0f, FloorY, 40.0f), -Pi * 0.5f, 0.0f, L"10_2面_ロッカーの中から覗く", true },
        { 27.0f, Vector3(14.0f, FloorY, 40.0f), -Pi * 0.5f, 0.0f, nullptr, false },
        { 33.0f, Vector3(0.0f, FloorY, 108.0f), 0.0f, 0.0f, L"11_2面_奥の扉", false },
        { 36.0f, Vector3(0.0f, FloorY, 112.0f), 0.0f, 0.0f, nullptr, false },
        { 38.0f, Vector3(0.0f, FloorY, 37.0f), -Pi * 0.5f, -0.06f, nullptr, false },
        { 39.0f, Vector3(0.0f, FloorY, 37.0f), -Pi * 0.5f, -0.06f, L"12_2面_鏡", false },
        // 鏡の異変の周回へ進んだ直後：左の壁の赤い印が、焦げた縁が広がるように浮かび上がる途中
        { 40.9f, Vector3(0.0f, FloorY, 37.0f), -Pi * 0.5f, -0.06f, L"12b_2面_焦げて浮かぶ印", false },
        { 46.0f, Vector3(0.0f, FloorY, 37.0f), -Pi * 0.5f, -0.06f, L"13_2面_鏡に映る人影", false },
        { 47.0f, Vector3(0.0f, FloorY, 37.0f), -Pi * 0.5f, -0.06f, nullptr, false },
        { 48.2f, Vector3(0.0f, FloorY, 37.0f), Pi * 0.5f, -0.06f, nullptr, false },
        { 48.8f, Vector3(0.0f, FloorY, 37.0f), Pi * 0.5f, -0.06f, L"14_2面_振り返ると誰もいない", false },
        { 50.0f, Vector3(0.0f, FloorY, 37.0f), Pi * 0.5f, -0.06f, nullptr, false },
        // 2周目へ進めた後、水たまりを踏んで乾いた床へ出て、振り返って濡れた足跡（自分の靴の跡と、後をついて来る誰かの裸足の跡）を見る
        { 51.0f, Vector3(0.0f, FloorY, 37.0f), Pi, -0.10f, nullptr, false },
        { 53.5f, Vector3(0.0f, FloorY, 5.0f), Pi, -0.10f, nullptr, false },
        { 57.5f, Vector3(0.0f, FloorY, -60.0f), Pi, -0.10f, nullptr, false },
        { 60.0f, Vector3(0.0f, FloorY, -60.0f), Pi, -0.10f, nullptr, false },
        { 60.6f, Vector3(0.0f, FloorY, -60.0f), 0.0f, -0.75f, nullptr, false },
        { 61.0f, Vector3(0.0f, FloorY, -60.0f), 0.0f, -0.75f, L"15_2面_濡れた足跡", false },
        { 62.0f, Vector3(0.0f, FloorY, -60.0f), 0.0f, -0.75f, nullptr, false },
        // 肖像画の異変の周回にして、肖像画を照らして見つめ続ける（絵の中の目が見開く）。その後、横へ動いて、瞳が目で追うのを見る
        { 64.5f, Vector3(0.0f, FloorY, -25.0f), Pi * 0.5f, -0.10f, nullptr, false },
        { 66.0f, Vector3(0.0f, FloorY, -25.0f), Pi * 0.5f, -0.10f, L"16_2面_肖像画を見つめる", false },
        { 69.5f, Vector3(0.0f, FloorY, -25.0f), Pi * 0.5f, -0.10f, L"17_2面_肖像画の目が開く", false },
        { 71.5f, Vector3(14.0f, FloorY, -5.0f), YawToward(14.0f, -5.0f, 38.9f, -25.0f), -0.12f, nullptr, false },
        { 72.0f, Vector3(14.0f, FloorY, -5.0f), YawToward(14.0f, -5.0f, 38.9f, -25.0f), -0.12f, L"18_2面_目で追う肖像画", false },
        { 73.0f, Vector3(14.0f, FloorY, -5.0f), YawToward(14.0f, -5.0f, 38.9f, -25.0f), -0.12f, nullptr, false },
        // 時計の前で、電池が弱った懐中電灯の光を見る（07 の満タンの光と見比べられる）
        { 74.5f, Vector3(0.0f, FloorY, -25.0f), -Pi * 0.5f, -0.10f, nullptr, false },
        { 76.0f, Vector3(0.0f, FloorY, -25.0f), -Pi * 0.5f, -0.10f, L"19_2面_電池が弱った懐中電灯", false },
        { 77.0f, Vector3(0.0f, FloorY, -25.0f), -Pi * 0.5f, -0.10f, nullptr, false },
        // 左の壁に現れた偽の扉（羽目板・木目・剥げた塗装・部屋番号の札）を、少し斜めから見る
        { 80.0f, Vector3(4.0f, FloorY, 46.0f), YawToward(4.0f, 46.0f, -39.3f, 70.0f), -0.08f, nullptr, false },
        { 81.5f, Vector3(4.0f, FloorY, 46.0f), YawToward(4.0f, 46.0f, -39.3f, 70.0f), -0.08f, L"20_2面_偽の扉", false },
        { 82.5f, Vector3(4.0f, FloorY, 46.0f), YawToward(4.0f, 46.0f, -39.3f, 70.0f), -0.08f, nullptr, false },
        // 見えない影の異変：廊下の奥から姿の見えない影が歩いてくる。姿は映らないが、吊るした裸電球と天井の照明の下を通ると、
        // 床と壁に人の形の影だけが落ちる（奥の裸電球の手前に立ち、影が電球の下を通って近づくのを、捕まる前まで見ている。懐中電灯で照らすと気づかれて速く寄ってくるので、少し左を向いて照らさないようにしている）
        { 83.6f, Vector3(0.0f, FloorY, 32.0f), -0.35f, -0.10f, nullptr, false },
        { 90.6f, Vector3(0.0f, FloorY, 32.0f), -0.35f, -0.10f, L"20e_2面_見えない影が落とす影", false },
        { 91.6f, Vector3(0.0f, FloorY, 32.0f), -0.35f, -0.16f, L"20f_2面_近づいてくる影だけの人影", false },
        { 92.0f, Vector3(0.0f, FloorY, 32.0f), -0.35f, -0.16f, nullptr, false },
        { 92.6f, Vector3(-30.0f, FloorY, 32.0f), Pi, -0.10f, nullptr, false },
        // 中央の水たまりの手前で足元をのぞくと、水面にだけ、向こう側に立つ人影が逆さまに映る（のぞき続けると一歩ずつ近づく）。
        // 顔を上げると、そこには誰もいない
        // （水たまりを踏んで波を立てないよう、左の壁際を回って手前へ出ている）
        { 94.3f, Vector3(-30.0f, FloorY, -18.0f), Pi * 0.5f, -0.10f, nullptr, false },
        { 94.7f, Vector3(9.0f, FloorY, -18.0f), 0.0f, -0.10f, nullptr, false },
        { 95.3f, Vector3(9.0f, FloorY, -18.0f), 0.0f, -0.95f, nullptr, false },
        { 96.4f, Vector3(9.0f, FloorY, -18.0f), 0.0f, -0.95f, L"20b_2面_水たまりに映る人影", false },
        { 98.4f, Vector3(9.0f, FloorY, -18.0f), 0.0f, -0.95f, L"20c_2面_近づいてくる水面の人影", false },
        { 98.8f, Vector3(9.0f, FloorY, -18.0f), 0.0f, -0.05f, nullptr, false },
        { 99.6f, Vector3(9.0f, FloorY, -18.0f), 0.0f, -0.05f, L"20d_2面_顔を上げると誰もいない", false },
        // 最後の停電：肖像画が目を見開き、振り返って床を見ると、照明を消しながら濡れた足跡を残して影が迫ってくる
        // （影の胸を視線の中心に入れると崩れて消えるため、影の通り道を視界の端に入れて、斜め後ろの床を見ている）
        { 100.5f, Vector3(0.0f, FloorY, -8.0f), YawToward(0.0f, -8.0f, 38.9f, -25.0f), -0.06f, nullptr, false },
        { 101.5f, Vector3(0.0f, FloorY, -8.0f), YawToward(0.0f, -8.0f, 38.9f, -25.0f), -0.06f, L"21_2面_停電で目を開く肖像画", false },
        { 102.0f, Vector3(25.0f, FloorY, -8.0f), YawToward(25.0f, -8.0f, -30.0f, -40.0f), -0.50f, nullptr, false },
        { 102.5f, Vector3(25.0f, FloorY, -8.0f), YawToward(25.0f, -8.0f, -30.0f, -40.0f), -0.50f, L"22_2面_最後の追跡", false },
        { 103.0f, Vector3(25.0f, FloorY, -8.0f), YawToward(25.0f, -8.0f, -30.0f, -40.0f), -0.50f, nullptr, false },
        // 中央の水たまりの奥側へ走り抜け、振り返って足元の水面をのぞく。追っ手を直接見ずに、水面に映った影で位置を確かめている
        { 103.6f, Vector3(9.0f, FloorY, 36.0f), Pi, -0.90f, nullptr, false },
        { 104.4f, Vector3(9.0f, FloorY, 36.0f), Pi, -0.90f, L"22b_2面_水面に映る追っ手", false },
        { 104.8f, Vector3(9.0f, FloorY, 36.0f), Pi, -0.90f, nullptr, false },
    };

    // 同じカメラ位置で、実際のドア更新と不可視の影の移動を撮る。足跡は注入しない。
    const std::vector<Keyframe> FeatureStage1 = {
        { 0.0f, Vector3(0,FloorY,-128), Pi, -0.05f, nullptr, false },
        { 9.0f, Vector3(0,FloorY,-128), Pi, -0.05f, L"feature_door_1_open", false },
        { 12.0f, Vector3(0,FloorY,-128), Pi, -0.05f, L"feature_door_2_lit_closed", false },
        { 20.0f, Vector3(0,FloorY,-128), Pi, -0.05f, L"feature_door_3_reopen", false },
        { 21.0f, Vector3(0,FloorY,-128), Pi, -0.05f, nullptr, false },
        { 21.05f, Vector3(-292,FloorY,-79), Pi, -0.98f, nullptr, false },
        { 24.0f, Vector3(-292,FloorY,-79), Pi, -0.98f, L"feature_valve_close", false },
        { 25.0f, Vector3(-292,FloorY,-79), Pi, -0.98f, nullptr, false },
        // 同じ位置・向きで通常表示の点灯と消灯を撮り、表示の切り替えを確認する。
        { 25.10f, Vector3(0,FloorY,220), 0.0f, -0.42f, nullptr, false },
        { 26.5f, Vector3(0,FloorY,220), 0.0f, -0.42f, L"feature_corridor_light_on", false },
        { 29.8f, Vector3(0,FloorY,220), 0.0f, -0.42f, L"feature_corridor_light_off", false },
        { 31.0f, Vector3(0,FloorY,220), 0.0f, -0.42f, L"feature_corridor_lit_again", false },
        { 32.0f, Vector3(0,FloorY,220), 0.0f, -0.42f, nullptr, false },
        { 32.10f, Vector3(187,FloorY,150), Pi * 0.5f, -0.36f, nullptr, false },
        { 33.5f, Vector3(187,FloorY,150), Pi * 0.5f, -0.36f, L"feature_keypad_fuse_case_sign", false },
        { 34.0f, Vector3(187,FloorY,150), Pi * 0.5f, -0.36f, nullptr, false },
    };
    const std::vector<Keyframe> FeatureStage2 = {
        { 0.0f, Vector3(-28,FloorY,-85), 0.15f, -0.40f, nullptr, false },
        { 14.0f, Vector3(-28,FloorY,-85), 0.15f, -0.40f, L"feature_unseen_1", false },
        { 24.0f, Vector3(-28,FloorY,-85), 0.15f, -0.40f, L"feature_unseen_2", false },
        { 32.0f, Vector3(-28,FloorY,-85), 0.15f, -0.55f, L"feature_unseen_3", false },
        { 34.0f, Vector3(-28,FloorY,-85), 0.15f, -0.55f, nullptr, false },
    };
    // 3面の開発用撮影。通常の1面・2面の道順にはつながない。
    const std::vector<Keyframe> Stage3Tour = {
        { 0.0f, Vector3(34.0f, FloorY, -65.0f), 0.0f, 0.0f, nullptr, false },
        { 1.0f, Vector3(34.0f, FloorY, -34.0f), -2.6f, -0.35f, L"01_階段踊り場", false },
        { 1.4f, Vector3(34.0f, FloorY, -34.0f), -2.6f, -0.35f, nullptr, false },
        { 2.0f, Vector3(34.0f, FloorY, -3.0f), Pi * 0.5f, 0.0f, nullptr, false },
        { 4.0f, Vector3(48.0f, FloorY, 1.0f), Pi * 0.5f, -0.20f, L"02_配線の廊下", false },
        { 4.3f, Vector3(48.0f, FloorY, 1.0f), Pi * 0.5f, -0.20f, nullptr, false },
        { 5.0f, Vector3(123.0f, FloorY, 1.0f), 0.0f, 0.0f, nullptr, false },
        { 7.0f, Vector3(123.0f, FloorY, 54.0f), 0.0f, -0.14f, L"03_中継室", false },
        { 8.0f, Vector3(123.0f, FloorY, 54.0f), 0.0f, -0.14f, nullptr, false },
        { 10.0f, Vector3(123.0f, FloorY, 2.0f), Pi * 0.5f, 0.0f, nullptr, false },
        { 12.8f, Vector3(126.0f, FloorY, -70.0f), Pi, -0.13f, nullptr, false },
        { 14.4f, Vector3(131.0f, FloorY, -77.0f), Pi, -0.12f, L"04_記録室", false },
        { 15.7f, Vector3(131.0f, FloorY, -77.0f), Pi, -0.12f, L"05_保守記録", false },
        { 16.8f, Vector3(150.0f, FloorY, -102.0f), Pi * 0.5f, -0.12f, nullptr, false },
        { 21.0f, Vector3(265.0f, FloorY, -102.0f), Pi * 0.5f, -0.12f, L"06_点検通路", false },
        { 23.0f, Vector3(354.0f, FloorY, -86.0f), 2.83f, -0.10f, nullptr, false },
        { 25.2f, Vector3(354.0f, FloorY, -86.0f), 2.83f, -0.10f, L"07_送信側の遮断", false },
        { 27.5f, Vector3(318.0f, FloorY, -86.0f), 3.23f, -0.10f, L"08_監視回線の接続", false },
        { 29.5f, Vector3(356.0f, FloorY, -86.0f), 2.88f, -0.10f, nullptr, false },
        { 31.4f, Vector3(338.0f, FloorY, -2.0f), 0.0f, 0.0f, nullptr, false },
        { 34.0f, Vector3(236.0f, FloorY, 2.0f), 0.0f, 0.0f, nullptr, false },
        { 36.00f, Vector3(249.0f, FloorY, 48.0f), 0.14f, -0.12f, nullptr, false },
        { 38.65f, Vector3(249.0f, FloorY, 48.0f), 0.14f, -0.12f, L"09_通信卓と監視映像", false },
        { 38.95f, Vector3(249.0f, FloorY, 48.0f), 0.14f, -0.12f, nullptr, false },
        { 39.80f, Vector3(249.0f, FloorY, 48.0f), -0.55f, -0.15f, L"10_送信の一時停止", false },
        { 42.20f, Vector3(278.0f, FloorY, 55.0f), -0.17f, -0.12f, L"11_主電源を落とすな", false },
        { 43.00f, Vector3(278.0f, FloorY, 55.0f), -0.17f, -0.12f, nullptr, false },
        { 44.00f, Vector3(237.0f, FloorY, 46.0f), Pi, 0.0f, nullptr, false },
        { 46.00f, Vector3(237.0f, FloorY, 3.0f), Pi * 0.5f, 0.0f, nullptr, false },
        { 50.00f, Vector3(415.0f, FloorY, 1.0f), 0.39f, -0.17f, L"12_非常口の手動解錠", false },
        { 51.60f, Vector3(415.0f, FloorY, 1.0f), Pi * 0.5f, 0.0f, nullptr, false },
        { 53.60f, Vector3(478.0f, FloorY, 1.0f), Pi * 0.5f, -0.04f, nullptr, false },
        { 55.0f, Vector3(485.0f, FloorY, 0.0f), Pi * 0.5f, -0.06f, L"13_林の分岐", false },
        { 59.0f, Vector3(536.0f, FloorY, 43.0f), 0.30f, -0.26f, nullptr, false },
        { 61.2f, Vector3(536.0f, FloorY, 43.0f), 0.30f, -0.26f, L"14_北の受信記録", false },
        { 63.0f, Vector3(518.0f, FloorY, 24.0f), Pi, -0.08f, nullptr, false },
        { 66.8f, Vector3(534.0f, FloorY, -38.0f), 2.80f, -0.23f, nullptr, false },
        { 68.7f, Vector3(534.0f, FloorY, -38.0f), 2.80f, -0.23f, L"15_南の中継箱", false },
        { 72.5f, Vector3(615.0f, FloorY, 0.0f), Pi * 0.5f, -0.04f, L"16_送信小屋", false },
        { 74.5f, Vector3(650.0f, FloorY, 0.0f), 0.24f, -0.22f, nullptr, false },
        { 77.0f, Vector3(650.0f, FloorY, 0.0f), 0.24f, -0.22f, L"17_調査員の原本", false },
        { 81.0f, Vector3(650.0f, FloorY, 0.0f), 0.24f, -0.22f, L"18_原本の扱いを選ぶ", false },
        { 82.5f, Vector3(650.0f, FloorY, 0.0f), 0.24f, -0.22f, nullptr, false },
        { 84.0f, Vector3(613.0f, FloorY, 0.0f), Pi, 0.0f, nullptr, false },
        { 87.0f, Vector3(616.0f, FloorY, -60.0f), Pi * 0.5f, -0.14f, nullptr, false },
        { 91.0f, Vector3(731.0f, FloorY, -60.0f), 0.0f, -0.08f, nullptr, false },
        { 94.0f, Vector3(743.0f, FloorY, 0.0f), Pi * 0.5f, 0.0f, L"19_道路の門", false },
        { 96.0f, Vector3(743.0f, FloorY, 0.0f), Pi * 0.5f, 0.0f, nullptr, false },
        { 98.0f, Vector3(804.0f, FloorY, 0.0f), 0.30f, -0.22f, nullptr, false },
        { 99.5f, Vector3(804.0f, FloorY, 0.0f), 0.30f, -0.22f, L"20_通信途絶", false },
        { 101.0f, Vector3(804.0f, FloorY, 0.0f), 0.30f, -0.22f, nullptr, false },
    };

    // 観測区画の道順：通常の3面の道順のうち、この秒数までを使い、観測区画を回ってから、この秒数以降へ戻る（戻った後は、この秒数だけ遅らせる）
    constexpr float ObservationBranchSeconds = 8.0f;
    constexpr float ObservationRejoinSeconds = 36.0f;
    constexpr float ObservationExtraSeconds = 4.0f;
    const std::vector<Keyframe> Stage3ObservationTour = []
    {
        std::vector<Keyframe> tour;
        for (const Keyframe& pose : Stage3Tour)
        {
            if (pose.Time <= ObservationBranchSeconds) tour.push_back(pose);
        }
        const Keyframe annex[] = {
            { 10.0f, Vector3(154.0f, FloorY, 77.0f), 0.0f, -0.08f, nullptr, false },
            { 12.0f, Vector3(157.0f, FloorY, 117.0f), 0.0f, -0.20f, L"04_観測区画入口", false },
            { 12.9f, Vector3(134.0f, FloorY, 132.0f), 0.0f, -0.08f, nullptr, false },
            { 13.6f, Vector3(134.0f, FloorY, 161.0f), 0.7f, -0.08f, nullptr, false },
            { 14.3f, Vector3(163.0f, FloorY, 185.0f), Pi * 0.5f, -0.06f, nullptr, false },
            { 15.4f, Vector3(198.0f, FloorY, 186.0f), 2.4f, -0.09f, L"05_観測センサー作動", false },
            { 17.2f, Vector3(161.0f, FloorY, 188.0f), Pi * 0.5f, -0.08f, nullptr, false },
            { 20.2f, Vector3(202.0f, FloorY, 187.0f), Pi, -0.06f, L"06_消灯して迂回", false },
            { 22.2f, Vector3(206.0f, FloorY, 145.0f), Pi * 0.5f, -0.08f, nullptr, false },
            { 24.6f, Vector3(250.0f, FloorY, 146.0f), 0.7f, -0.06f, L"07_遮蔽壁の先", false },
            { 27.8f, Vector3(281.0f, FloorY, 166.0f), 0.15f, -0.23f, L"08_迂回回線の端末", false },
            { 29.0f, Vector3(281.0f, FloorY, 166.0f), 0.15f, -0.23f, nullptr, false },
            { 31.2f, Vector3(310.0f, FloorY, 124.0f), Pi, -0.05f, L"09_通信室への裏口", false },
            { 32.8f, Vector3(310.0f, FloorY, 124.0f), Pi, -0.05f, nullptr, false },
            { 34.5f, Vector3(310.0f, FloorY, 102.0f), Pi, 0.0f, nullptr, false },
            { 36.0f, Vector3(310.0f, FloorY, 101.0f), Pi, 0.0f, nullptr, false },
            { 38.2f, Vector3(310.0f, FloorY, 48.0f), -Pi * 0.5f, -0.06f, nullptr, false }
        };
        tour.insert(tour.end(), std::begin(annex), std::end(annex));
        for (Keyframe pose : Stage3Tour)
        {
            if (pose.Time >= ObservationRejoinSeconds)
            {
                pose.Time += ObservationExtraSeconds;
                tour.push_back(pose);
            }
        }
        return tour;
    }();

    // 外の森（--capture と --forest を一緒に付けたとき）：開始地点 → 小道の街灯 → 振り返って後ろの街灯が消えるのを見る → 前方の草むらを何かが横切る → 研究所が見えてくる所 → 研究所の正面
    const std::vector<Keyframe> ForestTour = {
        { 0.0f, Vector3(0.0f, FloorY, -400.0f), 0.0f, 0.0f, nullptr, false },
        { 2.5f, Vector3(0.0f, FloorY, -398.0f), 0.0f, 0.0f, L"forest_01_開始地点", false },
        { 7.0f, Vector3(-6.0f, FloorY, -100.0f), YawToward(-6.0f, -100.0f, -29.0f, -60.0f), -0.12f, nullptr, false },
        { 8.0f, Vector3(-6.0f, FloorY, -98.0f), YawToward(-6.0f, -98.0f, -29.0f, -60.0f), -0.12f, L"forest_02_街灯と水たまり", false },
        // 街灯を通り過ぎて振り返る（後ろの街灯が1つずつ消える演出。ForestEvents）
        { 10.0f, Vector3(4.0f, FloorY, -15.0f), Pi, -0.05f, nullptr, false },
        { 13.0f, Vector3(4.0f, FloorY, -15.0f), Pi, -0.05f, L"forest_02b_振り返ると街灯が消えている", false },
        // 小道を少し進むと、前方の草むらを「見えない何か」が左から右へ横切る（ForestEvents と草の Compute Shader）
        { 14.5f, Vector3(0.0f, FloorY, 45.0f), 0.0f, -0.38f, nullptr, false },
        { 17.2f, Vector3(0.0f, FloorY, 45.0f), 0.0f, -0.38f, L"forest_02c_草むらを何かが通る", false },
        { 19.8f, Vector3(0.0f, FloorY, 45.0f), 0.0f, -0.38f, L"forest_02d_通った跡が草むらに残る", false },
        { 20.5f, Vector3(0.0f, FloorY, 45.0f), 0.0f, -0.38f, nullptr, false },
        { 23.0f, Vector3(0.0f, FloorY, 120.0f), 0.0f, 0.03f, nullptr, false },
        { 24.0f, Vector3(0.0f, FloorY, 125.0f), 0.0f, 0.03f, L"forest_03_研究所が見えてくる", false },
        { 27.0f, Vector3(0.0f, FloorY, 215.0f), 0.0f, 0.14f, nullptr, false },
        { 28.0f, Vector3(0.0f, FloorY, 218.0f), 0.0f, 0.14f, L"forest_04_研究所の正面", false },
        { 29.0f, Vector3(0.0f, FloorY, 218.0f), 0.0f, 0.14f, nullptr, false },
        // 草むらを足元まで見下ろす（草をカメラの上方向へ曲げる処理の確認。授業資料 p.23）
        { 30.0f, Vector3(-62.0f, FloorY, -205.0f), 0.6f, -1.10f, nullptr, false },
        { 32.0f, Vector3(-62.0f, FloorY, -205.0f), 0.6f, -1.10f, L"forest_05_草むらを見下ろす", false },
        { 33.0f, Vector3(-62.0f, FloorY, -205.0f), 0.6f, -1.10f, nullptr, false },
    };
}
