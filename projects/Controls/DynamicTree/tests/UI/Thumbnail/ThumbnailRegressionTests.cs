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
public class ThumbnailRegressionTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task HasDefaultTemplate_BeforeBeingLoaded_Async() => EnqueueAsync(
    () =>
    {
        // Act
        var thumbnail = new Controls.Thumbnail { Content = "Test Content" };

        // Assert
        _ = thumbnail.ContentTemplate.Should().NotBeNull();
    });

    [TestMethod]
    public Task SharesDefaultTemplate_BetweenInstances_Async() => EnqueueAsync(
    () =>
    {
        // Act
        var first = new Controls.Thumbnail { Content = "First" };
        var second = new Controls.Thumbnail { Content = "Second" };

        // Assert
        _ = second.ContentTemplate.Should().BeSameAs(first.ContentTemplate);
    });

    [TestMethod]
    public Task KeepsExternalTemplate_WhenContentChanges_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        var external = CreateTemplate("External");
        var thumbnail = new Controls.Thumbnail
        {
            Content = "Text",
            ContentTemplate = external,
            ContentTemplateSelector = new FixedSelector(CreateTemplate("Selected")),
        };

        // Act
        thumbnail.Content = 42;

        // Assert
        _ = thumbnail.ContentTemplate.Should().BeSameAs(external);
    });

    [TestMethod]
    public Task ReselectsTemplate_WhenContentChangesAfterExternalTemplateIsRemoved_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        var textTemplate = CreateTemplate("Text");
        var numberTemplate = CreateTemplate("Number");
        var thumbnail = new Controls.Thumbnail
        {
            Content = "Text",
            ContentTemplate = CreateTemplate("External"),
            ContentTemplateSelector = new TypeSelector(textTemplate, numberTemplate),
        };

        // Act
        thumbnail.ContentTemplate = null;
        var afterClear = thumbnail.ContentTemplate;
        thumbnail.Content = 42;

        // Assert
        _ = afterClear.Should().BeSameAs(textTemplate);
        _ = thumbnail.ContentTemplate.Should().BeSameAs(numberTemplate);
    });

    [TestMethod]
    public Task ReselectsTemplate_WhenAssignedWithoutBeingLoaded_Async() => EnqueueAsync(
    () =>
    {
        // Setup
        var textTemplate = CreateTemplate("Text");
        var numberTemplate = CreateTemplate("Number");
        var thumbnail = new Controls.Thumbnail
        {
            Content = "Text",
            ContentTemplateSelector = new TypeSelector(textTemplate, numberTemplate),
        };

        // Act
        thumbnail.Content = 42;

        // Assert
        _ = thumbnail.ContentTemplate.Should().BeSameAs(numberTemplate);
    });

    private static DataTemplate CreateTemplate(string marker)
        => (DataTemplate)XamlReader.Load(
            $"""
            <DataTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>
                <TextBlock Text='{marker}' />
            </DataTemplate>
            """);

    private sealed partial class FixedSelector(DataTemplate template) : DataTemplateSelector
    {
        protected override DataTemplate SelectTemplateCore(object item, DependencyObject container) => template;
    }

    private sealed partial class TypeSelector(DataTemplate text, DataTemplate number) : DataTemplateSelector
    {
        protected override DataTemplate SelectTemplateCore(object item, DependencyObject container)
            => item is string ? text : number;
    }
}
