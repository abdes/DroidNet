// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DroidNet.Config;
using DroidNet.Coordinates;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm;
using DroidNet.Routing;
using DroidNet.Storage;
using DryIoc;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Caching.Memory;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Composition.UI.Tests.TestSupport;
using Oxygen.Editor.ContentExplorer;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.CookedInspector;
using Oxygen.Editor.Data;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Problems;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Sessions;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Composition.UI.Tests;

/// <summary>
///     The editor's scope chain as the application composes it: the editor scope, a project session's scope, a workspace
///     window's scope and the Content Explorer's scope. Each scope registers only what it owns and resolves the rest from
///     its parent, so shared identity does not depend on which scope resolves first (spec 6.3).
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Topology")]
public sealed class TopologyTests
{
    // Container infrastructure: the router, the view locator, the window factory, the settings manager, the UI thread
    // and the dock views locate the services they create.
    private static readonly string[] Infrastructure =
    [
        nameof(DefaultViewLocator),
        nameof(Router),
        "WindowContextProvider",
        nameof(DefaultWindowFactory),
        nameof(SettingsManager),
        nameof(UserInterfaceThread),
        "DockViewFactory",
    ];

    // Service locators that a later slice removes; an entry that no longer occurs fails the test.
    private static readonly Dictionary<string, string> PendingLocators = new(StringComparer.Ordinal)
    {
        [nameof(SceneEditorProvider)] = "S6.1 removes IContainer from the scene editor's view model",
    };

    // The services that create child scopes: each takes the handle of the scope that builds it.
    private static readonly string[] ScopeOwners = [nameof(ProjectSessionHost), nameof(ContentExplorerViewModel)];

    // Singleton views that a later slice makes transient (spec 6.2); an entry that no longer occurs fails the test.
    private static readonly Dictionary<string, string> PendingSingletonViews = new(StringComparer.Ordinal)
    {
        [nameof(DocumentAreaView)] = "S5.3 creates the window's document host per window",
    };

    // Constructor arguments that the code creating the type passes through a Func<> factory.
    private static readonly string[] FactoryArguments = ["DockPanelViewModel(IDock dock)"];

    // The reviewed delegate registrations of the editor scope, without the bootstrapper's.
    private static readonly (string Service, object? Key)[] EditorDelegates =
    [
        (nameof(ISettingsManager), null),
        (ScopeTopology.NameOf(typeof(ISettingsService<IWindowDecorationSettings>)), null),
        (ScopeTopology.NameOf(typeof(ISettingsService<IAppearanceSettings>)), null),
        (ScopeTopology.NameOf(typeof(ISettingsService<IEngineStartupSettings>)), null),
        (nameof(SpatialMapperFactory), null),
        (nameof(RawSpatialMapperFactory), null),
        (nameof(TransformToolSettingsService), null),
        (nameof(IHostedService), null),
        (nameof(IMemoryCache), null),
        (nameof(IAtomicFileStore), null),
        (nameof(IOperationResultStore), null),
        (nameof(IOperationResultPublisher), null),
        (nameof(Window), new DroidNet.Routing.Target { Name = "wnd-we" }),
        (nameof(Window), new DroidNet.Routing.Target { Name = "wnd-pe" }),
        (nameof(IEngineContentPak), null),
        (nameof(INativeCompatibilityService), EditorNativeCompatibilityService.CookingServiceKey),
        (nameof(ImportToolContentPipelineApi), null),
        (nameof(IEngineContentPipelineApi), null),
        (ScopeTopology.NameOf(typeof(IDbContextFactory<PersistentState>)), null),
        (nameof(PersistentState), null),
    ];

