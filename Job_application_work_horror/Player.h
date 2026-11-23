// ============================================================================
// ファイルの役割: 一人称の移動、視点、懐中電灯、電池、足音、ロッカーに隠れる操作を管理している。
// 主な技術: 一人称の操作、壁との当たり判定と押し戻し、歩くときの頭の揺れ、スポットライト、キーボードとコントローラーの入力の統合
// ============================================================================

#pragma once

#include "Object.h"
#include "Texture.h"
#include "StaticMesh.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "FlashlightSystem.h"
#include "PlayerMovement.h"
#include "FirstPersonHands.h"

#include <vector>

class Wall;
class Door;

// プレイヤー。足元の位置を持ち、毎フレーム入力から移動し、目の高さにカメラを置いて懐中電灯の光を作っている。
class Player : public Object
{
private:
    // ===== 移動と当たり判定 =====
    PlayerMovement m_Movement;
    // 当たり判定の半径と、足元の最低位置。
    static constexpr float PLAYER_RADIUS = 4.0f;
    static constexpr float MIN_Y_POSITION = -99.0f;

    // 当たり判定の半径、経過時間（手持ちライトの揺れ・呼吸の揺れに使っている）、次の足音までの秒数、足音の番号（左右で音の高さを変える）
    float m_Radius = PLAYER_RADIUS;
    float m_AmbienceTimer = 0.0f;
    float m_FootstepTimer = 0.0f;
    int m_FootstepIndex = 0;
    // 直前の足音の大きさ（2面の危険度に使っている）、Sceneから知らされた水の床の上か
    float m_SurfaceNoisePulse = 0.0f;

    // ===== 懐中電灯と電池 =====
    // 電池の残りがしきい値を下回ると、完全に消える前に不安定な点滅へ移っている。
    FlashlightSystem m_Flashlight;
    // 壁や床が近いほど1に近づく値（近いときに光を弱めて白飛びを防ぐのに使っている）
    float m_FlashlightNearSurfaceBlend = 0.0f;
    // このフレームでスイッチを押したか（手の小さな押す動きに使っている）
    bool m_FlashlightToggledThisFrame = false;
    // 懐中電灯が点かない間か（1面の音で輪郭が浮かぶ真っ暗な部屋）。スイッチを押しても音だけ鳴り、点かない
    bool m_FlashlightBlocked = false;
    // 手の変わり具合と怖さ（FirstPersonHands へ渡す。シーンが毎フレーム決めている）
    float m_HandCorruption = 0.0f;
    float m_HandFear = 0.0f;
    // 体の影の、歩く手足の振りの位相と大きさ（0〜1）
    float m_BodyWalkPhase = 0.0f;
    float m_BodyWalkAmount = 0.0f;

    // ===== ライティングの調整値 =====
    // 点灯しているときの色。Diffuseは照らす光、Ambientは最低限残す環境光。
    float m_LightDiffuseR = 1.72f;
    float m_LightDiffuseG = 1.62f;
    float m_LightDiffuseB = 1.42f;

    // 消灯しているときの色。真っ暗で進めなくならないための、最低限の明るさを持っている。
    // 足元から目までの高さ
    float m_CameraHeightOffset = 40.0f;

    // ===== プレイヤーのメッシュの描画資源（三人称のデバッグ表示だけで使っている） =====
    MeshRenderer m_MeshRenderer;
    std::vector<std::unique_ptr<Material>> m_Materials;
    std::vector<SUBSET> m_subsets;
    std::vector<std::unique_ptr<Texture>> m_Textures;

    // ===== 一人称で見える、懐中電灯を持った右手（Mayaで作ったモデル） =====
    FirstPersonHands m_Hands;

    // ===== カメラと操作の状態 =====
    bool m_IsFPS = true;                    // trueなら一人称視点
    // 操作できるか、走ってよいか
    bool m_CanControl = true;
    // タイトル画面の後ろで映すときの、入力を読まない状態（SetAttractMode）
    bool m_AttractMode = false;
    // 懐中電灯の照り返し：光が当たった面から跳ね返って、まわりをぼんやり照らす弱い点光源（位置・届く範囲・色・強さ）。
    // 位置と強さは、急に跳ばないよう少しずつ近づけている（Update）。CollectPointLights でタイルベースライティングへ渡している
    DirectX::SimpleMath::Vector3 m_BouncePosition;
    DirectX::SimpleMath::Vector3 m_BounceColor;
    float m_BounceRange = 0.0f;
    float m_BounceIntensity = 0.0f;
    bool m_HasBouncePosition = false;
    bool m_SprintAllowed = true;

