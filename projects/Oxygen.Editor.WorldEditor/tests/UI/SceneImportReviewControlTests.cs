// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices.WindowsRuntime;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media.Imaging;
using Moq;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.Projects;
using Windows.Graphics.Imaging;

namespace Oxygen.Editor.World.Tests;

/// <summary>Renders the model import review with its real bindings and inline validation.</summary>
[TestClass]
[TestCategory("UITest")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository configuration.")]
public sealed class SceneImportReviewControlTests : VisualUserInterfaceTests
{
    /// <summary>Gets or sets cancellation and rendered-artifact reporting.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The compact review shows source/name/destination and updates errors without leaving the dialog.</summary>
    /// <param name="theme">The editor theme.</param>
    /// <returns>The asynchronous rendered review test.</returns>
    [TestMethod]
    [DataRow(ElementTheme.Dark)]
    [DataRow(ElementTheme.Light)]
    public Task ModelImportReviewShowsInlineValidation(ElementTheme theme) => EnqueueAsync(async () =>
    {
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(), Name = "Import", Category = Category.Games,
            ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
        };
        var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
        var view = new SceneImportDialogView { ViewModel = model, Width = 480, RequestedTheme = theme };
        var host = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}' Padding='16' />");
        host.RequestedTheme = theme;
        host.Width = 512;
        host.VerticalAlignment = VerticalAlignment.Top;
        host.Child = view;
        await LoadTestContentAsync(host).ConfigureAwait(true);
        var window = VisualUserInterfaceTestsApp.MainWindow.AppWindow;
        var original = window.Size;
        var scale = view.XamlRoot.RasterizationScale;
        window.Resize(new((int)(560 * scale), (int)(500 * scale)));
        try
        {
            await Task.Delay(350, this.TestContext.CancellationToken).ConfigureAwait(true);
            view.UpdateLayout();
            var name = view.FindDescendant<TextBox>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportName", StringComparison.Ordinal))!;
            var destination = view.FindDescendant<TextBox>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportDestination", StringComparison.Ordinal))!;
            _ = name.Text.Should().Be("Crate");
            _ = destination.Text.Should().Be("/Content/Models");
            _ = model.CanAccept.Should().BeTrue();
            _ = view.ActualHeight.Should().BeLessThan(360);
            model.Name = "../invalid";
            await Task.Yield();
            var error = view.FindDescendant<TextBlock>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportError", StringComparison.Ordinal))!;
            _ = model.CanAccept.Should().BeFalse();
            _ = error.Text.Should().NotBeEmpty();
            model.Name = "Crate";
            await Task.Yield();
            _ = model.CanAccept.Should().BeTrue();
            await Task.Delay(150, this.TestContext.CancellationToken).ConfigureAwait(true);
            await this.CaptureAsync(host, theme).ConfigureAwait(true);
        }
        finally
        {
            window.Resize(original);
        }
    });

    private async Task CaptureAsync(FrameworkElement view, ElementTheme theme)
    {
        var bitmap = new RenderTargetBitmap();
        await bitmap.RenderAsync(view);
        var pixels = (await bitmap.GetPixelsAsync()).ToArray();
        var folder = Directory.CreateTempSubdirectory("OxygenImportReviewRender-");
        var path = Path.Combine(folder.FullName, "import-review-" + theme + ".png");
        var file = File.Create(path);
        await using var lifetime = file.ConfigureAwait(false);
        using var stream = file.AsRandomAccessStream();
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)bitmap.PixelWidth, (uint)bitmap.PixelHeight, 96, 96, pixels);
        await encoder.FlushAsync();
        this.TestContext.AddResultFile(path);
    }
}
