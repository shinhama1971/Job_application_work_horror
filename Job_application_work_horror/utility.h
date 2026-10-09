// ============================================================================
// ファイルの役割: 文字コードの変換や保存先の取得など、いろいろな所から使う小さな補助関数を提供している。
// 主な技術: std::filesystem、UTF-8・UTF-16・Shift-JISの変換、Known Folder（%LOCALAPPDATA%）の取得
// ============================================================================

#pragma once
#include	<filesystem>
#include	<string>

namespace utility
{
    // セーブデータの保存先（%LOCALAPPDATA%\SignalLost）を返している。
    // 起動したときの作業フォルダに左右されず、書き込み権限のある場所に保存するためである。
    std::filesystem::path GetSaveDirectory();

    // 前の版が作業フォルダの下の save フォルダへ保存していたファイルのパスを返している。
    // 新しい保存先にファイルが無い場合だけ、読み込み元として使っている。
    std::filesystem::path GetLegacySavePath(std::string const& fileName);

    // 読み込み用のパスを返している。新しい保存先にあればそれを、無ければ前の保存先を返している。
    std::filesystem::path ResolveSaveFileForRead(std::string const& fileName);

	// 文字コードの変換（UTF-16→Shift-JIS、UTF-8→UTF-16、UTF-8→Shift-JIS）
	std::string wide_to_multi_winapi(std::wstring const& src);
	std::wstring utf8_to_wide_winapi(std::string const& src);
	std::string utf8_to_multi_winapi(std::string const& src);

    // 続けられない初期化の失敗をダイアログで知らせ、プロセスを終えている。
    // assertはReleaseで消えるため、素材のファイルが無いときなどはこちらで扱っている。
    [[noreturn]] void ReportFatalError(std::string const& utf8Message);
};
