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
    public Task DoesNotUseDefaultTemplate_WhenContentTemplateIsSet_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        const string customTemplateXaml =
            """
              <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                            xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
                  <TextBlock Text='Custom Template Marker' x:Name='CustomMarker' />
              </DataTemplate>
            """;
        var customTemplate = (DataTemplate)XamlReader.Load(customTemplateXaml);

        var thumbnail = new Controls.Thumbnail()
        {
            Content = "Test Content",
            ContentTemplate = customTemplate,
        };

        // Act
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Assert - verify the custom template is actually rendered in the visual tree
        var textBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = textBlock.Should().NotBeNull();
        _ = textBlock!.Text.Should().Be("Custom Template Marker", "custom template should be rendered");
    });

    [TestMethod]
    public Task DoesNotUseDefaultTemplate_WhenContentTemplateSelectorIsSet_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var customTemplateSelector = new CustomTemplateSelector
        {
            DefaultTemplate = (DataTemplate)XamlReader.Load(
                """
                    <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                                  xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
                        <TextBlock Text='Selector Template Marker' x:Name='SelectorMarker' />
                    </DataTemplate>
                    """),
        };

        var thumbnail = new Controls.Thumbnail()
        {
            Content = "Test Content",
            ContentTemplateSelector = customTemplateSelector,
        };

        // Act
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Assert - verify the selector's template is actually rendered in the visual tree
        var textBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = textBlock.Should().NotBeNull();
        _ = textBlock!.Text.Should().Be("Selector Template Marker", "selector's template should be rendered");
    });

    [TestMethod]
    public Task UsesDefaultTemplate_WhenTemplateAndSelectorAreNull_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var thumbnail = new Controls.Thumbnail() { Content = "Test Content", };

        // Act
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Assert - verify default template is rendered (it should show the content)
        // The default template from Thumbnail.xaml should create visual content
        var contentPresenter = FindVisualChild<ContentPresenter>(thumbnail);
        _ = contentPresenter.Should().NotBeNull("default template should create a ContentPresenter");
        _ = thumbnail.ContentTemplate.Should().NotBeNull("default template should be applied");
    });

    [TestMethod]
    public Task UsesDefaultTemplate_WhenTemplateAndSelectorAreNull_AfterTemplateChange_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var initialTemplate = (DataTemplate)XamlReader.Load(
            """
                <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                              xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
                    <TextBlock Text='Initial Template Marker' x:Name='InitialMarker' />
                </DataTemplate>
                """);

        var thumbnail = new Controls.Thumbnail()
        {
            Content = "Test Content",
            ContentTemplate = initialTemplate,
        };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Verify initial template is rendered
        var initialTextBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = initialTextBlock.Should().NotBeNull();
        _ = initialTextBlock!.Text.Should().Be("Initial Template Marker");

        // Act - clear the template
        thumbnail.ContentTemplate = null;
        await WaitForRenderAsync().ConfigureAwait(true);

        // Assert - verify default template is now rendered (different from initial)
        _ = thumbnail.ContentTemplate.Should().NotBeNull("default template should be applied");
        var defaultContentPresenter = FindVisualChild<ContentPresenter>(thumbnail);
        _ = defaultContentPresenter.Should().NotBeNull("default template should create a ContentPresenter");
    });

    [TestMethod]
    public Task AppliesDefaultTemplate_WhenSelectorIsCleared_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var initialTemplateSelector = new CustomTemplateSelector
        {
            DefaultTemplate = (DataTemplate)XamlReader.Load(
                """
                <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                              xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
                    <TextBlock Text='Selector Template Marker' x:Name='SelectorMarker' />
                </DataTemplate>
                """),
        };

        var thumbnail = new Controls.Thumbnail()
        {
            Content = "Test Content",
            ContentTemplateSelector = initialTemplateSelector,
        };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Verify selector's template is rendered
        var selectorTextBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = selectorTextBlock.Should().NotBeNull();
        _ = selectorTextBlock!.Text.Should().Be("Selector Template Marker");

        // Act - clear the selector
        thumbnail.ContentTemplateSelector = null;
        await WaitForRenderAsync().ConfigureAwait(true);

        // Assert - verify default template is now rendered (different from selector's template)
        var defaultContentPresenter = FindVisualChild<ContentPresenter>(thumbnail);
        _ = defaultContentPresenter.Should().NotBeNull("default template should be applied after selector is cleared");
        _ = thumbnail.ContentTemplate.Should().NotBeNull();
    });

    [TestMethod]
    public Task ReEvaluatesSelector_WhenContentChanges_Async() => EnqueueAsync(
    async () =>
    {
        // Setup - Create templates with distinct visual elements we can verify
        var templateSelector = new ContentTypeTemplateSelector
        {
            TextTemplate = (DataTemplate)XamlReader.Load(
                """
                <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                              xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
                    <TextBlock Text='Text Template' x:Name='TextTemplateMarker' />
                </DataTemplate>
                """),
            NumberTemplate = (DataTemplate)XamlReader.Load(
                """
                <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
                              xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml'>
                    <TextBlock Text='Number Template' x:Name='NumberTemplateMarker' />
                </DataTemplate>
                """),
        };

        var thumbnail = new Controls.Thumbnail()
        {
            Content = "Text Content",
            ContentTemplateSelector = templateSelector,
        };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Verify initial template is applied and rendered
        var initialTextBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = initialTextBlock.Should().NotBeNull();
        _ = initialTextBlock!.Text.Should().Be("Text Template");

        // Act - change content to a different type
        thumbnail.Content = 42;

        // Wait for visual tree to update
        await WaitForRenderAsync().ConfigureAwait(true);

        // Assert - verify the visual tree actually changed to show the new template
        var updatedTextBlock = FindVisualChild<TextBlock>(thumbnail);
        _ = updatedTextBlock.Should().NotBeNull();
        _ = updatedTextBlock!.Text.Should().Be("Number Template");
    });

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
