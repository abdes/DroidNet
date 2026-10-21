// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;

namespace Oxygen.Editor.Projects;

/// <summary>
///     The project services of the editor scope: the active project, its creation, validation and persistence, the
///     recent projects, and the resolution of authoring targets and cook scopes.
/// </summary>
public sealed class ProjectsModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        scope.Register<IProjectValidationService, ProjectValidationService>(Reuse.Singleton);
        scope.Register<IProjectContextService, ProjectContextService>(Reuse.Singleton);
        scope.Register<IProjectCookScopeProvider, ProjectCookScopeProvider>(Reuse.Singleton);
        scope.Register<IAuthoringTargetResolver, AuthoringTargetResolver>(Reuse.Singleton);
        scope.Register<IProjectCreationService, ProjectCreationService>(Reuse.Singleton);
        scope.Register<IRecentProjectAdapter, RecentProjectAdapter>(Reuse.Singleton);
        scope.Register<IProjectManagerService, ProjectManagerService>(Reuse.Singleton);
    }
}
