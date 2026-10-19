// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;

namespace Oxygen.Managed.Assets.Authoring;

/// <summary>
/// The position of every value of a JSON document, by its JSON pointer (<c>/contexts/0/name</c>), so a reader can say
/// where a declaration or a problem is.
/// </summary>
internal sealed class JsonPositions
{
    private readonly Dictionary<string, SourcePosition> positions;

    private JsonPositions(Dictionary<string, SourcePosition> positions)
    {
        this.positions = positions;
    }

    /// <summary>Indexes a document the descriptor readers accepted, with comments and trailing commas.</summary>
    /// <param name="jsonUtf8">The document, without its byte order mark.</param>
    /// <returns>The positions.</returns>
    public static JsonPositions Index(ReadOnlySpan<byte> jsonUtf8)
    {
        var lines = new LineIndex(jsonUtf8);
        var positions = new Dictionary<string, SourcePosition>(StringComparer.Ordinal);
        var containers = new Stack<Container>();
        var property = string.Empty;
        var reader = new Utf8JsonReader(jsonUtf8, new JsonReaderOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = JsonCommentHandling.Skip,
        });
        while (reader.Read())
        {
            switch (reader.TokenType)
            {
                case JsonTokenType.PropertyName:
                    property = reader.GetString()!;
                    break;
                case JsonTokenType.EndObject or JsonTokenType.EndArray:
                    _ = containers.Pop();
                    break;
                default:
                    var pointer = containers.TryPeek(out var parent) ? parent.Child(property) : string.Empty;
                    positions[pointer] = lines.Find(jsonUtf8, (int)reader.TokenStartIndex);
                    if (reader.TokenType is JsonTokenType.StartObject or JsonTokenType.StartArray)
                    {
                        containers.Push(new Container(pointer, reader.TokenType == JsonTokenType.StartArray));
                    }

                    break;
            }
        }

        return new JsonPositions(positions);
    }

    /// <summary>Finds the position of a value, or of its closest enclosing value the document has.</summary>
    /// <param name="pointer">The value's JSON pointer; empty for the document.</param>
    /// <returns>The position.</returns>
    public SourcePosition? Find(string pointer)
    {
        for (var current = pointer; ; current = current[..current.LastIndexOf('/')])
        {
            if (this.positions.TryGetValue(current, out var position))
            {
                return position;
            }

            if (current.Length == 0)
            {
                return null;
            }
        }
    }

    private sealed class Container(string pointer, bool isArray)
    {
        private int next;

        public string Child(string property)
            => isArray
                ? $"{pointer}/{this.next++}"
                : $"{pointer}/{property.Replace("~", "~0", StringComparison.Ordinal).Replace("/", "~1", StringComparison.Ordinal)}";
    }

    private sealed class LineIndex
    {
        private readonly List<int> starts = [0];

        public LineIndex(ReadOnlySpan<byte> json)
        {
            for (var i = 0; i < json.Length; ++i)
            {
                if (json[i] == (byte)'\n')
                {
                    this.starts.Add(i + 1);
                }
            }
        }

        public SourcePosition Find(ReadOnlySpan<byte> json, int offset)
        {
            var line = this.starts.BinarySearch(offset);
            line = line >= 0 ? line : ~line - 1;
            var start = this.starts[line];
            return new SourcePosition(line + 1, Encoding.UTF8.GetCharCount(json[start..offset]) + 1);
        }
    }
}
