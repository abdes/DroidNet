// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Moq;
using Oxygen.Editor.ContentBrowser.Importing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Importing;

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
    /// <param name="replacement">Whether an existing retained source requires explicit replacement.</param>
    /// <param name="rasterizationScale">The effective XAML scale.</param>
    /// <returns>The asynchronous rendered review test.</returns>
    [TestMethod]
    [DataRow(ElementTheme.Dark, false, 1d)]
    [DataRow(ElementTheme.Dark, false, 1.5d)]
    [DataRow(ElementTheme.Dark, false, 2d)]
    [DataRow(ElementTheme.Light, false, 1d)]
    [DataRow(ElementTheme.Light, false, 1.5d)]
    [DataRow(ElementTheme.Light, false, 2d)]
    [DataRow(ElementTheme.Dark, true, 1d)]
    [DataRow(ElementTheme.Dark, true, 1.5d)]
    [DataRow(ElementTheme.Dark, true, 2d)]
    [DataRow(ElementTheme.Light, true, 1d)]
    [DataRow(ElementTheme.Light, true, 1.5d)]
    [DataRow(ElementTheme.Light, true, 2d)]
    public Task ModelImportReviewShowsInlineValidation(ElementTheme theme, bool replacement, double rasterizationScale) => EnqueueAsync(async () =>
    {
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Import",
            Category = Category.Games,
            ProjectRoot = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            AuthoringMounts = [new("Content", "Content")],
            LocalFolderMounts = [],
            Scenes = [],
        };
        if (replacement)
        {
            await this.CreateRetainedSourceAsync(project).ConfigureAwait(true);
        }

        var model = new SceneImportDialogViewModel(project, Path.Combine(Path.GetTempPath(), "Crate.gltf"), "/Content/Models", Mock.Of<IDialogService>());
        var view = new SceneImportDialogView { ViewModel = model, Width = 480, RequestedTheme = theme };
        var host = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}' Padding='16' />");
        host.RequestedTheme = theme;
        host.Width = 512;
        host.VerticalAlignment = VerticalAlignment.Top;
        host.Child = view;
        var surface = new Grid { Width = 512, Height = 600, Children = { host } };
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(surface, rasterizationScale, this.TestContext.CancellationToken).ConfigureAwait(true);
        try
        {

            view.UpdateLayout();
            await this.CheckReviewAsync(view, model, host, theme, replacement).ConfigureAwait(true);
        }
        finally
        {
            if (Directory.Exists(project.ProjectRoot))
            {
                Directory.Delete(project.ProjectRoot, recursive: true);
            }
        }
    });

    private async Task CheckReviewAsync(SceneImportDialogView view, SceneImportDialogViewModel model, Border host, ElementTheme theme, bool replacement)
    {
        var name = view.FindDescendant<TextBox>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportName", StringComparison.Ordinal))!;
        var destination = view.FindDescendant<TextBox>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportDestination", StringComparison.Ordinal))!;
        _ = name.Text.Should().Be("Crate");
        _ = destination.Text.Should().Be("/Content/Models");
        _ = model.CanAccept.Should().Be(!replacement);
        _ = view.ActualHeight.Should().BeLessThan(replacement ? 500 : 360);
        _ = name.IsTabStop.Should().BeTrue();
        _ = destination.IsTabStop.Should().BeTrue();
        _ = view.FindDescendants().OfType<TextBox>()
            .Where(field => ReferenceEquals(field, name) || ReferenceEquals(field, destination))
            .OrderBy(static field => field.TabIndex).Should().Equal(name, destination);
        name.Text = "../invalid";
        await Task.Yield();
        var error = view.FindDescendant<TextBlock>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportError", StringComparison.Ordinal))!;
        _ = model.CanAccept.Should().BeFalse();
        _ = error.Text.Should().NotBeEmpty();
        name.Text = "Crate";
        await Task.Yield();
        _ = model.CanAccept.Should().Be(!replacement);
        if (replacement)
        {
            var warning = view.FindDescendant<InfoBar>(item => string.Equals(AutomationProperties.GetAutomationId(item), "ModelImportCollision", StringComparison.Ordinal))!;
            _ = warning.IsOpen.Should().BeTrue();

            await model.ReviewReplacementCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            _ = model.CanAccept.Should().BeTrue();
            _ = model.PrimaryButtonText.Should().Be("Replace and import");
            _ = warning.Message.Should().Contain("/Content/SourceMedia/DCC/Crate/model.gltf");
            _ = warning.Message.Should().Contain("/Content/Materials/Models/Crate");
        }

    }

    private async Task CreateRetainedSourceAsync(ProjectContext project)
    {
        const string relative = "Content/SourceMedia/DCC/Crate";
        var root = Path.Combine(project.ProjectRoot, relative);
        _ = Directory.CreateDirectory(root);
        var bytes = System.Text.Encoding.UTF8.GetBytes("original source");
        await File.WriteAllBytesAsync(Path.Combine(root, "model.gltf"), bytes, this.TestContext.CancellationToken).ConfigureAwait(false);
        var settings = Oxygen.Editor.ContentPipeline.Import.NativeSceneImportSettings.Create(
            new(relative, "model.gltf", [new("model.gltf", Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(bytes)))]),
            "Content",
            "Crate",
            "Models/Crate");
        await File.WriteAllBytesAsync(Path.Combine(root, "model.gltf.import.json"), settings.ToBytes(), this.TestContext.CancellationToken).ConfigureAwait(false);
    }

}
