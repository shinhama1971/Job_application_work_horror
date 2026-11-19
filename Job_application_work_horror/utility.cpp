// ============================================================================
// ファイルの役割: 文字コードの変換や保存先の取得など、いろいろな所から使う小さな補助関数を提供している。
// 主な技術: std::filesystem、UTF-8・UTF-16・Shift-JISの変換、Known Folder（%LOCALAPPDATA%）の取得
// ============================================================================

#include	"utility.h"

#include	<cstdlib>
#include	<filesystem>
#include	<fstream>
#include	<string>
#include	<vector>
#include	<Windows.h>
#include	<ShlObj.h>

namespace utility {
	// ワイド文字（UTF-16）を、Windowsの既定の文字コード（日本語環境ではShift-JIS）にしている
	std::string wide_to_multi_winapi(std::wstring const& src)
	{
		// 1回目で必要な長さを求め、2回目で実際に変換している
		auto const dest_size = ::WideCharToMultiByte(
			CP_ACP,
			0U,
			src.data(),
			-1,
			nullptr,
			0,
			nullptr,
			nullptr);
		std::vector<char> dest(dest_size, '\0');
		if (::WideCharToMultiByte(
			CP_ACP,
			0U,
			src.data(),
			-1,
			dest.data(),
			static_cast<int>(dest.size()),
			nullptr,
			nullptr) == 0) {
			throw std::system_error{ static_cast<int>(::GetLastError()), std::system_category() };
		}
		dest.resize(std::char_traits<char>::length(dest.data()));
		dest.shrink_to_fit();
		return std::string(dest.begin(), dest.end());
	}

	// UTF-8をワイド文字（UTF-16）にしている
	std::wstring utf8_to_wide_winapi(std::string const& src)
	{
		auto const dest_size = ::MultiByteToWideChar(
			CP_UTF8,			 // 元の文字列はUTF-8
			0U,
			src.data(),
			-1,
			nullptr,
			0U);
		std::vector<wchar_t> dest(dest_size, L'\0');
		if (::MultiByteToWideChar(CP_UTF8, 0U, src.data(), -1, dest.data(), static_cast<int>(dest.size())) == 0) {
			throw std::system_error{ static_cast<int>(::GetLastError()), std::system_category() };
		}
		dest.resize(std::char_traits<wchar_t>::length(dest.data()));
		dest.shrink_to_fit();
		return std::wstring(dest.begin(), dest.end());
	}

	// UTF-8を、Windowsの既定の文字コード（Shift-JIS）にしている
	std::string utf8_to_multi_winapi(std::string const& src)
	{
		auto const wide = utf8_to_wide_winapi(src);
		return wide_to_multi_winapi(wide);
	}

    // %LOCALAPPDATA%\SignalLost を返している
    std::filesystem::path GetSaveDirectory()
    {
        std::filesystem::path directory;
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(
            FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)))
        {
            directory = std::filesystem::path(localAppData) / L"SignalLost";
        }
        ::CoTaskMemFree(localAppData);

        // 取得できない環境では、前と同じく作業フォルダの下の save を使っている。
        if (directory.empty())
        {
            directory = L"save";
        }
        return directory;
    }

    // 新しい保存先にファイルがあればそれを、無ければ前の保存先のパスを返している
    std::filesystem::path ResolveSaveFileForRead(std::string const& fileName)
    {
        const std::filesystem::path current = GetSaveDirectory() / fileName;
        std::error_code existsError;
        if (std::filesystem::exists(current, existsError))
        {
            return current;
        }
        return std::filesystem::path(L"save") / fileName;
    }

    AtomicWriteResult WriteFileAtomically(const std::filesystem::path& path,
        const std::function<void(std::ofstream&)>& write, const std::function<bool()>& canReplace)
    {
        const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path(L".");
        wchar_t temporaryName[MAX_PATH]{};
        if (::GetTempFileNameW(directory.c_str(), L"slt", 0, temporaryName) == 0)
        {
            return AtomicWriteResult::TemporaryFileFailed;
        }
        struct TemporaryFile
        {
            const wchar_t* name;
            ~TemporaryFile()
            {
                if (name != nullptr)
                {
                    ::DeleteFileW(name);
                }
            }
        } temporary{temporaryName};

        std::ofstream file(std::filesystem::path(temporary.name), std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return AtomicWriteResult::TemporaryFileFailed;
        }
        write(file);
        file.close();
        if (!file)
        {
            return AtomicWriteResult::WriteFailed;
        }
        if (canReplace && !canReplace())
        {
            return AtomicWriteResult::DestinationChanged;
        }
        if (!::MoveFileExW(temporary.name, path.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            return AtomicWriteResult::ReplaceFailed;
        }
        temporary.name = nullptr;
        return AtomicWriteResult::Success;
    }

    AtomicWriteResult WriteTextAtomically(const std::filesystem::path& path, std::string_view text,
        const std::function<bool()>& canReplace)
    {
        return WriteFileAtomically(path, [text](std::ofstream& file)
        {
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
        }, canReplace);
    }

    bool WriteSaveFile(std::string const& fileName, std::string_view text)
    {
        const auto directory = GetSaveDirectory();
        const auto path = directory / fileName;
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        if (error || WriteTextAtomically(path, text) != AtomicWriteResult::Success)
        {
            ::OutputDebugStringW((L"セーブファイルを書き込めない: " + path.wstring() + L"\n").c_str());
            return false;
        }
        return true;
    }

    // 出力ウィンドウとダイアログにエラーを出し、すぐにプロセスを終えている
    void ReportFatalError(std::string const& utf8Message)
    {
        ::OutputDebugStringA((utf8Message + "\n").c_str());

        std::wstring message;
        try
        {
            message = utf8_to_wide_winapi(utf8Message);
        }
        catch (...)
        {
            message = L"Fatal error";
        }
        ::MessageBoxW(
            ::GetActiveWindow(), message.c_str(), L"起動エラー", MB_OK | MB_ICONERROR);

        // 読み込みの途中のSceneやObjectは不完全な状態のため、デストラクタを呼ばずに終了している。
        ::ExitProcess(EXIT_FAILURE);
    }
}

