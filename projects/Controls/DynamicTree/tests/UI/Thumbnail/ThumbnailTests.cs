// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using FluentAssertions;
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
              <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                  <StackPanel>
                      <TextBlock Text='Custom Template' />
                  </StackPanel>
              </DataTemplate>
            """;
        var customTemplate = (DataTemplate)XamlReader.Load(customTemplateXaml);

        var thumbnail = new Controls.Thumbnail()
        {
            Content = new TextBlock { Text = "Test" },
            ContentTemplate = customTemplate,
        };

        // Act
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Assert
        _ = thumbnail.ContentTemplate.Should().Be(customTemplate);
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
                    <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                        <StackPanel>
                            <TextBlock Text='Default Template' />
                        </StackPanel>
                    </DataTemplate>
                    """),
        };

        var thumbnail = new Controls.Thumbnail()
        {
            Content = new TextBlock { Text = "Test" },
            ContentTemplateSelector = customTemplateSelector,
        };

        // Act
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Assert
        _ = thumbnail.ContentTemplate.Should().Be(customTemplateSelector.DefaultTemplate);
    });

    [TestMethod]
    public Task UsesDefaultTemplate_WhenTemplateAndSelectorAreNull_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var thumbnail = new Controls.Thumbnail() { Content = new TextBlock { Text = "Test" }, };

        // Act
        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Assert
        _ = thumbnail.ContentTemplate.Should().Be(Application.Current.Resources["DefaultThumbnailTemplate"] as DataTemplate);
    });

    [TestMethod]
    public Task UsesDefaultTemplate_WhenTemplateAndSelectorAreNull_AfterTemplateChange_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var initialTemplate = (DataTemplate)XamlReader.Load(
            """
                <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                    <StackPanel>
                        <TextBlock Text='Initial Template' />
                    </StackPanel>
                </DataTemplate>
                """);

        var thumbnail = new Controls.Thumbnail()
        {
            Content = new TextBlock { Text = "Test" },
            ContentTemplate = initialTemplate,
        };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);

        // Act
        thumbnail.ContentTemplate = null;

        // Assert
        _ = thumbnail.ContentTemplate.Should().Be(Application.Current.Resources["DefaultThumbnailTemplate"] as DataTemplate);
    });

    [TestMethod]
    public Task DoesNotChangeTemplate_AfterSelectorChange_WhenTemplateIsNotNull_Async() => EnqueueAsync(
    async () =>
    {
        // Setup
        var initialTemplateSelector = new CustomTemplateSelector
        {
            DefaultTemplate = (DataTemplate)XamlReader.Load(
                """
                <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                    <StackPanel>
                        <TextBlock Text='Initial Template' />
                    </StackPanel>
                </DataTemplate>
                """),
        };

        var thumbnail = new Controls.Thumbnail()
        {
            Content = new TextBlock { Text = "Test" },
            ContentTemplateSelector = initialTemplateSelector,
        };

        await LoadTestContentAsync(thumbnail).ConfigureAwait(true);
        _ = thumbnail.ContentTemplate.Should().NotBeNull();

        // Act
        thumbnail.ContentTemplateSelector = null;

        // Assert
        _ = thumbnail.ContentTemplate.Should().Be(initialTemplateSelector.DefaultTemplate);
    });
}

[SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "This is a class used only by this test suite")]
internal sealed partial class CustomTemplateSelector : DataTemplateSelector
{
    public required DataTemplate DefaultTemplate { get; set; }

    protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => this.DefaultTemplate;
}
