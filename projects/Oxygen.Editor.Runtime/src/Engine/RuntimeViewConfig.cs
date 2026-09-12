// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Editor view configuration; omitted optional values retain native defaults.</summary>
public sealed record RuntimeViewConfig
{
    /// <summary>Gets the display name.</summary>
    public string Name { get; init; } = string.Empty;

    /// <summary>Gets the view purpose.</summary>
    public string Purpose { get; init; } = string.Empty;

    /// <summary>Gets the target composition surface identity, if any.</summary>
    public Guid? CompositingTarget { get; init; }

    /// <summary>Gets the initial width in pixels, when supplied.</summary>
    public uint? Width { get; init; }

    /// <summary>Gets the initial height in pixels, when supplied.</summary>
    public uint? Height { get; init; }

    /// <summary>Gets the initial clear color, when supplied.</summary>
    public RuntimeColor? ClearColor { get; init; }
}
