// ============================================================================
// ファイルの役割: Windowsエントリーポイントからアプリケーションを起動します。
// 主な技術: main関数（Release構成はmainCRTStartup経由でWindowsサブシステム起動）、アプリケーションライフサイクル
// ============================================================================

#include    "main.h"
#include    "Application.h"

// 内蔵GPUと単体GPUを持つノートPCで、GPUドライバーに単体GPU（高性能側）を使うよう伝えます。
// Renderer::Initでも高性能なGPUを選びますが、古いドライバーではこちらの印が使われます。
extern "C"
{
	__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

//=======================================
//エントリーポイント
//=======================================
int main(void)
{
#if defined(DEBUG) || defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif//defined(DEBUG) || defined(_DEBUG)


	// アプリケーション実行
	Application app;
	app.Run();

	return 0;
}