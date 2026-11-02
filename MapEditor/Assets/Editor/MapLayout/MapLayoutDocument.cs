// ============================================================================
// ファイルの役割: ゲームの配置ファイル（assets/layout/stage*_props.txt）を、行ごとに読み書きしている。
// ・書き出すときは、変わった数値だけを書き換え、コメント・空行・並び・空白はそのまま残している
//   （ゲーム側の PropLayoutFile と、Debug版の配置エディターの書き戻しと同じ考え方）。
// ・数値は、ゲームの PropLayoutFile::FormatNumber と同じ「小数3桁まで、末尾の0を消す」書き方にしている。
// ・座標はゲームと同じ（左手系、y が上、z が前、1単位は約5cm）。Unity も左手系で y が上なので、そのまま使える。
//   向き（yaw）も、ゲームは +Z を0度として x の側へ回る向き、Unity の y 軸まわりの回転と同じ。
// ============================================================================

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using UnityEngine;

namespace MapLayout.Editor
{
    public sealed class MapLayoutDocument
    {
        // 1行分。Content は「#」より前、Comment は「#」から後ろ。Tokens は Content を空白で区切った値と、その位置
        public sealed class Line
        {
            public string Content;
            public string Comment;
            public readonly List<Token> Tokens = new List<Token>();

            public string Command => Tokens.Count > 0 ? Tokens[0].Text : string.Empty;
            public string Text => Content + Comment;

            public static Line Parse(string text)
            {
                var line = new Line();
                var hash = text.IndexOf('#');
                line.Content = hash >= 0 ? text.Substring(0, hash) : text;
                line.Comment = hash >= 0 ? text.Substring(hash) : string.Empty;
                foreach (Match match in Regex.Matches(line.Content, @"\S+"))
                {
                    line.Tokens.Add(new Token { Start = match.Index, Text = match.Value });
                }
                return line;
            }

            public float Number(int index)
            {
                return float.Parse(Tokens[index].Text, CultureInfo.InvariantCulture);
            }

            // index 番目の値を書き換えている（数値が同じなら、元の書き方のまま残す）
            public void SetNumber(int index, float value)
            {
                if (Mathf.Abs(Number(index) - value) < 0.0005f)
                {
                    return;
                }
                SetToken(index, FormatNumber(value));
            }

            public void SetToken(int index, string text)
            {
                var token = Tokens[index];
                var delta = text.Length - token.Text.Length;
                Content = Content.Substring(0, token.Start) + text + Content.Substring(token.Start + token.Text.Length);
                token.Text = text;
                Tokens[index] = token;
                for (var later = index + 1; later < Tokens.Count; ++later)
                {
                    var moved = Tokens[later];
                    moved.Start += delta;
                    Tokens[later] = moved;
                }
            }

            // 最後の値の後ろに、値を足している（place 行に yaw・size を足すとき）
            public void Append(string text)
            {
                var end = Content.TrimEnd().Length;
                var trailing = Content.Substring(end);
                var insert = " " + text;
                Content = Content.Substring(0, end) + insert + trailing;
                foreach (Match match in Regex.Matches(insert, @"\S+"))
                {
                    Tokens.Add(new Token { Start = end + match.Index, Text = match.Value });
                }
            }

            public int IndexOf(string token, int from)
            {
                for (var index = from; index < Tokens.Count; ++index)
                {
                    if (Tokens[index].Text == token)
                    {
                        return index;
                    }
                }
                return -1;
            }
        }

        public struct Token
        {
            public int Start;
            public string Text;
        }

        public readonly List<Line> Lines = new List<Line>();
        // color 行の名前と色（光らない none は、ゲームと同じく最初から黒）
        public readonly Dictionary<string, Color> Colors = new Dictionary<string, Color> { { "none", Color.black } };

        public static MapLayoutDocument Load(string path)
        {
            var document = new MapLayoutDocument();
            var text = File.ReadAllText(path, Encoding.UTF8);
            foreach (var raw in text.Replace("\r\n", "\n").Split('\n'))
            {
                var line = Line.Parse(raw);
                document.Lines.Add(line);
                if (line.Command == "color" && line.Tokens.Count >= 5)
                {
                    document.Colors[line.Tokens[1].Text] = new Color(line.Number(2), line.Number(3), line.Number(4));
                }
            }
            return document;
        }

        // ゲームと同じ改行（CRLF）と、BOMなしの UTF-8 で書いている
        public string ToText()
        {
            var builder = new StringBuilder();
            for (var index = 0; index < Lines.Count; ++index)
            {
                builder.Append(Lines[index].Text);
                if (index + 1 < Lines.Count)
                {
                    builder.Append("\r\n");
                }
            }
            return builder.ToString();
        }

        public void Save(string path)
        {
            File.WriteAllText(path, ToText(), new UTF8Encoding(false));
        }

        public static string FormatNumber(float value)
        {
            var text = value.ToString("0.000", CultureInfo.InvariantCulture).TrimEnd('0').TrimEnd('.');
            return text == "-0" ? "0" : text;
        }
    }
}
