// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts;

using Microsoft.UI.Xaml;

public interface IDockViewFactory
{
    /// <summary>Create the <see cref="UIElement">UI representation</see> for a <see cref="IDock">dock</see>.</summary>
    /// <param name="dock">The dock object for which a <see cref="UIElement" />needs to be created.</param>
    /// <returns>A <see cref="UIElement" /> for the dock, which can be placed into the docking workspace visual tree.</returns>
    UIElement CreateViewForDock(IDock dock);
}
