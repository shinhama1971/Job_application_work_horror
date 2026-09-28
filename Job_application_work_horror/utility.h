// ============================================================================
// ファイルの役割: 文字列変換など複数機能から使う小さな補助関数を提供します。
// 主な技術: std::filesystem、UTF文字列、再利用可能な純粋関数
// ============================================================================

#pragma once
#include	<filesystem>
#include	<string>

namespace utility
{
    // セーブデータの保存先（%LOCALAPPDATA%\SignalLost）を返します。
    // 起動時の作業フォルダに左右されず、書き込み権限のある場所に保存するためです。
    std::filesystem::path GetSaveDirectory();

    // 旧バージョンが作業フォルダ直下の save フォルダへ保存していたファイルのパスです。
    // 新しい保存先にファイルが無い場合だけ、読み込み元として使います。
    std::filesystem::path GetLegacySavePath(std::string const& fileName);

    // 読み込み用のパス。新しい保存先にあればそれを、無ければ旧保存先を返します。
    std::filesystem::path ResolveSaveFileForRead(std::string const& fileName);

	std::string wide_to_multi_winapi(std::wstring const& src);
	std::wstring utf8_to_wide_winapi(std::string const& src);
	std::string utf8_to_multi_winapi(std::string const& src);

    // 続行できない初期化失敗をダイアログで通知し、プロセスを終了します。
    // assertはReleaseで消えるため、アセット欠落などはこちらで扱います。
    [[noreturn]] void ReportFatalError(std::string const& utf8Message);
};
