// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Defines the kind of a discovered dependency.
/// </summary>
public enum ImportedDependencyKind
{
    SourceFile,
    Sidecar,
    ReferencedResource,
}
