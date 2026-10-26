// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using NumberBox = DroidNet.Controls.NumberBox;
using VectorBox = DroidNet.Controls.VectorBox;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>Hosts and probes shared by the layout tests: a consumer's implicit styles over the library's keyed ones.</summary>
internal static class LayoutHosts
{
    /// <summary>Creates a host that styles value boxes the way a consumer does, in the compact density.</summary>
    /// <param name="width">The host width.</param>
    /// <returns>The host.</returns>
    internal static Grid CreateHost(double width)
    {
        var host = new Grid { Width = width, HorizontalAlignment = HorizontalAlignment.Left };
        host.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("ms-appx:///Microsoft.UI.Xaml/DensityStyles/Compact.xaml"),
        });
        host.Resources.Add(
            typeof(NumberBox),
            new Style(typeof(NumberBox)) { BasedOn = (Style)Application.Current.Resources["PropertyNumberBoxStyle"] });
        host.Resources.Add(
            typeof(VectorBox),
            new Style(typeof(VectorBox)) { BasedOn = (Style)Application.Current.Resources["PropertyVectorBoxStyle"] });
        return host;
    }

    /// <summary>Creates a host holding <paramref name="content"/>.</summary>
    /// <param name="width">The host width.</param>
    /// <param name="content">The content to host.</param>
    /// <returns>The host.</returns>
    internal static Grid CreateHost(double width, UIElement content)
    {
        var host = CreateHost(width);
        host.Children.Add(content);
        return host;
    }

    /// <summary>Gets the channel inputs a vector currently shows.</summary>
    /// <param name="vector">A realized vector.</param>
    /// <returns>The visible channel inputs, in order.</returns>
    internal static NumberBox[] ShownChannels(VectorBox vector)
        => [.. vector.FindDescendants().OfType<NumberBox>().Where(input => input.FindAscendant<Grid>() is { Visibility: Visibility.Visible })];
}
