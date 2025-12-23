// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Represents a dependency discovered during import.
/// </summary>
/// <param name="Path">Project-relative path of the dependency.</param>
/// <param name="Kind">The dependency kind.</param>
public sealed record ImportedDependency(
    string Path,
    ImportedDependencyKind Kind);
