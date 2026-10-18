// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Text;

namespace Oxygen.Managed.Core;

/// <summary>
/// The virtual path rules of the engine's <c>Content/Docs/virtual_paths.md</c>, implemented to agree with the engine's
/// <c>VirtualPath.h</c> exactly: both run the engine's <c>VirtualPathCases.json</c> in their tests.
/// </summary>
public static partial class VirtualPath
{
    /// <summary>The longest segment, in bytes of UTF-8: the portable file name bound.</summary>
    public const int MaxSegmentBytes = 255;

    private const string ReservedCharacters = "\\/:*?\"<>|";

    private static readonly SearchValues<char> IdentifierCharacters
        = SearchValues.Create("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-");

    /// <summary>
    /// Checks one segment below a mount: the segment rule, and for an authored segment the derived content rule, as
    /// <see cref="Validate"/> checks each segment of a path.
    /// </summary>
    /// <param name="segment">The segment, such as a file or folder name a user typed.</param>
    /// <param name="rules">The rules to check it against.</param>
    /// <returns>The refusal, or <see langword="null"/> when the rules accept the segment.</returns>
    public static VirtualPathError? ValidateSegment(string segment, VirtualPathRuleSet rules)
        => ValidateSegment(segment) ?? (rules == VirtualPathRuleSet.Authored && IsDerivedContent(segment) ? VirtualPathError.DerivedContent : null);

    /// <summary>Checks one path segment against the segment rule.</summary>
    /// <param name="segment">The segment.</param>
    /// <returns>The refusal, or <see langword="null"/> when the rule accepts the segment.</returns>
    public static VirtualPathError? ValidateSegment(string segment)
    {
        ArgumentNullException.ThrowIfNull(segment);
        if (segment.Length == 0)
        {
            return VirtualPathError.EmptySegment;
        }

        if (segment is "." or "..")
        {
            return VirtualPathError.DotSegment;
        }

        if (!TryGetCodePoints(segment, out var codes))
        {
            return VirtualPathError.InvalidUtf8;
        }

        if (codes.Exists(IsControl))
        {
            return VirtualPathError.ControlCharacter;
        }

        if (codes.Exists(static code => code < 0x80 && ReservedCharacters.Contains((char)code, StringComparison.Ordinal)))
        {
            return VirtualPathError.ReservedCharacter;
        }

        if (!codes.TrueForAll(IsAssigned))
        {
            return VirtualPathError.UnassignedCodePoint;
        }

        if (!segment.IsNormalized(NormalizationForm.FormC))
        {
            return VirtualPathError.NotNfc;
        }

        if (segment[0] == ' ' || segment[^1] == ' ')
        {
            return VirtualPathError.EdgeSpace;
        }

        if (segment[^1] == '.')
        {
            return VirtualPathError.TrailingDot;
        }

        if (IsDeviceName(segment))
        {
            return VirtualPathError.DeviceName;
        }

        return Encoding.UTF8.GetByteCount(segment) > MaxSegmentBytes ? VirtualPathError.SegmentTooLong : null;
    }

    /// <summary>
    /// Checks a mount name, without its leading <c>/</c>. An authored mount, one a project names, refuses the leading
    /// dot that marks a mount the engine reserves.
    /// </summary>
    /// <param name="mount">The mount name.</param>
    /// <param name="rules">The rules to check it against.</param>
    /// <returns>The refusal, or <see langword="null"/> when the rules accept the name.</returns>
    public static VirtualPathError? ValidateMountName(string mount, VirtualPathRuleSet rules)
    {
        ArgumentNullException.ThrowIfNull(mount);
        return rules == VirtualPathRuleSet.Authored && mount.StartsWith('.') ? VirtualPathError.InvalidMountName : ValidateMountName(mount);
    }

    /// <summary>Checks a mount name, without its leading <c>/</c>.</summary>
    /// <param name="mount">The mount name.</param>
    /// <returns>The refusal, or <see langword="null"/> when the rule accepts the name.</returns>
    public static VirtualPathError? ValidateMountName(string mount)
    {
        ArgumentNullException.ThrowIfNull(mount);
        var name = mount.StartsWith('.') ? mount[1..] : mount;
        if (name.Length == 0 || !IsIdentifierStart(name[0]) || name.AsSpan(1).ContainsAnyExcept(IdentifierCharacters))
        {
            return VirtualPathError.InvalidMountName;
        }

        return mount.Length > MaxSegmentBytes ? VirtualPathError.SegmentTooLong : null;
    }

    /// <summary>Checks a virtual path.</summary>
    /// <param name="virtualPath">The path, such as <c>/Content/Materials/Red.omat</c>.</param>
    /// <param name="rules">The rules to check it against.</param>
    /// <returns>The refusal, or <see langword="null"/> when the rules accept the path.</returns>
    public static VirtualPathError? Validate(string virtualPath, VirtualPathRuleSet rules = VirtualPathRuleSet.Cooked)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        if (virtualPath.Length == 0)
        {
            return VirtualPathError.Empty;
        }

        if (virtualPath[0] != '/')
        {
            return VirtualPathError.NotAbsolute;
        }

