// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.World.Inspector;

/// <summary>The services the scripting inspector uses when the host provides them.</summary>
/// <param name="Analysis">The script analysis, for script slot parameters and problems.</param>
/// <param name="CodeEditor">The code editor, for editing a slot's script.</param>
/// <param name="Declarations">The declarations of library scripts, which have no source to analyze.</param>
internal sealed record InspectorScriptServices(
    IScriptAnalysisService? Analysis,
    ICodeEditorService? CodeEditor,
    IScriptDeclarationProvider? Declarations);
