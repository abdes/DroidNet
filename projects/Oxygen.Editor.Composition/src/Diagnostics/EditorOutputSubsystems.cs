// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Output;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.Diagnostics;

/// <summary>
///     The editor's subsystems for the Output panel: the feature areas a user recognizes, mapped from the logger or
///     source names the editor, the engine, the cooker and the player write under.
/// </summary>
/// <remarks>
///     <para>
///         The longest matching prefix wins, so a feature area's prefix takes precedence over the <c>Oxygen</c> and
///         <c>DroidNet</c> roots, which make <see cref="Editor" /> the catch-all for the editor's own code: the shell,
///         composition, startup, hosting, commands, the Output and Problems panels and generic controls. A source no
///         prefix covers, such as a third-party library's logger, has no subsystem.
///     </para>
///     <para>
///         The content pipeline serves two areas: cooking and publication belong to <see cref="Cooker" />; asset
///         authoring and relocation to <see cref="Assets" />. Three cooking services sit in the pipeline's root
///         namespace, so they are mapped by their type names.
///     </para>
/// </remarks>
public static class EditorOutputSubsystems
{
    /// <summary>The native engine and its lifecycle in the editor: start, shutdown, startup settings.</summary>
    public const string Engine = "Engine";

    /// <summary>The player process Play launches: its output, control channel and settings.</summary>
    public const string Player = "Player";

    /// <summary>The live preview of a scene in the editor's viewports: preview sessions and content demand.</summary>
    public const string Preview = "Preview";

    /// <summary>Cooking and publishing assets into runtime content, model import, and the cooked output.</summary>
    public const string Cooker = "Cooker";

    /// <summary>The asset library: catalog, Content Explorer, authoring, moving and renaming, pickers, textures.</summary>
    public const string Assets = "Assets";

    /// <summary>Material documents and the Material Editor.</summary>
    public const string Materials = "Materials";

    /// <summary>The scene: its model, viewport editing, Scene Explorer and the Properties inspector.</summary>
    public const string Scene = "Scene";

    /// <summary>Scripts: analysis, script facts and project script tooling.</summary>
    public const string Scripting = "Scripting";

    /// <summary>Input sources and bindings.</summary>
    public const string Input = "Input";

    /// <summary>Creating, opening and loading projects, templates, recent projects and project sessions.</summary>
    public const string Project = "Project";

    /// <summary>Open documents, their tabs and windows, and saving.</summary>
    public const string Documents = "Documents";

    /// <summary>Windows, docking panels and the workspace layout.</summary>
    public const string Layout = "Layout";

    /// <summary>Navigation between the editor's views.</summary>
    public const string Router = "Router";

    /// <summary>Editor and user settings, themes and appearance.</summary>
    public const string Settings = "Settings";

    /// <summary>The catch-all for the editor's own code that no feature area covers.</summary>
    public const string Editor = "Editor";

    /// <summary>Gets the map from source prefixes to the editor's subsystems.</summary>
    public static SubsystemMap Map { get; } = new(
    [
        // Engine: the native engine's log category and the in-process engine service.
        new(EngineConstants.LogCategory, Engine),
        new("Oxygen.Editor.Runtime", Engine),

        // Player: the external player process and Play's session with it.
        new(OutputModule.PlayerSource, Player),
        new("Oxygen.Editor.Runtime.Player", Player),
        new("Oxygen.Editor.LivePreview.Play", Player),

        // Preview: everything else of the live preview.
        new("Oxygen.Editor.LivePreview", Preview),

        // Cooker.
        new("Oxygen.Editor.ContentPipeline.Cooking", Cooker),
        new("Oxygen.Editor.ContentPipeline.Publication", Cooker),
        new(typeof(ContentCookCoordinator).FullName!, Cooker),
        new(typeof(MaterialCookService).FullName!, Cooker),
        new(typeof(ImportToolContentPipelineApi).FullName!, Cooker),
        new("Oxygen.Editor.Cooking", Cooker),
        new("Oxygen.Editor.CookedInspector", Cooker),

        // Assets: the rest of the content pipeline, and the asset library's modules.
        new("Oxygen.Editor.ContentPipeline", Assets),
        new("Oxygen.Editor.Assets", Assets),
        new("Oxygen.Managed.Assets", Assets),
        new("Oxygen.Editor.ContentExplorer", Assets),
        new("Oxygen.Editor.Pickers", Assets),
        new("Oxygen.Editor.Textures", Assets),

        new("Oxygen.Editor.Materials", Materials),
        new("Oxygen.Editor.MaterialEditor", Materials),

        new("Oxygen.Editor.World", Scene),
        new("Oxygen.Editor.Scenes", Scene),
        new("Oxygen.Editor.SceneEditor", Scene),
        new("Oxygen.Editor.SceneExplorer", Scene),
        new("Oxygen.Editor.SceneInspector", Scene),

        new("Oxygen.Editor.Scripting", Scripting),

        new("Oxygen.Editor.Input", Input),

        new("Oxygen.Editor.Projects", Project),
        new("Oxygen.Editor.ProjectExplorer", Project),
        new("Oxygen.Editor.Sessions", Project),
        new("Oxygen.Editor.Data", Project),

        new("Oxygen.Editor.Documents", Documents),
        new("Oxygen.Editor.DocumentWindows", Documents),
        new("Oxygen.Editor.Shell.Documents", Documents),
        new("DroidNet.Aura.Documents", Documents),

        new("Oxygen.Editor.Shell.Layout", Layout),
        new("Oxygen.Editor.Shell.Windows", Layout),
        new("Oxygen.Editor.Workspace", Layout),
        new("DroidNet.Aura", Layout),
        new("DroidNet.Docking", Layout),

        new("Oxygen.Editor.Routing", Router),
        new("DroidNet.Routing", Router),

        new("DroidNet.Config", Settings),
        new("DroidNet.Aura.Settings", Settings),
        new("DroidNet.Aura.Theming", Settings),

        // Editor: the catch-all for the editor's own code, and the operation results it reports.
        new("Oxygen", Editor),
        new("DroidNet", Editor),
        new(OperationResultOutputLogAdapter.Source, Editor),
    ]);
}
