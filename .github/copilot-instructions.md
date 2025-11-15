# DroidNet — Copilot Instructions

Act as a concise, pragmatic coding partner for this mono-repo of WinUI/.NET projects.
Focus on actionable edits, small surface-area changes, and following existing patterns.

## Quick Orientation
- **Repo layout:** top-level `projects/` contains modular subprojects (each with a `README.md`). Key solutions: `AllProjects.sln` and `projects/Projects.sln`.
- **Platform & tech:** WinUI 3 + .NET 10 (C#), with some C++ projects (Oxygen.Engine). Many projects use source generators, MVVM patterns, and DI (see `projects/Hosting/` and `projects/Hosting.Generators/`).
- **Where to look first:** module `README.md`, `design/`, and any `design/*.md` or `plan/` docs for architecture and rationale.

## Style & Conventions (project-specific)
- Follow the repository C# style: see `.github/instructions/csharp_coding_style.instructions.md` (C# 13 preview, `nullable` on, `ImplicitUsings`, strict analysis).
- UI code follows MVVM and WinUI practices; prefer minimal code-behind and use ViewModels and source-generated wiring (see `projects/Hosting/` and `projects/Routing/`).
- Always use explicit access modifiers (`public`, `private`, etc.) and `this.` for instance members.
- Prefer composition and small, well-justified API surfaces. When adding a public API, check `src/*/Base` or `Oxygen.Base`-like modules first.

## Architecture Highlights (how this repo is organized)
- **Modular mono-repo:** each `projects/<Name>/` is a self-contained subsystem:
  - `Hosting/`: .NET Generic Host integration for WinUI apps with UserInterfaceHostedService running UI as a background service
  - `Routing/`: Angular-inspired URL-based navigation with outlets, matrix parameters, and nested routes
  - `Docking/`: Flexible docking framework with tree-based layout management
  - `Mvvm.Generators/`: Source generators for View-ViewModel wiring using `[ViewModel]` attribute
  - `Hosting.Generators/`: Auto-injection source generators for services
  - `Oxygen.Engine/`: C++ game engine with bindless rendering (has its own `.github/copilot-instructions.md`)
  - `Aura/`: Window decoration and theming services
  - `Bootstrap/`: Application bootstrapping
  - `Collections/`, `Config/`, `Controls/`, `Converters/`, `Resources/`, `TimeMachine/`: Utility modules
- Many modules include their own `.github/copilot-instructions.md` or `instructions/` — follow those local rules when working inside that module.
- Design docs live in `plan/` and `projects/**/design` — use them to find the "why" behind non-obvious decisions.

## Code patterns and examples to reuse
- **Source generators:** check `projects/Hosting.Generators` and `projects/Mvvm.Generators` for how views/models/services are auto-wired — mimic patterns rather than inventing new generator usage.
- **DI/container:** DryIoc is used via the Hosting layer — prefer registering services in the standard host startup pattern found in `projects/Hosting/README.md`. Services are registered using Microsoft.Extensions.DependencyInjection patterns.
- **MVVM patterns:** ViewModels typically inherit from `ObservableObject` (CommunityToolkit.Mvvm) or framework base classes. Views are `partial class` with `[ViewModel(typeof(TViewModel))]` attribute for source-generated wiring.
- **MVVM routing:** `projects/Routing/` provides router and registration conventions; look at existing route declarations before proposing changes. Routes support outlets (primary, modal, popup), matrix parameters, and hierarchical navigation.
- **Docking:** managed through `Docker` class with `IDock`, `IDockable` interfaces. Supports CenterDock and ToolDock types with tree-based layout.

## Build System & Project Structure
- **Artifacts output:** Uses .NET 8+ simplified artifacts layout (`UseArtifactsOutput=true`). All build outputs go to top-level `artifacts/` directory, organized by `Configuration_TargetFramework[_RID]`.
- **Central package management:** `Directory.Packages.props` manages all NuGet versions centrally. Do not specify versions in individual `.csproj` files.
- **Project naming:** Test projects must end with `.Tests`. UI tests requiring visual interface must end with `.UI.Tests`.
- **Project layout:** Sources under `src/`, tests under `tests/`, samples under `samples/`.
- **Solution generation:** Solutions are generated using `dotnet slngen`. See `projects/open.cmd` for the pattern.

## Testing & Test Projects
- Tests use **MSTest** (not xUnit or NUnit). Use `[TestClass]`, `[TestMethod]`, `[DataRow]` attributes.
- Follow AAA (Arrange-Act-Assert) pattern with naming: `MethodName_Scenario_ExpectedBehavior`.
- See `.github/prompts/csharp-mstest.prompt.md` for detailed MSTest conventions.
- Test projects reference `MSTest`, `MSTest.Analyzers`, `AwesomeAssertions`, and optionally `Moq` for mocking.
- Use `projects/TestHelpers/` for shared test utilities.
- Run tests with: `dotnet test` or `dotnet test /p:CollectCoverage=true /p:CoverletOutputFormat=lcov`.

## Developer Workflows (explicit commands and tips)
- **Init script:** Run `init.ps1` **only once** after a fresh checkout to install the .NET SDK (when needed), restore tools, and set up pre-commit hooks. It is not intended as a frequent workflow command.

	Example (run once on a new clone):

	```powershell
	pwsh ./init.ps1
	```

- **Prefer targeted builds:** Each module contains its own `.sln` and multiple `.csproj` files. To save time during development, prefer building or testing a specific project file instead of the large solution:

	```powershell
	dotnet build projects/Hosting/src/Hosting/Hosting.csproj
	dotnet test projects/Collections/tests/Collections.Tests/Collections.Tests.csproj
	```

- **Generate/open solution:** Use the repo helper in `projects/` to regenerate and open the solution for Visual Studio:

	```powershell
	cd projects
	.\open.cmd
	```

	This uses `dotnet slngen -d . -o Projects.sln --folders false .\**\*.csproj` to generate the solution.

- **Clean build artifacts:**

	```powershell
	.\clean.ps1
	```

	Removes all `obj/`, `bin/`, and `artifacts/` directories recursively.

- **CI / full-solution tasks:** Use solution-level builds/tests in CI or when you need an end-to-end verification. For iterative development, prefer per-project commands as shown above.

## Key Dependencies & Frameworks
- **WinUI 3:** Microsoft.WindowsAppSDK 1.8+, target framework `net10.0-windows10.0.26100.0`
- **DI:** DryIoc 6.0 preview, Microsoft.Extensions.DependencyInjection/Hosting 10.0
- **Logging:** Serilog with multiple sinks (Console, Debug, File, Seq)
- **MVVM:** CommunityToolkit.Mvvm 8.4
- **Testing:** MSTest 4.0, AwesomeAssertions, Moq, Testably.Abstractions for filesystem mocking
- **Code quality:** StyleCop.Analyzers, Roslynator, Meziantou.Analyzer
- **Versioning:** Nerdbank.GitVersioning reads from `version.json`

## What to avoid / default behaviours
- Do not add broad, repo-wide public APIs without a design doc and cross-module review.
- Avoid suggesting shell commands that assume Bash; **always use PowerShell** on Windows.
- Do not manually specify package versions in `.csproj` files — they are centrally managed in `Directory.Packages.props`.
- Do not create test projects without the `.Tests` suffix.
- Do not use regions in test organization.

## If you're unsure
- Ask a focused question that references the module path (e.g., "In `projects/Hosting`, should service X be registered as singleton or transient? I see similar services in Hosting/Startup.cs").
- Read the local `README.md` and `design/` docs before proposing architectural changes.
- Check for module-specific `.github/copilot-instructions.md` (e.g., `projects/Oxygen.Engine/.github/copilot-instructions.md` for C++ projects).

---
References: `.github/instructions/csharp_coding_style.instructions.md`, `.github/prompts/csharp-mstest.prompt.md`, `projects/Oxygen.Engine/.github/copilot-instructions.md`, `projects/*/README.md`, `plan/`, `Directory.Packages.props`, `Directory.build.props`.
