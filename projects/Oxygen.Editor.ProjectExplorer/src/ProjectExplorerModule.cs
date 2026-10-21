// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Oxygen.Editor.ProjectExplorer.Projects;
using Oxygen.Editor.ProjectExplorer.Templates;
using Oxygen.Editor.ProjectExplorer.ViewModels;
using Oxygen.Editor.ProjectExplorer.Views;

namespace Oxygen.Editor.ProjectExplorer;

/// <summary>
///     The Project Explorer in the editor scope: its templates, its project service, and the view models and views of
///     its pages.
/// </summary>
public sealed class ProjectExplorerModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        // Register the universal template source with NO key, so it gets selected when injected an
        // instance of ITemplateSource. Register specific template source implementations KEYED.
        // They are injected only as a collection of implementation instances, only by the universal
        // source.
        scope.Register<ITemplatesSource, UniversalTemplatesSource>(Reuse.Singleton);
        scope.Register<ITemplatesSource, LocalTemplatesSource>(Reuse.Singleton, serviceKey: Uri.UriSchemeFile);
        scope.Register<ITemplatesService, TemplatesService>(Reuse.Transient);

        scope.Register<IProjectExplorerService, ProjectExplorerService>(Reuse.Singleton);

        scope.Register<MainViewModel>(Reuse.Transient);
        scope.Register<MainView>(Reuse.Transient);
        scope.Register<HomeViewModel>(Reuse.Transient);
        scope.Register<HomeView>(Reuse.Transient);
        scope.Register<NewProjectViewModel>(Reuse.Transient);
        scope.Register<NewProjectView>(Reuse.Transient);
        scope.Register<OpenProjectViewModel>(Reuse.Transient);
        scope.Register<OpenProjectView>(Reuse.Transient);
    }
}
