// ============================================================================
// ファイルの役割: Windowsエントリーポイントからアプリケーションを起動します。
// 主な技術: main関数（Release構成はmainCRTStartup経由でWindowsサブシステム起動）、アプリケーションライフサイクル
// ============================================================================

#include    "main.h"
#include    "Application.h"

//=======================================
//エントリーポイント
//=======================================
int main(void)
{
#if defined(DEBUG) || defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif//defined(DEBUG) || defined(_DEBUG)

	//FreeConsole();

	// アプリケーション実行
	Application app;
	app.Run();

	return 0;
}