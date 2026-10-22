// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Composition;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.EntityFrameworkCore.Infrastructure;
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
/// <remarks>
///     The database's contexts come from one pool, configured once. A context resolved here is short-lived: its owner
///     disposes it, which returns it to the pool, so repeated reads and writes create no new context.
/// </remarks>
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

    /// <summary>Rents a context over the editor's state database from the editor's pool.</summary>
    /// <param name="resolver">Resolves the editor's pool of contexts.</param>
    /// <returns>A context, which its owner disposes to return it to the pool.</returns>
    internal static PersistentState CreatePersistentState(IResolverContext resolver)
        => resolver.Resolve<IDbContextFactory<PersistentState>>().CreateDbContext();

    // The pool is configured once, when the database is first used, so IOxygenPathFinder is resolved late.
    private static void RegisterPersistentState(IRegistrator scope)
    {
        scope.RegisterDelegate<IDbContextFactory<PersistentState>>(CreatePool, Reuse.Singleton);
        scope.RegisterDelegate<PersistentState>(CreatePersistentState, Reuse.Transient, setup: Setup.With(preventDisposal: true));
    }

    private static PooledDbContextFactory<PersistentState> CreatePool(IResolverContext resolver)
    {
        var path = resolver.Resolve<IOxygenPathFinder>().StateDatabasePath;
        Log.Information("Configuring PersistentState database at path: {DbPath}", path);
        return new PooledDbContextFactory<PersistentState>(
            new DbContextOptionsBuilder<PersistentState>()
                .UseLoggerFactory(resolver.Resolve<ILoggerFactory>())
                .EnableDetailedErrors()
                .EnableSensitiveDataLogging()
                .UseSqlite($"Data Source={path}; Mode=ReadWriteCreate")
            .Options);
    }
}
