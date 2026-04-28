// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Diagnostic code prefixes reserved for editor operation results.
/// </summary>
public static class DiagnosticCodes
{
    /// <summary>
    /// Project-domain diagnostic code prefix.
    /// </summary>
    public const string ProjectPrefix = "OXE.PROJECT.";

    /// <summary>
    /// Workspace-domain diagnostic code prefix.
    /// </summary>
    public const string WorkspacePrefix = "OXE.WORKSPACE.";

    /// <summary>
    /// Native runtime startup and discovery diagnostic code prefix.
    /// </summary>
    public const string RuntimePrefix = "OXE.RUNTIME.";

    /// <summary>
    /// Runtime surface diagnostic code prefix.
    /// </summary>
    public const string SurfacePrefix = "OXE.SURFACE.";

    /// <summary>
    /// Runtime view diagnostic code prefix.
    /// </summary>
    public const string ViewPrefix = "OXE.VIEW.";

    /// <summary>
    /// Viewport UI diagnostic code prefix.
    /// </summary>
    public const string ViewportPrefix = "OXE.VIEWPORT.";

    /// <summary>
    /// Editor settings diagnostic code prefix.
    /// </summary>
    public const string SettingsPrefix = "OXE.SETTINGS.";

    /// <summary>
    /// Runtime cooked-root mount diagnostic code prefix.
    /// </summary>
    public const string AssetMountPrefix = "OXE.ASSETMOUNT.";

    /// <summary>
    /// Scene authoring diagnostic code prefix.
    /// </summary>
    public const string ScenePrefix = "OXE.SCENE.";

    /// <summary>
    /// Scene document diagnostic code prefix.
    /// </summary>
    public const string DocumentPrefix = "OXE.DOCUMENT.";

    /// <summary>
    /// Live scene sync diagnostic code prefix.
    /// </summary>
    public const string LiveSyncPrefix = "OXE.LIVESYNC.";

    /// <summary>
    /// Material authoring diagnostic code prefix.
    /// </summary>
    public const string MaterialPrefix = "OXE.MATERIAL.";

    /// <summary>
    /// Asset identity diagnostic code prefix.
    /// </summary>
    public const string AssetIdentityPrefix = "OXE.ASSETID.";

    /// <summary>
    /// Content pipeline diagnostic code prefix.
    /// </summary>
    public const string ContentPipelinePrefix = "OXE.CONTENTPIPELINE.";

    /// <summary>
    /// Asset import diagnostic code prefix.
    /// </summary>
    public const string AssetImportPrefix = "OXE.ASSETIMPORT.";

    /// <summary>
    /// Asset cook diagnostic code prefix.
    /// </summary>
    public const string AssetCookPrefix = "OXE.ASSETCOOK.";
}