        if (virtualPath.Length > 1 && virtualPath[^1] == '/')
        {
            return VirtualPathError.TrailingSlash;
        }

        var segments = virtualPath[1..].Split('/');
        if (Array.Exists(segments, static segment => segment.Length == 0))
        {
            return VirtualPathError.EmptySegment;
        }

        if (ValidateMountName(segments[0]) is { } mountError)
        {
            return mountError;
        }

        if (segments.Length == 1)
        {
            return VirtualPathError.MountOnly;
        }

        for (var index = 1; index < segments.Length; index++)
        {
            var segment = segments[index];
            if (ValidateSegment(segment, rules) is { } error)
            {
                return error;
            }

            if (index == segments.Length - 1 && segment.LastIndexOf('.') <= 0)
            {
                return VirtualPathError.MissingExtension;
            }
        }

        return null;
    }

    /// <summary>Gets a value indicating whether <see cref="Validate"/> accepts a path.</summary>
    /// <param name="virtualPath">The path.</param>
    /// <param name="rules">The rules to check it against.</param>
    /// <returns><see langword="true"/> when the rules accept the path.</returns>
    public static bool IsCanonical(string virtualPath, VirtualPathRuleSet rules = VirtualPathRuleSet.Cooked)
        => Validate(virtualPath, rules) is null;

    /// <summary>
    /// Gets a value indicating whether a segment below a mount names derived content: it starts with <c>.</c>
    /// (<c>.cooked</c>, <c>.imported</c>, <c>.build</c>, <c>.pipeline</c>) or is <c>SourceMedia</c>.
    /// </summary>
    /// <param name="segment">The segment.</param>
    /// <returns><see langword="true"/> for derived content, which an authored path refuses.</returns>
    public static bool IsDerivedContent(string segment)
    {
        ArgumentNullException.ThrowIfNull(segment);
        return segment.StartsWith('.') || string.Equals(segment, "SourceMedia", StringComparison.Ordinal);
    }

    /// <summary>
    /// Gets the form a name a user typed is checked and used in: trimmed, and in NFC as the rules require. Text holding
    /// an unpaired surrogate is only trimmed; <see cref="ValidateSegment(string)"/> then refuses it.
    /// </summary>
    /// <param name="typed">The name as typed.</param>
    /// <returns>The name to check and use.</returns>
    public static string NormalizeTypedName(string typed)
    {
        ArgumentNullException.ThrowIfNull(typed);
        var trimmed = typed.Trim();
        return ToNfc(trimmed) ?? trimmed;
    }

    /// <summary>Gets the NFC form of text, the form the rules require of every segment.</summary>
    /// <param name="text">The text, such as a name a user typed.</param>
    /// <returns>The NFC text, or <see langword="null"/> when the text holds an unpaired surrogate.</returns>
    public static string? ToNfc(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        return TryGetCodePoints(text, out _) ? text.Normalize(NormalizationForm.FormC) : null;
    }

    /// <summary>
    /// Gets the Unicode 15.1 simple case folding of text. Two names whose foldings are equal are case-only siblings,
    /// which a case-insensitive file system holds in one file.
    /// </summary>
    /// <param name="text">The text.</param>
    /// <returns>The folded text, or <see langword="null"/> when the text holds an unpaired surrogate.</returns>
    public static string? FoldCase(string text)
    {
        ArgumentNullException.ThrowIfNull(text);
        if (!TryGetCodePoints(text, out var codes))
        {
            return null;
        }

        var builder = new StringBuilder(text.Length);
        foreach (var code in codes)
        {
            var index = UnicodeTables.FoldingCodes.BinarySearch(code);
            _ = builder.Append(char.ConvertFromUtf32(index >= 0 ? UnicodeTables.FoldingTargets[index] : code));
        }

        return builder.ToString();
    }

    private static bool TryGetCodePoints(string text, out List<int> codes)
    {
        codes = [];
        var index = 0;
        while (index < text.Length)
        {
            if (Rune.DecodeFromUtf16(text.AsSpan(index), out var rune, out var consumed) != OperationStatus.Done)
            {
                return false;
            }

            codes.Add(rune.Value);
            index += consumed;
        }

        return true;
    }

    private static bool IsControl(int code) => code is <= 0x1F or (>= 0x7F and <= 0x9F);

    private static bool IsAssigned(int code)
    {
        var index = UnicodeTables.AssignedFirst.BinarySearch(code);
        var range = index >= 0 ? index : ~index - 1;
        return range >= 0 && code <= UnicodeTables.AssignedLast[range];
    }

    private static bool IsDeviceName(string segment)
    {
        var dot = segment.IndexOf('.', StringComparison.Ordinal);
        var stem = (dot < 0 ? segment : segment[..dot]).ToUpperInvariant();
        return stem is "CON" or "PRN" or "AUX" or "NUL"
            || (stem.Length == 4 && stem[3] is >= '1' and <= '9'
                && (stem.StartsWith("COM", StringComparison.Ordinal) || stem.StartsWith("LPT", StringComparison.Ordinal)));
    }

    private static bool IsIdentifierStart(char c) => char.IsAsciiLetter(c) || c == '_';
}
