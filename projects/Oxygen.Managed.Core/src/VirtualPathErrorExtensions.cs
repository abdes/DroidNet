// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>Stable codes and user-facing explanations of <see cref="VirtualPathError"/>.</summary>
public static class VirtualPathErrorExtensions
{
    /// <summary>Gets the stable refusal code, the same the engine returns.</summary>
    /// <param name="error">The refusal.</param>
    /// <returns>A code such as <c>vpath.segment.reserved_character</c>.</returns>
    public static string ToCode(this VirtualPathError error) => error switch
    {
        VirtualPathError.Empty => "vpath.empty",
        VirtualPathError.NotAbsolute => "vpath.not_absolute",
        VirtualPathError.TrailingSlash => "vpath.trailing_slash",
        VirtualPathError.EmptySegment => "vpath.segment.empty",
        VirtualPathError.DotSegment => "vpath.segment.dot",
        VirtualPathError.InvalidUtf8 => "vpath.segment.invalid_utf8",
        VirtualPathError.ControlCharacter => "vpath.segment.control_character",
        VirtualPathError.ReservedCharacter => "vpath.segment.reserved_character",
        VirtualPathError.UnassignedCodePoint => "vpath.segment.unassigned_code_point",
        VirtualPathError.NotNfc => "vpath.segment.not_nfc",
        VirtualPathError.EdgeSpace => "vpath.segment.edge_space",
        VirtualPathError.TrailingDot => "vpath.segment.trailing_dot",
        VirtualPathError.DeviceName => "vpath.segment.device_name",
        VirtualPathError.SegmentTooLong => "vpath.segment.too_long",
        VirtualPathError.InvalidMountName => "vpath.mount.invalid",
        VirtualPathError.MountOnly => "vpath.mount_only",
        VirtualPathError.MissingExtension => "vpath.leaf.missing_extension",
        VirtualPathError.DerivedContent => "vpath.segment.derived_content",
        _ => throw new ArgumentOutOfRangeException(nameof(error), error, message: null),
    };

    /// <summary>Gets a sentence that tells a user why a name was refused and what to change.</summary>
    /// <param name="error">The refusal.</param>
    /// <returns>The explanation.</returns>
    public static string ToMessage(this VirtualPathError error) => error switch
    {
        VirtualPathError.Empty or VirtualPathError.EmptySegment => "Enter a name.",
        VirtualPathError.DotSegment => "'.' and '..' cannot be used as names.",
        VirtualPathError.InvalidUtf8 => "The name holds an incomplete character.",
        VirtualPathError.ControlCharacter => "Remove the control characters from the name.",
        VirtualPathError.ReservedCharacter => "Use a name without \\ / : * ? \" < > or |.",
        VirtualPathError.UnassignedCodePoint => "The name holds a character Unicode 15.1 does not define.",
        VirtualPathError.NotNfc => "The name is not in Unicode normal form C.",
        VirtualPathError.EdgeSpace => "Remove the space at the start or end of the name.",
        VirtualPathError.TrailingDot => "Remove the dot at the end of the name.",
        VirtualPathError.DeviceName => "The name is reserved by the file system.",
        VirtualPathError.SegmentTooLong => "Use a shorter name: at most 255 bytes.",
        VirtualPathError.InvalidMountName => "Start with a letter or '_' and use only letters, digits, '_' and '-'.",
        VirtualPathError.MountOnly => "The path names only a mount.",
        VirtualPathError.MissingExtension => "The name needs an extension.",
        VirtualPathError.DerivedContent => "Names starting with a dot, and the SourceMedia folder, are reserved for derived content.",
        VirtualPathError.NotAbsolute => "The path must start with '/'.",
        VirtualPathError.TrailingSlash => "The path must not end with '/'.",
        _ => throw new ArgumentOutOfRangeException(nameof(error), error, message: null),
    };
}
