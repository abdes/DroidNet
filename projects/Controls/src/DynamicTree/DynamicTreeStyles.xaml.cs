// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using Microsoft.UI.Xaml;

/// <summary>
/// A <see cref="ResourceDictionary" /> for the styles used in the <see cref="DynamicTree" /> control implementation. Because the
/// styles use <c>{x:Bind}</c>, they must be backed by a <see cref="ResourceDictionary" /> implemented in code behind.
/// </summary>
public partial class DynamicTreeStyles
{
    public DynamicTreeStyles() => this.InitializeComponent();
}