    // ===== 隠れ場所（ロッカー） =====
    // 隠れている間は移動できず、懐中電灯も点けられない。視点は扉の正面から左右に少しだけ動かせる。
    static constexpr float HidingLookRange = 0.65f;     // 扉の正面から左右に見回せる角度（ラジアン）
    static constexpr float HidingInputDelay = 0.35f;    // 入った直後・出た直後に、同じキーで出入りしないための待ち時間
    // 隠れているか
    bool m_IsHiding = false;
    DirectX::SimpleMath::Vector3 m_HidePosition;        // 隠れている間の足元の位置
    DirectX::SimpleMath::Vector3 m_HideExitPosition;    // 外へ出たときに立つ位置
    float m_HideFacing = 0.0f;                          // 扉の正面の向き（ヨー）
    // 次に出入りの操作を受け付けるまでの秒数
    float m_HidingInputTimer = 0.0f;

    // 隠れている間の見回せる範囲の制限と、外へ出る操作
    void UpdateHiding(Camera& camera, float deltaTime);

    // ===== 1フレームの更新の段階（Player.cpp。Update から順に呼んでいる） =====
    // 懐中電灯の光線：目の位置と向き、最初に当たる面までの距離、近い面で光を弱めるのに使う距離
    struct FlashlightBeam
    {
        DirectX::SimpleMath::Vector3 Origin;
        DirectX::SimpleMath::Vector3 Direction = DirectX::SimpleMath::Vector3::UnitZ;
        float HitDistance = 0.0f;
        float ClosestSurfaceDistance = 0.0f;
    };
    DirectX::SimpleMath::Vector3 ReadMoveInput(
        const DirectX::SimpleMath::Vector3& forward, const DirectX::SimpleMath::Vector3& right) const;
    DirectX::SimpleMath::Vector3 MoveAndCollide(const DirectX::SimpleMath::Vector3& moveDir,
        const std::vector<Wall*>& walls, const std::vector<Door*>& doors, float deltaTime);
    void UpdateFootsteps(const DirectX::SimpleMath::Vector3& positionBeforeMove, float deltaTime);
    FlashlightSystem::FrameState UpdateFlashlightPower(float deltaTime);
    FlashlightBeam TraceFlashlightBeam(const Camera& camera, float yaw,
        const std::vector<Wall*>& walls, bool visibleLight) const;
    LIGHT BuildFlashlightLight(const FlashlightSystem::FrameState& state, float closestSurfaceDistance,
        float batteryWeakness, float deltaTime);
    void UpdateFlashlightBounce(const LIGHT& light, bool renderFlashlight, const FlashlightBeam& beam, float deltaTime);
    void UpdateCameraPosition(Camera& camera,
        const DirectX::SimpleMath::Vector3& forward, const DirectX::SimpleMath::Vector3& right);
    void UpdateBodyShadow(const DirectX::SimpleMath::Vector3& forward, float deltaTime);
    void HandleDebugBatteryKey();

public:
    // ===== 階段を自動で上る演出（1面の最後の扉の先・2面の始まり） =====
    // 決めた向きへ一定の速さで歩かせ、段を踏むたびに目の高さを上げている。足元の位置（当たり判定の高さ）は床のままで、
    // カメラだけを上げている（このゲームの当たり判定は上から見た箱なので、段の箱には当たらずに通り抜ける）。
    struct StairClimb
    {
        DirectX::SimpleMath::Vector3 Direction = DirectX::SimpleMath::Vector3(0.0f, 0.0f, 1.0f); // 歩く向き（水平）
        float FlatDistance = 0.0f;      // 最初の段までの平らな距離
        float StepRun = 0.0f;           // 1段の奥行き
        float StepRise = 0.0f;          // 1段の高さ
        int StepCount = 0;             // 段の数
        float EndFlatDistance = 0.0f;   // 最後の段の後に歩く平らな距離
        float Speed = 0.0f;             // 歩く速さ（入口と出口の生成処理で指定）
        float EyeDrop = 0.0f;           // 始まりの目の高さを、普段よりこれだけ低くする（2面の始まり：下の段から上がってくる）
        float TargetPitch = 0.0f;       // 歩きながら向ける上下の向き（正で上）
        bool HoldAtEnd = false;         // 上り切った後もその姿勢で止まっているか（false なら、普段の操作に戻る）
    };
    // 出口の扉の先の階段（1面・2面の最後）：上の踊り場へ、この距離だけ歩いて止まる。上りながら少し見上げる
    static constexpr float ExitClimbLandingDepth = 30.0f;
    static constexpr float ExitClimbSpeed = 26.0f;
    static constexpr float ExitClimbTargetPitch = 0.16f;
    // 面の始まり（2面・3面）：下の段から3段上がって出てくる。少し見上げた視点から始め、普段の高さへ戻す
    // 入口の演出用の段の寸法。出口では、各面の実際の階段寸法を受け取る。
    static constexpr float ArrivalStepRun = 7.0f;
    static constexpr float ArrivalStepRise = 4.0f;
    static constexpr int ArrivalStepCount = 3;
    static constexpr float ArrivalStartPitch = 0.22f;
    static constexpr float ArrivalFlatDistance = 3.0f;
    static constexpr float ArrivalEndFlatDistance = 4.0f;
    static constexpr float ArrivalSpeed = 18.0f;

