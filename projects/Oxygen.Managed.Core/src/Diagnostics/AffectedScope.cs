// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Optional identity bundle describing the primary scope affected by an operation.
/// </summary>
public sealed record AffectedScope
{
    /// <summary>
    /// Gets the empty affected scope.
    /// </summary>
    public static AffectedScope Empty { get; } = new();

    /// <summary>
    /// Gets the affected project identity.
    /// </summary>
    public Guid? ProjectId { get; init; }

    /// <summary>
    /// Gets the affected project path.
    /// </summary>
    public string? ProjectPath { get; init; }

    /// <summary>
    /// Gets the affected project display name.
    /// </summary>
    public string? ProjectName { get; init; }

    /// <summary>
    /// Gets the affected asset identity.
    /// </summary>
    public string? AssetId { get; init; }

    /// <summary>
    /// Gets the affected asset source path.
    /// </summary>
    public string? AssetSourcePath { get; init; }

    /// <summary>
    /// Gets the affected asset virtual path.
    /// </summary>
    public string? AssetVirtualPath { get; init; }

    /// <summary>
    /// Gets the affected scene identity.
    /// </summary>
    public SceneId? SceneId { get; init; }

    /// <summary>
    /// Gets the affected scene display name.
    /// </summary>
    public string? SceneName { get; init; }

    /// <summary>
    /// Gets the affected scene node identity.
    /// </summary>
    public NodeId? NodeId { get; init; }

    /// <summary>
    /// Gets the affected scene node display name.
    /// </summary>
    public string? NodeName { get; init; }

    /// <summary>
    /// Gets the affected component type.
    /// </summary>
    public string? ComponentType { get; init; }

    /// <summary>
    /// Gets the affected component display name.
    /// </summary>
    public string? ComponentName { get; init; }

    /// <summary>
    /// Gets the path of the affected object inside its source file, such as <c>/materials/12</c> of a glTF file.
    /// </summary>
    public string? SourceObjectPath { get; init; }

    /// <summary>
    /// Gets the name the source file gives the affected object, such as a material's or a mesh's name; null when it
    /// has none.
    /// </summary>
    public string? SourceObjectName { get; init; }
}
