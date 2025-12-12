// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// A <see cref="ResourceDictionary" /> for the styles used in the <see cref="DynamicTree" /> demo. Because the
/// styles use <c>{x:Bind}</c>, they must be backed by a <see cref="ResourceDictionary" /> implemented in code behind.
/// </summary>
internal sealed partial class Styles
{
    /// <summary>
    /// Initializes a new instance of the <see cref="Styles"/> class.
    /// </summary>
    public Styles()
    {
        this.InitializeComponent();
    }
}
