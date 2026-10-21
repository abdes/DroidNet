// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>An accepted native cooked content source.</summary>
/// <param name="Path">The physical container: a root directory or a pak file.</param>
/// <param name="Kind">How the engine reads the container.</param>
public sealed record RuntimeCookedRoot(string Path, RuntimeCookedRootKind Kind);
