// ============================================================================
// ファイルの役割: プログラムの入口。Applicationを作って起動している。
// 主な技術: main関数（Release構成はmainCRTStartup経由でWindowsサブシステムとして起動）、アプリケーションの一生の管理
// ============================================================================

#include    "main.h"
#include    "Application.h"
#include    "Renderer.h"

#include    <filesystem>
#include    <fstream>
#include    <shellapi.h>

// 内蔵GPUと単体GPUを持つノートPCで、GPUのドライバーに単体GPU（高性能な方）を使うよう伝えている。
// Renderer::Initでも高性能なGPUを選んでいるが、古いドライバーではこちらの印が使われる。
extern "C"
{
	// NVIDIAのOptimus用と、AMDのPowerXpress用の印
	__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
	__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// 起動オプション --compile-shaders <フォルダ> のときは、ゲームを始めずにシェーダーを.csoへまとめてコンパイルしている。
// ビルドの後に呼ばれ（.vcxprojのPostBuildEvent）、初めての起動でも、タイトル画面からゲームを始めるまでの待ち時間をなくしている。
// 何もしなかったときは-1、成功は0、失敗は1を返している（失敗したらビルドも失敗にする）
int PrecompileShadersFromCommandLine()
{
	int argumentCount = 0;
	LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
	if (arguments == nullptr)
	{
		return -1;
	}
	std::filesystem::path folder;
	for (int index = 1; index + 1 < argumentCount; ++index)
	{
		if (std::wstring(arguments[index]) == L"--compile-shaders")
		{
			folder = arguments[index + 1];
			break;
		}
	}
	LocalFree(arguments);
	if (folder.empty())
	{
		return -1;
	}

	// 失敗したときのエラーは、ビルドの出力と、シェーダーのフォルダのファイルの両方に残している
	std::string errors;
	const bool succeeded = Renderer::PrecompileShaders(folder, errors);
	const std::filesystem::path errorFile = folder / "shader-compile-errors.txt";
	std::error_code error;
	if (succeeded)
	{
		std::filesystem::remove(errorFile, error);
		return 0;
	}
	std::ofstream(errorFile, std::ios::binary) << errors;
	fputs(errors.c_str(), stderr);
	return 1;
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

	// ビルド時のシェーダーの事前コンパイルなら、ウィンドウを作らずに終わっている
	const int precompileResult = PrecompileShadersFromCommandLine();
	if (precompileResult >= 0)
	{
		return precompileResult;
	}

	// アプリケーションを実行している（ウィンドウを閉じるまで戻らない）
	Application app;
	app.Run();

	return 0;
}