// ============================================================================
// ファイルの役割: プログラムの入口。Applicationを作って起動している。
// 主な技術: main関数（Release構成はmainCRTStartup経由でWindowsサブシステムとして起動）、アプリケーションの一生の管理
// ============================================================================

#include    "main.h"
#include    "Application.h"

// 内蔵GPUと単体GPUを持つノートPCで、GPUのドライバーに単体GPU（高性能な方）を使うよう伝えている。
// Renderer::Initでも高性能なGPUを選んでいるが、古いドライバーではこちらの印が使われる。
extern "C"
{
	// NVIDIAのOptimus用と、AMDのPowerXpress用の印
	__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

//=======================================
// エントリーポイント
//=======================================
int main(void)
{
// Debug構成では、終了時にメモリリークを出力ウィンドウに報告させている
#if defined(DEBUG) || defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif//defined(DEBUG) || defined(_DEBUG)


	// アプリケーションを実行している（ウィンドウを閉じるまで戻らない）
	Application app;
	app.Run();

	return 0;
}