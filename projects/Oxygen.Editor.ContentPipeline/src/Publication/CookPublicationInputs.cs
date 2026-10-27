// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>The last cook's consumed inputs; mount-only publications preserve these facts unchanged.</summary>
/// <param name="InputIdentity">The digest identifying the consumed inputs.</param>
/// <param name="Inputs">The source inputs the last cook consumed.</param>
/// <param name="CookedDependencies">The cooked assets the last cook depended on.</param>
/// <param name="ProducedSourceFiles">The source files the cook itself produced.</param>
internal sealed record CookPublicationInputs(
    string InputIdentity,
    ImmutableArray<CookSnapshotInput> Inputs,
    ImmutableArray<CookedDependencySnapshot> CookedDependencies,
    ImmutableArray<CookPublicationSourceTransition> ProducedSourceFiles);
