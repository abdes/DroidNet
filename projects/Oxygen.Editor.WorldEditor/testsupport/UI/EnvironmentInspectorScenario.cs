// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.WorldEditor.TestSupport;

// Drives an EnvironmentView inspector through fixture, model, view and the search/scope chrome.
// - Search filtering is synchronous: EnvironmentView registers a TextBox.Text property-changed
//   callback on ScenePropertySearchBox that calls ApplyScenePropertyFilter directly, so section
//   and card Visibility can be asserted immediately after setting Text; SearchAsync's single
//   render-pass wait exists for ItemsRepeater realization coherence before measuring or
//   revealing an item, not for visibility correctness.
// - SelectScopeAsync replaces the magic SelectedIndex values with the labels declared in
//   EnvironmentView.xaml (0=All, 1=Environment, 2=Post-processing, 3=References).
// - loadContent is a delegate because LoadTestContentAsync is protected static on the UI test
//   base class and unreachable from testsupport; tests pass their method group.
internal sealed class EnvironmentInspectorScenario : IDisposable
{
    private bool disposed;

    private EnvironmentInspectorScenario(SceneAuthoringFixture fixture, EnvironmentViewModel model, EnvironmentView view)
    {
        this.Fixture = fixture;
        this.Model = model;
        this.View = view;
    }

    internal SceneAuthoringFixture Fixture { get; }

    internal EnvironmentViewModel Model { get; }

    internal EnvironmentView View { get; }

    internal static async Task<EnvironmentInspectorScenario> LoadAsync(
        Func<FrameworkElement, Task> loadContent,
        double? width = 420,
        double? height = 780,
        ElementTheme theme = ElementTheme.Default,
        ScenarioHost host = ScenarioHost.Grid,
        Action<SceneAuthoringFixture>? prepareFixture = null,
        Func<EnvironmentViewModel, Task>? prepareModel = null)
    {
        var fixture = new SceneAuthoringFixture();
        prepareFixture?.Invoke(fixture);
        var model = (EnvironmentViewModel)InspectorModels.CreateModel("Environment", fixture);
        if (prepareModel is not null)
        {
            await prepareModel(model).ConfigureAwait(true);
        }

        var view = new EnvironmentView { ViewModel = model };
        FrameworkElement content = host switch
        {
            ScenarioHost.Scroll => Sized(new ScrollViewer { Content = view, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, width, height),
            ScenarioHost.None => Sized(view, width, height),
            _ => LoadedGrid(view, width, height, theme),
        };
        await loadContent(content).ConfigureAwait(true);
        return new EnvironmentInspectorScenario(fixture, model, view);
    }

    internal async Task SearchAsync(string text)
    {
        ((TextBox)this.View.FindName("ScenePropertySearchBox")).Text = text;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }

    internal async Task SelectScopeAsync(string scopeLabel)
    {
        var selector = (CommunityToolkit.WinUI.Controls.Segmented)this.View.FindName("ScenePropertyScopeSelector");
        for (var index = 0; index < selector.Items.Count; index++)
        {
            if (selector.Items[index] is CommunityToolkit.WinUI.Controls.SegmentedItem { Content: string label }
                && string.Equals(label, scopeLabel, StringComparison.Ordinal))
            {
                selector.SelectedIndex = index;
                _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
                return;
            }
        }

        throw new InvalidOperationException(
            $"Scene property scope '{scopeLabel}' is not declared. Available scopes: {string.Join(", ", selector.Items.OfType<CommunityToolkit.WinUI.Controls.SegmentedItem>().Select(item => item.Content))}.");
    }

    internal Oxygen.Editor.Controls.PropertiesExpander Section(string name)
        => (Oxygen.Editor.Controls.PropertiesExpander)InspectorControls.FindInspectorElement(this.View, name);

    internal FrameworkElement Named(string name)
        => (FrameworkElement)InspectorControls.FindInspectorElement(this.View, name);

    internal T Element<T>(string name)
        where T : class
        => (T)InspectorControls.FindInspectorElement(this.View, name);

    internal async Task<bool> BringIntoViewAsync(Oxygen.Editor.Controls.PropertiesExpander section, object item)
    {
        var broughtIntoView = section.BringItemIntoView(item);
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
        return broughtIntoView;
    }

    internal async Task ExpandGroupAsync(string sectionName, string groupHeader)
    {
        this.Section(sectionName).Items.OfType<Expander>().Single(group => Equals(group.Header, groupHeader)).IsExpanded = true;
        _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);
    }

    internal async Task WaitForRenderAsync()
        => _ = await CompositionTargetHelper.ExecuteAfterCompositionRenderingAsync(() => { }).ConfigureAwait(true);

    internal void AssertCleanHistory()
        => _ = this.Fixture.Context.History.UndoStack.Should().BeEmpty();

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.Model.Dispose();
        this.Fixture.Dispose();
    }

    private static FrameworkElement Sized(FrameworkElement element, double? width, double? height)
    {
        if (width.HasValue)
        {
            element.Width = width.Value;
        }

        if (height.HasValue)
        {
            element.Height = height.Value;
        }

        return element;
    }

    private static Grid LoadedGrid(EnvironmentView view, double? width, double? height, ElementTheme theme)
    {
        var grid = new Grid();
        if (width.HasValue)
        {
            grid.Width = width.Value;
        }

        if (height.HasValue)
        {
            grid.Height = height.Value;
        }

        if (theme != ElementTheme.Default)
        {
            grid.RequestedTheme = theme;
        }

        grid.Children.Add(view);
        return grid;
    }
}

internal enum ScenarioHost
{
    Grid,
    Scroll,
    None,
}
