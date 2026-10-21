// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Input;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Managed.Assets.Diagnostics;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Script and input authoring services of the workspace: the script and input facts, the code editor and project
/// tooling.
/// </summary>
public partial class WorkspaceViewModel
{
    private IContainer? scriptContainer;

    private void RegisterScriptServices(IContainer childContainer)
    {
        this.scriptContainer = childContainer;
        childContainer.RegisterInstance(ScriptToolingPaths.Discover());
        childContainer.Register<ICodeEditorService, CodeEditorService>(Reuse.Singleton);
        childContainer.Register<ProjectScriptTooling>(Reuse.Singleton);

        // The family facts follow this workspace's asset catalog, so they live as long as it does.
        childContainer.RegisterMany([typeof(IScripts), typeof(IAssetProblemSource)], typeof(ScriptFacts), Reuse.Singleton);
        childContainer.RegisterMany([typeof(IInputSources), typeof(IAssetProblemSource)], typeof(InputFacts), Reuse.Singleton);
        this.RegisterPlayServices(childContainer);
    }

    private void OnScriptingNavigated()
    {
        if (this.projectContextService.ActiveProject is { } project && this.scriptContainer?.Resolve<ProjectScriptTooling>() is { } tooling)
        {
            _ = Task.Run(() => tooling.Ensure(project));
        }
    }
}