    // The reviewed delegate registrations of a project session's scope.
    private static readonly (string Service, object? Key)[] SessionDelegates =
    [
        (nameof(IContentPipelineService), null),
        (nameof(IGeometryMaterialSlotProvider), null),
        (nameof(IScriptLibrary), null),
        (nameof(SceneNodeInteraction), null),
        (nameof(ISceneNodeInteraction), null),
        (nameof(PreviewSettingsService), null),
        (nameof(ViewportStateService), null),
        (nameof(PersistentState), null),
        (nameof(OpenDocuments), null),
        (nameof(IAssetCookStatusReader), null),
        (nameof(IScenePreview), null),
        ("PreviewContentDemand", null),
        (nameof(IAssetProblemSources), null),
        (nameof(DocumentEditors), null),
    ];

    // The reviewed delegate registrations of a workspace window's scope.
    private static readonly (string Service, object? Key)[] WindowDelegates =
    [
        (nameof(IContextProvider), null),
        (nameof(ContributionRegistry), null),
        (nameof(DocumentArea), null),
        (nameof(IDocumentArea), null),
        (nameof(CookedInspectionViewModel), null),
        (nameof(EditingViewMaskService), null),
    ];

    // The reviewed delegate registrations of the Content Explorer's scope.
    private static readonly (string Service, object? Key)[] ExplorerDelegates = [(nameof(IContextProvider), null)];

