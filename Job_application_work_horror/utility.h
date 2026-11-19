// ============================================================================
// ファイルの役割: 文字コードの変換や保存先の取得など、いろいろな所から使う小さな補助関数を提供している。
// 主な技術: std::filesystem、UTF-8・UTF-16・Shift-JISの変換、Known Folder（%LOCALAPPDATA%）の取得
// ============================================================================

#pragma once
#include	<filesystem>
#include	<functional>
#include	<iosfwd>
#include	<string>
#include	<string_view>

namespace utility
{
    // セーブデータの保存先（%LOCALAPPDATA%\SignalLost）を返している。
    // 起動したときの作業フォルダに左右されず、書き込み権限のある場所に保存するためである。
    std::filesystem::path GetSaveDirectory();

    // 読み込み用のパスを返している。新しい保存先にあればそれを、無ければ前の保存先を返している。
    std::filesystem::path ResolveSaveFileForRead(std::string const& fileName);

    enum class AtomicWriteResult
    {
        Success,
        TemporaryFileFailed,
        WriteFailed,
        DestinationChanged,
        ReplaceFailed
    };

    // 書き込み先と同じフォルダに一意の一時ファイルを作り、閉じた結果まで確認して差し替える。
    AtomicWriteResult WriteFileAtomically(const std::filesystem::path& path,
        const std::function<void(std::ofstream&)>& write, const std::function<bool()>& canReplace = {});

    // 隣の一時ファイルを閉じてから差し替える。差し替え直前に元のファイルを再確認する処理も渡せる。
    AtomicWriteResult WriteTextAtomically(const std::filesystem::path& path, std::string_view text,
        const std::function<bool()>& canReplace = {});

    // 保存フォルダを作り、完成した内容でセーブファイルを差し替える。
    bool WriteSaveFile(std::string const& fileName, std::string_view text);

	// 文字コードの変換（UTF-16→Shift-JIS、UTF-8→UTF-16、UTF-8→Shift-JIS）
	std::string wide_to_multi_winapi(std::wstring const& src);
	std::wstring utf8_to_wide_winapi(std::string const& src);
	std::string utf8_to_multi_winapi(std::string const& src);

    // 続けられない初期化の失敗をダイアログで知らせ、プロセスを終えている。
    // assertはReleaseで消えるため、素材のファイルが無いときなどはこちらで扱っている。
    [[noreturn]] void ReportFatalError(std::string const& utf8Message);
};
