// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Problems;

/// <summary>What the Problems panel's Retarget uses.</summary>
/// <param name="Relocation">Rewrites the references.</param>
/// <param name="Assets">The assets the picker offers.</param>
/// <param name="Histories">Holds the history the change is recorded in, for the ordinary Undo.</param>
/// <param name="Documents">Saves the unsaved documents first, after asking.</param>
public sealed record RetargetServices(IAssetRelocationService Relocation, IAssetFeed Assets, IHistoryService Histories, IDocumentArea Documents);
