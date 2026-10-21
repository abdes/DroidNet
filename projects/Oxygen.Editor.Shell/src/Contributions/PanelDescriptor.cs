// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Contributions;

/// <summary>A panel that a feature contributes to the workspace.</summary>
/// <param name="Id">The panel's identity, unique in the editor; placement shows the panel by it.</param>
/// <param name="Title">The title the panel's tab shows.</param>
/// <param name="ViewModelType">The type of the panel's view model, which the feature registers with its view.</param>
public sealed record PanelDescriptor(string Id, string Title, Type ViewModelType)
{
    /// <summary>Gets how many instances of the panel a session has.</summary>
    public PanelMultiplicity Multiplicity { get; init; } = PanelMultiplicity.Unique;

    /// <summary>Gets the URI of the panel's icon, if any.</summary>
    public Uri? IconUri { get; init; }
}
