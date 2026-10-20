// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Scripting;

namespace Oxygen.Editor.World.Inspector;

/// <summary>The services the scripting inspector uses when the host provides them.</summary>
/// <param name="Scripts">The scripts, for script slot parameters and problems.</param>
/// <param name="CodeEditor">The code editor, for editing a slot's script.</param>
internal sealed record InspectorScriptServices(IScripts? Scripts, ICodeEditorService? CodeEditor);
