// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Services;
using Oxygen.Managed.Core.Services;
using Serilog;

namespace Oxygen.Editor;

/// <summary>
///     The editor's persistent state in the editor scope: its database, the editor settings stored in it and the usage
///     of projects and templates.
/// </summary>
internal sealed class EditorDataModule : IScopeModule<IApplicationLayer>
{
    /// <inheritdoc />
    public IReadOnlyCollection<Type> Shadows => [];

    /// <inheritdoc />
    public IReadOnlyList<Type> Activation => [];

    /// <inheritdoc />
    public void Register(IRegistrator scope)
    {
        ArgumentNullException.ThrowIfNull(scope);

        scope.Register<IEditorSettingsManager, EditorSettingsManager>(Reuse.Singleton);
        scope.Register<IProjectUsageService, ProjectUsageService>(Reuse.Singleton);
        scope.Register<ITemplateUsageService, TemplateUsageService>(Reuse.Singleton);
        RegisterPersistentState(scope);
    }

    // Register PersistentState to be created lazily so IOxygenPathFinder is resolved later
    private static void RegisterPersistentState(IRegistrator scope)
        => scope.RegisterDelegate(
            resolver =>
            {
                var path = resolver.Resolve<IOxygenPathFinder>().StateDatabasePath;
                Log.Information("Configuring PersistentState database at path: {DbPath}", path);
                return new PersistentState(
                    new DbContextOptionsBuilder<PersistentState>()
                        .UseLoggerFactory(resolver.Resolve<ILoggerFactory>())
                        .EnableDetailedErrors()
                        .EnableSensitiveDataLogging()
                        .UseSqlite($"Data Source={resolver.Resolve<IOxygenPathFinder>().StateDatabasePath}; Mode=ReadWriteCreate")
                    .Options);
            },
            Reuse.Transient);
}
