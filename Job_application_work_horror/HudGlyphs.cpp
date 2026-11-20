// ============================================================================
// ファイルの役割: HUDの文字描画。UTF-8の読み取り、英数字のドット絵、日本語のGDI文字画像とそのキャッシュを扱う。
// ============================================================================

#include "Hud.h"
#include "StoryText.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

using namespace DirectX::SimpleMath;

namespace
{
    // 英数字のドット絵の大きさ（横5点・縦7点）
    constexpr int AsciiGlyphColumnCount = 5;
    constexpr std::size_t AsciiGlyphRowCount = 7;
    // 日本語の文字の大きさ：基準の18pxのフォントを pixelSize*0.34 倍し、5x7の英字と見た目の高さと送り幅をそろえる。小さくなりすぎないよう、この画素の高さより小さくしない
    constexpr float JapaneseFontReferencePixels = 18.0f;
    constexpr float JapaneseFontScaleToAscii = 0.34f;
    constexpr int MinimumJapaneseFontHeight = 6;

    // GDIの白黒画像は32ビット境界で行をそろえ、各バイトを上位ビットから読む。
    constexpr unsigned int GlyphBitmapBitsPerByte = 8;
    constexpr unsigned int GlyphBitmapRowAlignmentBytes = 4;
    constexpr unsigned int GlyphBitmapRowAlignmentBits = GlyphBitmapRowAlignmentBytes * GlyphBitmapBitsPerByte;
    constexpr unsigned int GlyphBitmapLeftmostBitMask = 0x80u;
    // キャッシュキーの下位32ビットを文字、上位をフォントの高さに使う。
    constexpr unsigned int GlyphCacheFontHeightShift = 32u;

    // UTF-8の先頭バイトは上位の印で長さを、残りのビットで文字番号の一部を表す。
    constexpr unsigned int Utf8FirstNonAsciiByte = 0x80u;
    constexpr unsigned int Utf8TwoBytePrefixMask = 0xE0u;
    constexpr unsigned int Utf8TwoBytePrefix = 0xC0u;
    constexpr unsigned int Utf8ThreeBytePrefixMask = 0xF0u;
    constexpr unsigned int Utf8ThreeBytePrefix = 0xE0u;
    constexpr unsigned int Utf8FourBytePrefixMask = 0xF8u;
    constexpr unsigned int Utf8FourBytePrefix = 0xF0u;
    constexpr int Utf8TwoByteLength = 2;
    constexpr int Utf8ThreeByteLength = 3;
    constexpr int Utf8FourByteLength = 4;
    constexpr unsigned int Utf8TwoByteLeadPayloadMask = 0x1Fu;
    constexpr unsigned int Utf8ThreeByteLeadPayloadMask = 0x0Fu;
    constexpr unsigned int Utf8FourByteLeadPayloadMask = 0x07u;
    // 続きのバイトからは印の上位2ビットを除き、下位6ビットだけをつなぐ。
    constexpr unsigned int Utf8ContinuationPayloadBits = 6u;
    constexpr unsigned int Utf8ContinuationPayloadMask = 0x3Fu;
    // 正規の長さより短い値や、UTF-16で予約されている値は文字として表示しない。
    constexpr unsigned int Utf8TwoByteMinimum = 0x80u;
    constexpr unsigned int Utf8ThreeByteMinimum = 0x800u;
    constexpr unsigned int Utf8FourByteMinimum = 0x10000u;
    constexpr unsigned int UnicodeSurrogateStart = 0xD800u;
    constexpr unsigned int UnicodeSurrogateEnd = 0xDFFFu;
    constexpr unsigned int UnicodeMaximum = 0x10FFFFu;
    constexpr char UnsupportedTextGlyph = '?';

    // GDIで作った1文字分の画像（幅・高さ・次の文字までの送り幅・ずれ・白黒の画素）
    struct RasterGlyph
    {
        unsigned int width = 0;
        unsigned int height = 0;
        int advance = 0;
        int offsetX = 0;    // 文字の位置から画像の左上までのずれ（画素）
        int offsetY = 0;
        std::vector<unsigned char> bitmap;
    };

    // 日本語の文字は、画面に出る大きさ（画素の高さ）のフォントで白黒の画像にし、1ドット＝1画素で描いている。
    // 小さいフォントの画像を縮めて描くと1ドットが1画素より細くなり、線が消えたり太ったりして字が崩れるためである。
    class JapaneseGlyphCache
    {
    public:
        // 文字の画像を作るための、画面に出さないデバイスコンテキストを作っている
        JapaneseGlyphCache()
        {
            m_DC = CreateCompatibleDC(nullptr);
        }

