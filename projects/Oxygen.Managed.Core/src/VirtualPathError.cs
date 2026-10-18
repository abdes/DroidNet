// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace Oxygen.Managed.Core;

/// <summary>
/// Why a virtual path, a path segment or a mount name is refused. <see cref="VirtualPathErrorExtensions.ToCode"/> gives
/// the stable refusal code the engine's implementation returns for the same input.
/// </summary>
public enum VirtualPathError
{
    /// <summary>The path is empty (<c>vpath.empty</c>).</summary>
    Empty,

    /// <summary>The path does not start with <c>/</c> (<c>vpath.not_absolute</c>).</summary>
    NotAbsolute,

    /// <summary>The path ends with <c>/</c> (<c>vpath.trailing_slash</c>).</summary>
    TrailingSlash,

    /// <summary>A segment is empty (<c>vpath.segment.empty</c>).</summary>
    EmptySegment,

    /// <summary>A segment is <c>.</c> or <c>..</c> (<c>vpath.segment.dot</c>).</summary>
    DotSegment,

    /// <summary>A segment is not valid Unicode text (<c>vpath.segment.invalid_utf8</c>).</summary>
    InvalidUtf8,

    /// <summary>A segment holds a control character (<c>vpath.segment.control_character</c>).</summary>
    ControlCharacter,

    /// <summary>A segment holds one of <c>\ / : * ? " &lt; &gt; |</c> (<c>vpath.segment.reserved_character</c>).</summary>
    [SuppressMessage("Naming", "CA1700:Do not name enum values 'Reserved'", Justification = "Windows names these the reserved characters; the member mirrors the shared refusal code and the engine's kReservedCharacter.")]
    ReservedCharacter,

    /// <summary>A segment holds a code point unassigned in Unicode 15.1 (<c>vpath.segment.unassigned_code_point</c>).</summary>
    UnassignedCodePoint,

    /// <summary>A segment is not in NFC (<c>vpath.segment.not_nfc</c>).</summary>
    NotNfc,

    /// <summary>A segment starts or ends with a space (<c>vpath.segment.edge_space</c>).</summary>
    EdgeSpace,

    /// <summary>A segment ends with a dot (<c>vpath.segment.trailing_dot</c>).</summary>
    TrailingDot,

    /// <summary>A segment is a Windows device name (<c>vpath.segment.device_name</c>).</summary>
    DeviceName,

    /// <summary>A segment is longer than 255 bytes of UTF-8 (<c>vpath.segment.too_long</c>).</summary>
    SegmentTooLong,

    /// <summary>The mount name is not an identifier (<c>vpath.mount.invalid</c>).</summary>
    InvalidMountName,

    /// <summary>The path names only a mount (<c>vpath.mount_only</c>).</summary>
    MountOnly,

    /// <summary>The leaf has no extension (<c>vpath.leaf.missing_extension</c>).</summary>
    MissingExtension,

    /// <summary>An authored path names derived content (<c>vpath.segment.derived_content</c>).</summary>
    DerivedContent,
}
