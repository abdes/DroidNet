// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Models.Documents;

/// <summary>The data of <see cref="ModelContent.Relocated"/>: the relocation the model's source file followed.</summary>
/// <param name="relocation">The source file after the relocation, and the assets it moved.</param>
public sealed class ModelRelocatedEventArgs(DocumentRelocation relocation) : EventArgs
{
    /// <summary>Gets the source file after the relocation, and the assets it moved.</summary>
    public DocumentRelocation Relocation { get; } = relocation;
}
