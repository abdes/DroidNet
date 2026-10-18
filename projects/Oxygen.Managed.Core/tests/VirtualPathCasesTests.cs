// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;

namespace Oxygen.Managed.Core.Tests;

/// <summary>
/// Runs the engine's <c>VirtualPathCases.json</c>, the case table of the virtual path rules, so this implementation
/// accepts and refuses exactly what the engine does, with the same codes.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(VirtualPath))]
public sealed class VirtualPathCasesTests
{
    private static readonly string[] RefusalSections = ["segments", "mounts", "paths"];

    private static readonly Lazy<JsonObject> Cases = new(static () =>
        JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "VirtualPathCases.json")))!.AsObject());

    [TestMethod]
    public void Segments_FollowTheCaseTable()
    {
        foreach (var entry in Entries("segments"))
        {
            // A .NET string cannot hold bytes that are not UTF-8: such a segment is refused before it is a string.
            if (entry["segment_hex"] is { } hex)
            {
                _ = TryDecodeUtf8(Convert.FromHexString(hex.GetValue<string>())).Should().BeNull(entry.ToJsonString());
                _ = Refusal(entry).Should().Be(VirtualPathError.InvalidUtf8.ToCode(), entry.ToJsonString());
                continue;
            }

            var actual = VirtualPath.ValidateSegment(entry["segment"]!.GetValue<string>());

            _ = Code(actual).Should().Be(Refusal(entry), entry.ToJsonString());
        }
    }

    [TestMethod]
    public void Mounts_FollowTheCaseTable()
    {
        foreach (var entry in Entries("mounts"))
        {
            var actual = VirtualPath.ValidateMountName(entry["mount"]!.GetValue<string>());

            _ = Code(actual).Should().Be(Refusal(entry), entry.ToJsonString());
        }
    }

    [TestMethod]
    public void Paths_FollowTheCaseTable()
    {
        foreach (var entry in Entries("paths"))
        {
            var path = entry["path"]!.GetValue<string>();
            var rules = string.Equals(entry["rules"]!.GetValue<string>(), "authored", StringComparison.Ordinal) ? VirtualPathRuleSet.Authored : VirtualPathRuleSet.Cooked;

            var actual = VirtualPath.Validate(path, rules);

            _ = Code(actual).Should().Be(Refusal(entry), entry.ToJsonString());
            _ = VirtualPath.IsCanonical(path, rules).Should().Be(Refusal(entry) is null, entry.ToJsonString());
        }
    }

    [TestMethod]
    public void Nfc_FollowsTheCaseTable()
    {
        foreach (var entry in Entries("nfc"))
        {
            _ = VirtualPath.ToNfc(entry["text"]!.GetValue<string>()).Should().Be(entry["nfc"]!.GetValue<string>(), entry.ToJsonString());
        }
    }

    [TestMethod]
    public void Folding_FollowsTheCaseTable()
    {
        foreach (var entry in Entries("fold"))
        {
            _ = VirtualPath.FoldCase(entry["text"]!.GetValue<string>()).Should().Be(entry["folded"]!.GetValue<string>(), entry.ToJsonString());
        }
    }

    [TestMethod]
    public void Table_CoversEveryRefusalCode()
    {
        var used = RefusalSections
            .SelectMany(Entries)
            .Select(Refusal)
            .OfType<string>()
            .ToHashSet(StringComparer.Ordinal);

        _ = used.Should().BeEquivalentTo(Enum.GetValues<VirtualPathError>().Select(static error => error.ToCode()));
        _ = Cases.Value["unicode_version"]!.GetValue<string>().Should().Be("15.1.0");
    }

    private static IEnumerable<JsonObject> Entries(string section)
        => Cases.Value[section]!.AsArray().Select(static node => node!.AsObject());

    private static string? Refusal(JsonObject entry) => entry["refusal"]?.GetValue<string>();

    private static string? Code(VirtualPathError? error) => error?.ToCode();

    private static string? TryDecodeUtf8(byte[] bytes)
    {
        try
        {
            return new UTF8Encoding(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true).GetString(bytes);
        }
        catch (DecoderFallbackException)
        {
            return null;
        }
    }
}
