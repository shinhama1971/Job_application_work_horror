// ============================================================================
// ファイルの役割: どこからも呼ばれていなかった関数を、消さずに残している（11月19日に本体から移した）。
// プロジェクトには登録していない（ビルドされない）。また使うときは、元のファイルへ戻す。
// ============================================================================

// ---- utility.cpp（namespace utility）から ----
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

	// UTF-8を、Windowsの既定の文字コード（Shift-JIS）にしている
	std::string utf8_to_multi_winapi(std::string const& src)
	{
		auto const wide = utf8_to_wide_winapi(src);
		return wide_to_multi_winapi(wide);
	}

// ---- PropModel.cpp（namespace PropModel）から。宣言は PropModel.h にあった ----
    IndexRange Merge(const IndexRange& first, const IndexRange& last)
    {
        IndexRange merged;
        merged.Start = first.Start;
        merged.Count = last.Start + last.Count - first.Start;
        return merged;
    }

// ---- StaticMesh.h（class StaticMesh）から ----
	const std::vector<std::string>& GetTextureNames() {
		return m_texturenames;
	}

// ---- FlashlightWriting.h（class FlashlightWriting）から ----
    // 文字が向いている方向（壁から外向き）
    const DirectX::SimpleMath::Vector3& GetNormal() const { return m_Normal; }