    // 出口の扉の先の階段を上る動きを作っている。stairAxis は段が並ぶ向き（ワールドの x か z の単位ベクトル）、
    // stairStart はその向きでの最初の段の位置、landing は向かう先（上の踊り場の奥）。
    // 斜めに歩くときは、段の奥行きを向きで割り、stairAxis の向きに stepRun 進むごとに1段上がるようにしている
    static StairClimb MakeExitClimb(const DirectX::SimpleMath::Vector3& position,
        const DirectX::SimpleMath::Vector3& stairAxis, float stairStart, float stepRun, float stepRise, int stepCount,
        const DirectX::SimpleMath::Vector3& landing);
    // 面の始まりに、下の段から上がってくる動きを作っている（目の高さを段の分だけ低い所から上げる）
    static StairClimb MakeArrivalClimb(const DirectX::SimpleMath::Vector3& direction);
    // 階段を自動で上り始めている（上っている間は、移動・視点・懐中電灯のスイッチの入力を読まない）
    void StartStairClimb(const StairClimb& climb);
    // 階段を自動で上っている最中か
    bool IsClimbingStairs() const { return m_Climb.Active; }

private:
    // 階段を上る演出の状態（始めた位置・歩いた距離・最後に踏んだ段・始めたときの視点の向き）
    struct ClimbState
    {
        StairClimb Settings;
        bool Active = false;
        DirectX::SimpleMath::Vector3 Start;
        float Distance = 0.0f;
        float Time = 0.0f;
        int LastStepIndex = -1;
        float StartYaw = 0.0f;
        float StartPitch = 0.0f;
    };
    ClimbState m_Climb;
    // 階段を上る演出を1フレーム進め、カメラを段の高さに置いている
    void UpdateStairClimb(float deltaTime);

public:
    void Init() override;
    void Update() override;
    void Draw(Camera* cam) override;
    void Uninit() override;
    // 懐中電灯の照り返しの点光源を1つ登録している（懐中電灯が点いていて、光が面に当たっているときだけ）
    void CollectPointLights(std::vector<ENVIRONMENT_POINT_LIGHT>& lights) const override;

    // カメラの水平方向の向き（上下の傾きを除いた単位ベクトル）を返している。
    DirectX::SimpleMath::Vector3 GetForward() const;
    // 電池を回復している
    void AddBattery(float value)
    {
        m_Flashlight.AddBattery(value);
    }
    // HUDとアイテムの判定が参照する、今の電池の残り（0〜100）を返している。
    bool IsBatteryFull() const { return m_Flashlight.IsBatteryFull(); }
    void RefillBattery() { m_Flashlight.RefillBattery(); }

    float GetBattery() const
    {
        return m_Flashlight.GetBattery();
    }