    [TestMethod]
    public async Task Create_SessionWindowAndExplorer_DeclareEveryShadow()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);

        // Act: the factory refuses a scope with an undeclared shadow.
        var create = () => scopes.CreateExplorer(scopes.CreateWindow(scopes.CreateSession()));

        // Assert
        _ = create.Should().NotThrow();
    }

    [TestMethod]
    public async Task Activation_OfEveryModule_IsProvidedByItsScope()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var missing = Unprovided(session, EditorScopes.SessionModules().SelectMany(static module => module.Activation))
            .Concat(Unprovided(window, scopes.WindowModules().SelectMany(static module => module.Activation)))
            .Concat(Unprovided(explorer, EditorScopes.ExplorerModules().SelectMany(static module => module.Activation)));

        // Assert
        _ = missing.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Resolve_EditorSingletonFromTheExplorerFirst_ReturnsTheEditorInstance()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var fromExplorer = explorer.Resolver.Resolve<IOperationResultStore>();

        // Assert
        _ = fromExplorer.Should().BeSameAs(window.Resolver.Resolve<IOperationResultStore>());
        _ = fromExplorer.Should().BeSameAs(session.Resolver.Resolve<IOperationResultStore>());
        _ = fromExplorer.Should().BeSameAs(scopes.Host.Resolve<IOperationResultStore>());
    }

    [TestMethod]
    public async Task Resolve_SessionSingletonFromTheExplorerFirst_ReturnsTheSessionInstance()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var fromExplorer = explorer.Resolver.Resolve<IHistoryService>();

        // Assert
        _ = fromExplorer.Should().BeSameAs(window.Resolver.Resolve<IHistoryService>());
        _ = fromExplorer.Should().BeSameAs(session.Resolver.Resolve<IHistoryService>());
        _ = scopes.Host.Resolve<IHistoryService>(IfUnresolved.ReturnDefault).Should().BeNull("the history is the session's");
    }

    [TestMethod]
    public async Task Resolve_WindowSingletonFromTheExplorerFirst_ReturnsTheWindowInstance()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var fromExplorer = explorer.Resolver.Resolve<ICommandService>();

        // Assert
        _ = fromExplorer.Should().BeSameAs(window.Resolver.Resolve<ICommandService>());
        _ = session.Resolver.Resolve<ICommandService>(IfUnresolved.ReturnDefault).Should().BeNull("the commands are the window's");
    }

    [TestMethod]
    public async Task Resolve_HostOpenGenericsFromTheExplorer_ResolveThroughTheChain()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var explorer = scopes.CreateExplorer(scopes.CreateWindow(scopes.CreateSession()));

        // Act
        var logger = explorer.Resolver.Resolve<ILogger<TopologyTests>>();
        var options = explorer.Resolver.Resolve<IOptions<LoggerFilterOptions>>();

        // Assert
        _ = logger.Should().NotBeNull();
        _ = options.Should().BeSameAs(scopes.Host.Resolve<IOptions<LoggerFilterOptions>>());
    }

    [TestMethod]
    public async Task Constructors_OfEveryScope_TakeNoServiceLocator()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var locators = new[] { scopes.Root, session, window, explorer }
            .SelectMany(static scope => ScopeTopology.Built(ScopeTopology.Owned(scope)))
            .Where(static built => ScopeTopology.LocatorParameters(built.Constructor).Any())
            .Select(static built => built.Type.Name)
            .Where(static name => !Infrastructure.Contains(name))
            .ToHashSet();

        // Assert
        _ = locators.Should().BeEquivalentTo(PendingLocators.Keys, "only container infrastructure locates services");
    }

    [TestMethod]
    public async Task Constructors_TakingAScopeHandle_AreTheScopeOwners()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var takers = new[] { scopes.Root, session, window, explorer }
            .SelectMany(static scope => ScopeTopology.Built(ScopeTopology.Owned(scope)))
            .Where(static built => built.Constructor.GetParameters().Any(static parameter => parameter.ParameterType == typeof(ScopeHandle)))
            .Select(static built => built.Type.Name)
            .ToHashSet();

        // Assert
        _ = takers.Should().BeEquivalentTo(ScopeOwners, "a scope handle is not a service locator");
    }

    [TestMethod]
    public async Task Constructors_UnderTheEditorScope_TakeNoCollectionOfAParentService()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var collections = Violations(session, ScopeTopology.CrossLayerCollections)
            .Concat(Violations(window, ScopeTopology.CrossLayerCollections))
            .Concat(Violations(explorer, ScopeTopology.CrossLayerCollections));

        // Assert
        _ = collections.Should().BeEmpty("a collection sees only its scope's registrations");
    }

    [TestMethod]
    public async Task CrossLayerCollections_ProblemSourcesTakenInTheWindow_IsAViolation()
    {
        // Arrange: the session registers the problem sources; a window type that took their collection would see none.
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var window = scopes.CreateWindow(scopes.CreateSession());
        var constructor = typeof(ProblemSourcesTaker).GetConstructors().Single();

        // Act
        var violations = ScopeTopology.CrossLayerCollections(window, constructor);

        // Assert
        _ = violations.Should().ContainSingle().Which.ParameterType.Should().Be<IEnumerable<IAssetProblemSource>>();
    }

    [TestMethod]
    public async Task Constructors_OfTheSession_TakeNoWindowBoundDependency()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();

        // Act: a service the session builds resolves without any window, so it never holds one window's state.
        var windowBound = Violations(session, ScopeTopology.Unresolvable)
            .Concat(ScopeTopology.Built(ScopeTopology.Owned(session))
                .SelectMany(static built => built.Constructor.GetParameters()
                    .Where(static parameter => parameter.ParameterType == typeof(WindowId))
                    .Select(parameter => $"{built.Type.Name}({parameter.ParameterType.Name} {parameter.Name})")));

        // Assert
        _ = windowBound.Should().BeEmpty("a session service serves every window of its session");
    }

    [TestMethod]
    public async Task Constructors_UnderTheEditorScope_TakeNoProjectContext()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act: a mount change gives the session a new project context, so a service reads it from the session.
        var captured = new[] { session, window, explorer }
            .SelectMany(static scope => ScopeTopology.Built(ScopeTopology.Owned(scope)))
            .SelectMany(static built => built.Constructor.GetParameters()
                .Where(static parameter => parameter.ParameterType == typeof(ProjectContext))
                .Select(parameter => $"{built.Type.Name}({parameter.Name})"));

        // Assert
        _ = captured.Should().BeEmpty("a constructor that takes a ProjectContext would keep one a mount change replaced");
        _ = ScopeTopology.Owned(session).Select(static registration => registration.ServiceType).Should().NotContain(typeof(ProjectContext));
    }

    [TestMethod]
    public async Task Constructors_OfWindowAndExplorer_AreResolvable()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var unresolvable = Violations(window, ScopeTopology.Unresolvable)
            .Concat(Violations(explorer, ScopeTopology.Unresolvable))
            .Except(FactoryArguments);

        // Assert
        _ = unresolvable.Should().BeEmpty();
    }

    [TestMethod]
    public async Task Registrations_OfEveryScope_HaveNoSingletonView()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var views = new[] { scopes.Root, session, window, explorer }
            .SelectMany(static scope => ScopeTopology.SingletonViews(ScopeTopology.Owned(scope)))
            .Select(static type => type.Name)
            .ToHashSet();

        // Assert
        _ = views.Should().BeEquivalentTo(PendingSingletonViews.Keys, "a view cannot be shared across windows");
    }

    [TestMethod]
    public async Task DelegateRegistrations_OfEveryScope_AreTheReviewedOnes()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);
        var session = scopes.CreateSession();
        var window = scopes.CreateWindow(session);
        var explorer = scopes.CreateExplorer(window);

        // Act
        var editor = ScopeTopology.Delegates(scopes.EditorRegistrations());
        var sessionDelegates = ScopeTopology.Delegates(ScopeTopology.Owned(session));
        var windowDelegates = ScopeTopology.Delegates(ScopeTopology.Owned(window));
        var explorerDelegates = ScopeTopology.Delegates(ScopeTopology.Owned(explorer));

        // Assert: a delegate's dependencies are opaque to these checks, so each one is reviewed.
        _ = editor.Should().BeEquivalentTo(EditorDelegates);
        _ = sessionDelegates.Should().BeEquivalentTo(SessionDelegates);
        _ = windowDelegates.Should().BeEquivalentTo(WindowDelegates);
        _ = explorerDelegates.Should().BeEquivalentTo(ExplorerDelegates);
    }

    [TestMethod]
    public async Task Registrations_OfTheEditorScope_LeaveTheDockPanelsToTheWindow()
    {
        // Arrange
        await using var scopes = await EditorScopes.CreateAsync().ConfigureAwait(false);

        // Act
        var window = scopes.CreateWindow(scopes.CreateSession());

        // Assert: a dock panel is built where it sees the window's view location.
        _ = ScopeRegistrations.CanProvide(scopes.Root, typeof(DroidNet.Docking.Controls.DockPanelViewModel), serviceKey: null).Should().BeFalse();
        _ = ScopeRegistrations.CanProvide(scopes.Root, typeof(DroidNet.Docking.Controls.DockPanel), serviceKey: null).Should().BeFalse();
        _ = ScopeTopology.Owned(window).Select(static registration => registration.ServiceType)
            .Should().Contain([typeof(DroidNet.Docking.Controls.DockPanelViewModel), typeof(DroidNet.Docking.Controls.DockPanel)]);
    }

    private static IEnumerable<string> Unprovided(ScopeHandle scope, IEnumerable<Type> services)
        => services.Where(service => !ScopeRegistrations.CanProvide(scope, service, serviceKey: null)).Select(static service => service.Name);

    private static IEnumerable<string> Violations(
        ScopeHandle scope,
        Func<ScopeHandle, System.Reflection.ConstructorInfo, IEnumerable<System.Reflection.ParameterInfo>> rule)
        => ScopeTopology.Built(ScopeTopology.Owned(scope))
            .SelectMany(built => rule(scope, built.Constructor).Select(parameter => $"{built.Type.Name}({parameter.ParameterType.Name} {parameter.Name})"));

    // A window type that would take the session's problem sources as a collection, which the rule refuses.
    private sealed class ProblemSourcesTaker(IEnumerable<IAssetProblemSource> sources)
    {
        public IEnumerable<IAssetProblemSource> Sources => sources;
    }
}
