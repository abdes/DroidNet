// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;

namespace DroidNet.Controls.Tests.Thumbnail;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Thumbnail")]
[TestCategory("UITest")]
public class ThumbnailTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task ContentTemplate_IsRenderedInsteadOfTheDefault_Async() => EnqueueAsync(
    async () =>
    {
        var thumbnail = new Controls.Thumbnail { Content = "Test Content", ContentTemplate = MarkerTemplate("Custom Template Marker") };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        AssertShowsMarker(thumbnail, "Custom Template Marker");
    });

    [TestMethod]
    public Task ContentTemplateSelector_IsRenderedInsteadOfTheDefault_Async() => EnqueueAsync(
    async () =>
    {
        var thumbnail = new Controls.Thumbnail
        {
            Content = "Test Content",
            ContentTemplateSelector = new CustomTemplateSelector { DefaultTemplate = MarkerTemplate("Selector Template Marker") },
        };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        AssertShowsMarker(thumbnail, "Selector Template Marker");
    });

    [TestMethod]
    public Task WithoutTemplateOrSelector_RendersTheDefaultTemplate_Async() => EnqueueAsync(
    async () =>
    {
        var thumbnail = new Controls.Thumbnail { Content = "Test Content" };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        AssertShowsDefaultTemplate(thumbnail);
    });

    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task ClearingTheCustomTemplateOrSelector_RendersTheDefaultTemplate_Async(bool clearTemplate) => EnqueueAsync(
    async () =>
    {
        var thumbnail = new Controls.Thumbnail { Content = "Test Content" };
        if (clearTemplate)
        {
            thumbnail.ContentTemplate = MarkerTemplate("Custom Template Marker");
        }
        else
        {
            thumbnail.ContentTemplateSelector = new CustomTemplateSelector { DefaultTemplate = MarkerTemplate("Custom Template Marker") };
        }

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);
        AssertShowsMarker(thumbnail, "Custom Template Marker");

        if (clearTemplate)
        {
            thumbnail.ContentTemplate = null;
        }
        else
        {
            thumbnail.ContentTemplateSelector = null;
        }

        await WaitForRenderAsync().ConfigureAwait(true);

        AssertShowsDefaultTemplate(thumbnail);
    });

    [TestMethod]
    public Task ReEvaluatesSelector_WhenContentChanges_Async() => EnqueueAsync(
    async () =>
    {
        var thumbnail = new Controls.Thumbnail
        {
            Content = "Text Content",
            ContentTemplateSelector = new ContentTypeTemplateSelector
            {
                TextTemplate = MarkerTemplate("Text Template"),
                NumberTemplate = MarkerTemplate("Number Template"),
            },
        };
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);
        AssertShowsMarker(thumbnail, "Text Template");

        thumbnail.Content = 42;
        await WaitForRenderAsync().ConfigureAwait(true);

        AssertShowsMarker(thumbnail, "Number Template");
    });

    private static DataTemplate MarkerTemplate(string marker)
        => (DataTemplate)XamlReader.Load(
            $$"""
            <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                <TextBlock Text='{{marker}}' />
            </DataTemplate>
            """);

    private static void AssertShowsMarker(Controls.Thumbnail thumbnail, string marker)
    {
        var textBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = textBlock.Should().NotBeNull();
        _ = textBlock!.Text.Should().Be(marker);
    }

    private static void AssertShowsDefaultTemplate(Controls.Thumbnail thumbnail)
    {
        // The default template is a text-free border shared by every thumbnail without a custom template.
        _ = thumbnail.ContentTemplate.Should().BeSameAs(new Controls.Thumbnail().ContentTemplate);
        _ = FindVisualChild<TextBlock>(thumbnail).Should().BeNull("no custom template remains");
        _ = FindVisualChild<Border>(thumbnail).Should().NotBeNull("the default template draws a border");
    }

    private static T? FindVisualChild<T>(DependencyObject parent)
        where T : DependencyObject
    {
        var childCount = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChildrenCount(parent);
        for (var i = 0; i < childCount; i++)
        {
            var child = Microsoft.UI.Xaml.Media.VisualTreeHelper.GetChild(parent, i);
            if (child is T typedChild)
            {
                return typedChild;
            }

            var result = FindVisualChild<T>(child);
            if (result is not null)
            {
                return result;
            }
        }

        return null;
    }
}

[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "This is a class used only by this test suite")]
internal sealed partial class CustomTemplateSelector : DataTemplateSelector
{
    public required DataTemplate DefaultTemplate { get; set; }

    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => this.DefaultTemplate;
}

[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "This is a class used only by this test suite")]
internal sealed partial class ContentTypeTemplateSelector : DataTemplateSelector
{
    public required DataTemplate TextTemplate { get; set; }

    public required DataTemplate NumberTemplate { get; set; }

    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container)
        => item is string ? this.TextTemplate : this.NumberTemplate;
}
