// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Immutable native content facts for one engine lifetime and content revision.</summary>
/// <param name="RunId">The native runtime lifetime.</param>
/// <param name="Revision">The monotonically increasing status revision.</param>
/// <param name="State">The acknowledged content state.</param>
/// <param name="Bindings">The complete last acknowledged roots and their optional project mount identities.</param>
/// <param name="Reason">An optional explanation of native unavailability.</param>
public sealed record RuntimeContentSnapshot(Guid RunId, long Revision, RuntimeContentState State, ImmutableArray<RuntimeCookedRoot> Bindings, string? Reason = null)
{
    /// <summary>Gets immutable accepted bindings; changing them creates a new snapshot.</summary>
    public ImmutableArray<RuntimeCookedRoot> Bindings { get; } = Bindings;

    /// <summary>Gets physical paths in native precedence order.</summary>
    public ImmutableArray<string> Roots { get; } = [.. Bindings.Select(static root => root.Path)];
}

/// <summary>How the engine reads a cooked content source.</summary>
public enum RuntimeCookedRootKind
{
    /// <summary>A loose cooked root directory.</summary>
    LooseFolder,

    /// <summary>A pak file.</summary>
    PakFile,
}

/// <summary>An accepted native cooked content source.</summary>
/// <param name="Path">The physical container: a root directory or a pak file.</param>
/// <param name="Kind">How the engine reads the container.</param>
public sealed record RuntimeCookedRoot(string Path, RuntimeCookedRootKind Kind);