    // 電池を拾った通知をHUDに出す残り時間を返している（0なら表示しない）。
    float GetBatteryNoticeTimer() const
    {
        return m_Flashlight.GetBatteryNoticeTimer();
    }
 
    // ワープしたときに勢いが残らないよう、座標と一緒に速度も0にしている。
    void SetPosition(DirectX::SimpleMath::Vector3 pos)
    {
        m_Position = pos;
        m_Movement.ResetVelocity();
    }
    
    // 一人称視点か

    // falseの間はPlayerの更新を止め、移動と操作を受け付けない（捕まった演出や監視映像を見ている間など）。
    void SetCanControl(bool enable)
    {
        m_CanControl = enable;
    }

    bool CanControl() const
    {
        return m_CanControl;
    }

    // trueの間は、操作の入力（移動・走る・懐中電灯のスイッチ）を読まず、手も下げている。カメラと懐中電灯の光は普段どおり更新する。
    // タイトル画面の後ろで森を映すとき（TitleScene）に、位置と視点の向きをシーンから動かすために使っている
    void SetAttractMode(bool enable)
    {
        m_AttractMode = enable;
    }

    // 走っているか
    bool IsSprinting() const
    {
        return m_Movement.IsSprinting();
    }

    // falseの間は走る入力を無視し、歩きだけにしている（2面の周回中など）。
    void SetSprintAllowed(bool allowed)
    {
        m_SprintAllowed = allowed;
    }

    bool IsSprintAllowed() const
    {
        return m_SprintAllowed;
    }

    // 実際に水平方向へ動いているか
    bool IsMovingHorizontally() const
    {
        return m_Movement.IsMovingHorizontally();
    }

    // Groundの水たまり以外の、Sceneが持つ特別な濡れた床の上にいることを、次のUpdateへ伝えている。
    // 直前の足音がどれだけ大きかったか（0〜1）。水たまりや走っているときに大きくなり、2面の危険度に足されている。
    float GetSurfaceNoisePulse() const { return m_SurfaceNoisePulse; }

    // スタミナの残りを返す・満タンに戻している
    float GetStamina() const
    {
        return m_Movement.GetStamina();
    }

    void RestoreStamina()
    {
        m_Movement.RestoreStamina();
    }

    // 懐中電灯が点いているか
    bool IsFlashlightOn() const
    {
        return m_Flashlight.IsOn();
    }

    // ロッカーに入っている。hidePositionは隠れている間の足元、exitPositionは出たときに立つ位置、
    // facingは扉の正面の向き（ヨー）。懐中電灯は自動で消している。
    void EnterHiding(
        const DirectX::SimpleMath::Vector3& hidePosition,
        const DirectX::SimpleMath::Vector3& exitPosition,
        float facing);
    // 隠れているか
    bool IsHiding() const { return m_IsHiding; }
    // 懐中電灯を点けた状態・消した状態にしている（自動撮影モードで使っている。音は鳴らさない）。
    void SetFlashlightOn(bool on)
    {
        // 点かない間は、点けようとしても消えたまま
        on = on && !m_FlashlightBlocked;
        if (m_Flashlight.IsOn() != on)
        {
            m_Flashlight.Toggle();
        }
    }
    // 懐中電灯を点かなくしている（blocked の間は消え、スイッチを押しても点かない）。false に戻しても、自分で点けるまでは消えたまま
    void SetFlashlightBlocked(bool blocked)
    {
        m_FlashlightBlocked = blocked;
        if (blocked && m_Flashlight.IsOn())
        {
            m_Flashlight.Toggle();
        }
    }
    // 手の変わり具合（0〜1。血管が浮き、指先が黒ずむ）と怖さ（0〜1。手が細かく震える）を決めている
    void SetHandCorruption(float corruption) { m_HandCorruption = corruption; }
    void SetHandFear(float fear) { m_HandFear = fear; }
    // 出来事（ヒューズを拾った瞬間など）で、懐中電灯を seconds 秒だけ激しくちらつかせている
    void SputterFlashlight(float seconds)
    {
        m_Flashlight.TriggerSputter(seconds);
    }
    // 捕まったときなど、その場でロッカーから出た扱いにしている（位置は呼び出し側が決めている）。
    void ForceExitHiding()
    {
        m_IsHiding = false;
        m_HidingInputTimer = HidingInputDelay;
    }
};
