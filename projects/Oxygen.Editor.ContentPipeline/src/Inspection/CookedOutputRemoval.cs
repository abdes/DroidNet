// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>The cooked assets and auxiliary files to remove from one loose cooked root.</summary>
/// <param name="VirtualPaths">The virtual paths of the cooked assets whose index entries and descriptors go.</param>
/// <param name="AuxiliaryFiles">The root-relative auxiliary files whose index records and files go.</param>
public sealed record CookedOutputRemoval(ImmutableArray<string> VirtualPaths, ImmutableArray<string> AuxiliaryFiles)
{
    /// <summary>Gets a value indicating whether there is nothing to remove.</summary>
    public bool IsEmpty => this.VirtualPaths.IsEmpty && this.AuxiliaryFiles.IsEmpty;
}
