// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scripting;

namespace Oxygen.Editor.Workspace;

/// <summary>
/// Script and input authoring services of the workspace: the script and input facts, the code editor and project
/// tooling.
/// </summary>
public partial class WorkspaceViewModel
{
    private ProjectScriptTooling? scriptTooling;

    private void OnScriptingNavigated()
    {
        if (this.session?.Context is { } project && this.scriptTooling is { } tooling)
        {
            _ = Task.Run(() => tooling.Ensure(project));
        }
    }
}
