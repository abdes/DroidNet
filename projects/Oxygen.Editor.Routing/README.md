# Oxygen.Editor.Routing

Local (embedded) routed navigation for the Oxygen Editor.

This project adapts the shared `DroidNet.Routing` router to work as a **child router** hosted inside an existing editor view model tree (rather than as a top-level, window-targeted router).

In other words:

- **Global/WinUI routing** typically selects a navigation *target* (often a `Window`) and loads the root route into that target.
- **Local editor routing** always routes **inside the currently active target** and loads content into **outlets owned by view models**.

## What this module provides

- `LocalRouterContext` / `ILocalRouterContext`
  - A navigation context specialized for child routing.
  - Designed so that local routing is always scoped to `Target.Self` (this is the point of “local”).
- `LocalRouteActivator`
  - Loads activated route view models into the correct `IOutletContainer`.
  - Uses the nearest parent route view model that implements `IOutletContainer`, or falls back to the context’s `RootViewModel`.
- `LocalRouterContextProvider`
  - A context provider for local routers.
  - Always returns the same local context and treats it as “created” once `LocalRouter` is assigned.
- `LocalRoutingExtensions`
  - `WithMvvm(...)` and `WithLocalRouting(...)` helpers to wire up a local router in a DryIoc container.

## How local route activation works

The shared router activation pipeline is split into two responsibilities:

1. **Create / reuse view models + run `IRoutingAware` hooks**
   - Done by the shared router’s activation observer (resolved via DI).
2. **Load the resulting view model into the UI tree**
   - Done by the route activator implementation.

This module implements (2) for the editor’s local routing scenario.

Activation behavior in `LocalRouteActivator`:

- If a route has no view model, activation is a no-op.
- Otherwise the activator finds the effective parent container:
  - Walk up to the nearest parent route whose `ViewModel` is non-null.
  - If none exists, use `ILocalRouterContext.RootViewModel`.
- The effective parent must implement `DroidNet.Routing.WinUI.IOutletContainer`.
  - The activator calls `LoadContent(route.ViewModel, route.Outlet)`.
  - If no suitable `IOutletContainer` exists, activation throws.

## How local navigation contexts work

Local routing intentionally does **not** model multi-window / multi-target navigation:

- `LocalRouterContext` uses `Target.Self` as its `NavigationTargetKey`.
- `LocalRouterContextProvider` ignores the requested target and always returns the same context.
- `ActivateContext(...)` is a no-op because the local router does not “activate a target” (there is no window/tab to foreground).

If you need to navigate to a different target (e.g., open/activate a different window), use the parent/global router.

## Using it (DryIoc)

The intended usage is to create a *dedicated child container* for the local router, register the local context and routes, then resolve the local `IRouter`.

```csharp
using DroidNet.Routing;
using DryIoc;
using Oxygen.Editor.Routing;

// 1) Prepare the local context
var localContext = new LocalRouterContext(target: /* any host-specific token */ new object())
{
    RootViewModel = /* your outlet container VM (must implement IOutletContainer) */ rootViewModel,
};

// Optional but recommended: allow local view models to navigate globally when needed
localContext.ParentRouter = globalRouter;

// 2) Build a child container (recommended so local registrations don’t pollute the global container)
var localContainer = new Container();

// 3) Register MVVM + local routing services
localContainer
    .WithMvvm()
    .WithLocalRouting(routesConfig: localRoutes, localRouterContext: localContext);

// 4) Use the local router
await localContext.LocalRouter.NavigateAsync("/some/local/path");
```

Notes:

- `WithLocalRouting(...)` assigns `localRouterContext.LocalRouter` after registering `IRouter`.
- Your host/root view model (and any parent route view models that host children) must implement `IOutletContainer` so that routes can load content into outlets.

## Relationship to Routing.WinUI

This module reuses WinUI routing primitives (notably `IOutletContainer`) for outlet-based content loading, but it is **not** a window context provider.

- `Routing.WinUI` (`WindowContextProvider` / `WindowRouteActivator`) is oriented around Windows (`Window`) as navigation targets.
- `Oxygen.Editor.Routing` (`LocalRouterContextProvider` / `LocalRouteActivator`) is oriented around an already-active editor view model tree as the host.

## Build & test

```powershell
dotnet build projects/Oxygen.Editor.Routing/src/Oxygen.Editor.Routing.csproj

dotnet test --project projects/Oxygen.Editor.Routing/tests/Oxygen.Editor.Routing.Tests.csproj
```
