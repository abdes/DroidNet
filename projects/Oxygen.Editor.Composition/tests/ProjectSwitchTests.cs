// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.Composition.UI.Tests.TestSupport;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Interaction;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>
///     Switching projects closes the workspace window and opens another one. The preferences stored per project live with
///     the window until the project session owns them, so the next project gets its own; the gizmo preferences, kept
///     across projects, are the editor's (spec 6.2, Phase 2).
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Project switch")]
public sealed class ProjectSwitchTests
{
    private static readonly Type[] ProjectPreferences = [typeof(PreviewSettingsService), typeof(SceneNodeInteraction), typeof(ViewportStateService)];

    [TestMethod]
    public async Task SwitchProject_ProjectPreferences_AreTheNextWindowsOwn()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var first = scopes.CreateWindow();
        var before = ProjectPreferences.Select(type => first.Resolver.Resolve(type)).ToList();
        var settingsBefore = first.Resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService));

        // Act
        await first.DisposeAsync().ConfigureAwait(false);
        var next = scopes.CreateWindow();

        // Assert
        _ = ProjectPreferences.Select(type => next.Resolver.Resolve(type)).Should().NotIntersectWith(before);
        _ = next.Resolver.Resolve<IEditorSettingsManager>(serviceKey: nameof(PreviewSettingsService)).Should().NotBeSameAs(settingsBefore);
    }

    [TestMethod]
    public async Task SwitchProject_GizmoPreferences_AreTheEditors()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var first = scopes.CreateWindow();
        var before = first.Resolver.Resolve<TransformToolSettingsService>();

        // Act
        await first.DisposeAsync().ConfigureAwait(false);
        var next = scopes.CreateWindow();

        // Assert
        _ = next.Resolver.Resolve<TransformToolSettingsService>().Should().BeSameAs(before)
            .And.BeSameAs(scopes.Host.Resolve<TransformToolSettingsService>());
    }

    [TestMethod]
    public async Task Registrations_ProjectPreferences_AreTheWindowsNotTheEditors()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);

        // Act
        var window = scopes.CreateWindow();

        // Assert: removed from the editor scope, so the window's registrations are not shadows.
        _ = ProjectPreferences.Where(type => ScopeRegistrations.CanProvide(scopes.Root, type, serviceKey: null)).Should().BeEmpty();
        _ = ScopeRegistrations.CanProvide(scopes.Root, typeof(IEditorSettingsManager), nameof(PreviewSettingsService)).Should().BeFalse();
        _ = ProjectPreferences.Where(type => !ScopeRegistrations.CanProvide(window, type, serviceKey: null)).Should().BeEmpty();
    }
}
