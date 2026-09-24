// ============================================================================
// ファイルの役割: 文字列変換など複数機能から使う小さな補助関数を提供します。
// 主な技術: std::filesystem、UTF文字列、再利用可能な純粋関数
// ============================================================================

#pragma once
#include	<string>

namespace utility
{
	std::string wide_to_multi_winapi(std::wstring const& src);
	std::wstring utf8_to_wide_winapi(std::string const& src);
	std::string utf8_to_multi_winapi(std::string const& src);

    // 続行できない初期化失敗をダイアログで通知し、プロセスを終了します。
    // assertはReleaseで消えるため、アセット欠落などはこちらで扱います。
    [[noreturn]] void ReportFatalError(std::string const& utf8Message);
};
