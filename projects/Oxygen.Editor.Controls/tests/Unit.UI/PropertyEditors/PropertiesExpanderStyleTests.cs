// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
/// A section has the library's one look from its default style, with no consumer style: 12 padding, 8 corner radius,
/// no minimum width, stretched across its host.
/// </summary>
[TestClass]
internal sealed class PropertiesExpanderStyleTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task TheDefaultStyleGivesTheEditorLook() => EnqueueAsync(async () =>
    {
        var section = new PropertiesExpander { Header = "Light" };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { section } }).ConfigureAwait(true);

        _ = section.Padding.Should().Be(new Thickness(12));
        _ = section.CornerRadius.Should().Be(new CornerRadius(8));
        _ = section.MinWidth.Should().Be(0);
        _ = section.HorizontalAlignment.Should().Be(HorizontalAlignment.Stretch);
        _ = section.HorizontalContentAlignment.Should().Be(HorizontalAlignment.Stretch);
    });
}