        // 選んでいたフォントを戻し、作ったフォントとデバイスコンテキストを解放している
        ~JapaneseGlyphCache()
        {
            if (m_DC != nullptr && m_OldFont != nullptr)
            {
                SelectObject(m_DC, m_OldFont);
            }
            for (const auto& [height, font] : m_Fonts)
            {
                DeleteObject(font);
            }
            if (m_DC != nullptr)
            {
                DeleteDC(m_DC);
            }
        }

        // fontHeightは文字の高さ（画素）。大きさと文字の組み合わせごとに、一度だけ画像を作っている。
        const RasterGlyph& Get(wchar_t character, int fontHeight)
        {
            const std::uint64_t key =
                (static_cast<std::uint64_t>(fontHeight) << GlyphCacheFontHeightShift) | static_cast<std::uint64_t>(character);
            const auto found = m_Glyphs.find(key);
            if (found != m_Glyphs.end())
            {
                return found->second;
            }

            RasterGlyph glyph;
            if (m_DC == nullptr || !SelectFont(fontHeight))
            {
                return m_Glyphs.emplace(key, std::move(glyph)).first->second;
            }

            // 拡大・回転なしの変換で、GGO_BITMAP（1画素1ビットの白黒画像）として文字の形を取り出している
            MAT2 transform{};
            transform.eM11.value = 1;
            transform.eM22.value = 1;
            GLYPHMETRICS metrics{};
            const DWORD size = GetGlyphOutlineW(
                m_DC, character, GGO_BITMAP, &metrics,
                0, nullptr, &transform);
            glyph.advance = metrics.gmCellIncX > 0
                ? metrics.gmCellIncX
                : fontHeight;
            if (size != GDI_ERROR && size > 0)
            {
                glyph.width = metrics.gmBlackBoxX;
                glyph.height = metrics.gmBlackBoxY;
                // 画像は字の形を囲む最小の四角なので、基準線からの位置で上下左右をずらしている。
                // ずらさないと「ュ」「ー」のような小さい字や横線が、上の端に寄って描かれる。
                glyph.offsetX = metrics.gmptGlyphOrigin.x;
                glyph.offsetY = m_Ascent - metrics.gmptGlyphOrigin.y - m_TopInset;
                // GDIの白黒画像は1行が4バイト単位にそろえられているので、その幅でビットを1画素ずつ取り出している
                const unsigned int pitch = ((glyph.width + GlyphBitmapRowAlignmentBits - 1u) / GlyphBitmapRowAlignmentBits) * GlyphBitmapRowAlignmentBytes;
                std::vector<unsigned char> packed(size);
                if (GetGlyphOutlineW(
                    m_DC, character, GGO_BITMAP, &metrics,
                    size, packed.data(), &transform) != GDI_ERROR)
                {
                    glyph.bitmap.resize(glyph.width * glyph.height, 0);
                    for (unsigned int row = 0; row < glyph.height; ++row)
                    {
                        for (unsigned int column = 0; column < glyph.width; ++column)
                        {
                            const unsigned char byte = packed[row * pitch + column / GlyphBitmapBitsPerByte];
                            glyph.bitmap[row * glyph.width + column] =
                                (byte & (GlyphBitmapLeftmostBitMask >> (column % GlyphBitmapBitsPerByte))) != 0 ? 1 : 0;
                        }
                    }
                }
            }
            return m_Glyphs.emplace(key, std::move(glyph)).first->second;
        }

    private:
        // デバイスコンテキスト、元のフォント、今選んでいる高さ
        HDC m_DC = nullptr;
        HGDIOBJ m_OldFont = nullptr;
        int m_SelectedHeight = 0;
        int m_Ascent = 0;       // 基準線から文字の枠の上端までの高さ
        int m_TopInset = 0;     // 文字の枠の上端から、漢字の上端までのすき間
        // 高さごとのフォントと、作った文字の画像
        std::unordered_map<int, HFONT> m_Fonts;
        std::unordered_map<std::uint64_t, RasterGlyph> m_Glyphs;

        // 指定した高さのフォントを使える状態にしている（初めての高さなら作っている）。
        bool SelectFont(int fontHeight)
        {
            if (m_SelectedHeight == fontHeight)
            {
                return true;
            }
            HFONT font = nullptr;
            const auto found = m_Fonts.find(fontHeight);
            if (found != m_Fonts.end())
            {
                font = found->second;
            }
            else
            {
                // Yu Gothic UIの太字を、アンチエイリアスなし（白黒）で作っている
                font = CreateFontW(
                    -fontHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    SHIFTJIS_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                    NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN,
                    L"Yu Gothic UI");
                if (font == nullptr)
                {
                    return false;
                }
                m_Fonts.emplace(fontHeight, font);
            }

            const HGDIOBJ previous = SelectObject(m_DC, font);
            if (m_OldFont == nullptr)
            {
                m_OldFont = previous;
            }
            m_SelectedHeight = fontHeight;

            TEXTMETRICW textMetrics{};
            GetTextMetricsW(m_DC, &textMetrics);
            m_Ascent = textMetrics.tmAscent;
            // 漢字の上端を文字の位置（y）にそろえ、以前と同じ高さに並ぶようにしている。
            MAT2 transform{};
            transform.eM11.value = 1;
            transform.eM22.value = 1;
            GLYPHMETRICS reference{};
            m_TopInset = GetGlyphOutlineW(
                m_DC, L'国', GGO_METRICS, &reference, 0, nullptr, &transform) != GDI_ERROR
                ? m_Ascent - reference.gmptGlyphOrigin.y
                : 0;
            return true;
        }
    };

    // 英数字と記号の5x7のドット絵。各行の下位5ビットが左から右の点を表している
    std::array<std::uint8_t, AsciiGlyphRowCount> GetAsciiGlyphRows(char character)
    {
        using Glyph = std::array<std::uint8_t, AsciiGlyphRowCount>;

        switch (character)
        {
        case 'A': return Glyph{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'B': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
        case 'C': return Glyph{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
        case 'D': return Glyph{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E };
        case 'E': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
        case 'F': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
        case 'G': return Glyph{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
        case 'H': return Glyph{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
        case 'I': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F };
        case 'J': return Glyph{ 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E };
        case 'K': return Glyph{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
        case 'L': return Glyph{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
        case 'M': return Glyph{ 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
        case 'N': return Glyph{ 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 };
        case 'O': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'P': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
        case 'Q': return Glyph{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
        case 'R': return Glyph{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
        case 'S': return Glyph{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
        case 'T': return Glyph{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
        case 'U': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
        case 'V': return Glyph{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
        case 'W': return Glyph{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
        case 'X': return Glyph{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
        case 'Y': return Glyph{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
        case 'Z': return Glyph{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
        case '0': return Glyph{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
        case '1': return Glyph{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
        case '2': return Glyph{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F };
        case '3': return Glyph{ 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
        case '4': return Glyph{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
        case '5': return Glyph{ 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E };
        case '6': return Glyph{ 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E };
        case '7': return Glyph{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
        case '8': return Glyph{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
        case '9': return Glyph{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
        case '-': return Glyph{ 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
        case '\'': return Glyph{ 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 };
        // 記号。HUDの「記録 0 / 4」「C / Y」「100%」「受信中...」「記録:」などで使っている（無いと空白になる）。
        case '/': return Glyph{ 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 };
        case '%': return Glyph{ 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
        case '.': return Glyph{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C };
        case ',': return Glyph{ 0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08 };
        case ':': return Glyph{ 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 };
        case '?': return Glyph{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 };
        case '!': return Glyph{ 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 };
        case '(': return Glyph{ 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
        case ')': return Glyph{ 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
        case '+': return Glyph{ 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
        default: return Glyph{};
        }
    }

    // 文字の画像は一度作ったら使い回す（初めて使うときに作り、プログラムの終わりに片付ける）
    JapaneseGlyphCache& GetJapaneseGlyphs()
    {
        static JapaneseGlyphCache japaneseGlyphs;
        return japaneseGlyphs;
    }
}

// 文字列を1文字ずつ、四角形の集まりとしてためている（日本語はフォントの画像、英数字は5x7のドット絵）
void Hud::AddText(
    float x,
    float y,
    std::string_view text,
    float pixelSize,
    const Color& color)
{
    float cursorX = x;
    for (size_t textIndex = 0; textIndex < text.size();)
    {
        const unsigned char firstByte =
            static_cast<unsigned char>(text[textIndex]);
        // 先頭のバイトが0x80以上なら、UTF-8の2〜4バイトの文字としてコードポイントを取り出している
        if (firstByte >= Utf8FirstNonAsciiByte)
        {
            int sequenceLength = 0;
            if ((firstByte & Utf8TwoBytePrefixMask) == Utf8TwoBytePrefix) sequenceLength = Utf8TwoByteLength;
            else if ((firstByte & Utf8ThreeBytePrefixMask) == Utf8ThreeBytePrefix) sequenceLength = Utf8ThreeByteLength;
            else if ((firstByte & Utf8FourBytePrefixMask) == Utf8FourBytePrefix) sequenceLength = Utf8FourByteLength;

            unsigned int codePoint = firstByte &
                (sequenceLength == Utf8TwoByteLength ? Utf8TwoByteLeadPayloadMask :
                 sequenceLength == Utf8ThreeByteLength ? Utf8ThreeByteLeadPayloadMask : Utf8FourByteLeadPayloadMask);
            bool valid = sequenceLength > 0 && static_cast<size_t>(sequenceLength) <= text.size() - textIndex;
            for (int byteIndex = 1; valid && byteIndex < sequenceLength; ++byteIndex)
            {
                const auto byte = static_cast<unsigned char>(text[textIndex + byteIndex]);
                valid = StoryText::IsUtf8ContinuationByte(byte);
                codePoint = (codePoint << Utf8ContinuationPayloadBits) | (byte & Utf8ContinuationPayloadMask);
            }
            const unsigned int minimum = sequenceLength == Utf8TwoByteLength ? Utf8TwoByteMinimum :
                sequenceLength == Utf8ThreeByteLength ? Utf8ThreeByteMinimum : Utf8FourByteMinimum;
            valid = valid && codePoint >= minimum && codePoint <= UnicodeMaximum &&
                (codePoint < UnicodeSurrogateStart || codePoint > UnicodeSurrogateEnd);
            // 壊れた並びは1バイトだけ進める。4バイト文字は1文字分を読み、GDIの1文字描画では代替表示にする。
            textIndex += valid ? static_cast<size_t>(sequenceLength) : 1;
            if (valid && sequenceLength != Utf8FourByteLength)
            {
                cursorX += AddJapaneseCharacter(cursorX, y, static_cast<wchar_t>(codePoint), pixelSize, color);
            }
            else
            {
                AddAsciiCharacter(cursorX, y, UnsupportedTextGlyph, pixelSize, color);
                cursorX += pixelSize * GlyphAdvance;
            }
            continue;
        }

        AddAsciiCharacter(cursorX, y, static_cast<char>(firstByte), pixelSize, color);
        ++textIndex;
        cursorX += pixelSize * GlyphAdvance;
    }
}

// 日本語の1文字を、画面に出る大きさのフォントの白黒の画像から、横に続く点をまとめた四角形で描いている。進む幅を返している
float Hud::AddJapaneseCharacter(float x, float y, wchar_t character, float pixelSize, const Color& color)
{
    // 5x7の英字フォントの見た目の高さと送り幅に合わせた大きさにしている（日本語とASCIIが混ざっても、今あるHUDのパネルの中へ収めるため）。
    // その大きさを実際の画素に直したフォントで画像を作り、1ドットを1画素（キャンバス単位では1/scale）で描いている。
    const float canvasScale = GetCanvasScale();
    const int fontHeight = (std::max)(MinimumJapaneseFontHeight,
        static_cast<int>(std::lround(JapaneseFontReferencePixels * pixelSize * JapaneseFontScaleToAscii * canvasScale)));
    const RasterGlyph& glyph = GetJapaneseGlyphs().Get(character, fontHeight);
    const float glyphPixelSize = 1.0f / canvasScale;
    // 文字の位置を画素の境目にそろえ、ドットが2画素にまたがってにじまないようにしている。
    const float glyphX = std::round(x * canvasScale) / canvasScale +
        static_cast<float>(glyph.offsetX) * glyphPixelSize;
    const float glyphY = std::round(y * canvasScale) / canvasScale +
        static_cast<float>(glyph.offsetY) * glyphPixelSize;
    // 横に続く点はまとめて1つの横長の四角形にし、頂点の数を減らしている
    for (unsigned int row = 0; row < glyph.height; ++row)
    {
        unsigned int column = 0;
        while (column < glyph.width)
        {
            while (column < glyph.width &&
                glyph.bitmap[row * glyph.width + column] == 0)
            {
                ++column;
            }
            const unsigned int runStart = column;
            while (column < glyph.width &&
                glyph.bitmap[row * glyph.width + column] != 0)
            {
                ++column;
            }
            if (column > runStart)
            {
                AddRectangle(
                    glyphX + static_cast<float>(runStart) * glyphPixelSize,
                    glyphY + static_cast<float>(row) * glyphPixelSize,
                    static_cast<float>(column - runStart) * glyphPixelSize,
                    glyphPixelSize,
                    color);
            }
        }
    }
    return static_cast<float>(glyph.advance) * glyphPixelSize;
}

// 英数字の1文字：小文字は大文字として扱い、5x7のドット絵の点を四角形で描いている
void Hud::AddAsciiCharacter(float x, float y, char character, float pixelSize, const Color& color)
{
    if (character >= 'a' && character <= 'z')
    {
        character = static_cast<char>(character - 'a' + 'A');
    }
    const std::array<std::uint8_t, AsciiGlyphRowCount> rows = GetAsciiGlyphRows(character);
    for (size_t row = 0; row < rows.size(); ++row)
    {
        for (int column = 0; column < AsciiGlyphColumnCount; ++column)
        {
            if ((rows[row] & (1u << (AsciiGlyphColumnCount - 1 - column))) != 0)
            {
                AddRectangle(
                    x + static_cast<float>(column) * pixelSize,
                    y + static_cast<float>(row) * pixelSize,
                    pixelSize,
                    pixelSize,
                    color);
            }
        }
    }
}
