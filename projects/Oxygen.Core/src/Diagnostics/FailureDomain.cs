// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable failure-domain vocabulary used by operation diagnostics.
/// </summary>
public enum FailureDomain
{
    /// <summary>
    /// Fallback domain when a narrower domain cannot be identified.
    /// </summary>
    Unknown,

    /// <summary>
    /// Project Browser shell or UI workflow.
    /// </summary>
    ProjectBrowser,

    /// <summary>
    /// Project folder or manifest validation.
    /// </summary>
    ProjectValidation,

    /// <summary>
    /// Project manifest persistence.
    /// </summary>
    ProjectPersistence,

    /// <summary>
    /// Project template workflow.
    /// </summary>
    ProjectTemplate,

    /// <summary>
    /// Recent project or project usage state.
    /// </summary>
    ProjectUsage,

    /// <summary>
    /// Project content root validation.
    /// </summary>
    ProjectContentRoots,

    /// <summary>
    /// Workspace activation workflow.
    /// </summary>
    WorkspaceActivation,

    /// <summary>
    /// Workspace restoration workflow.
    /// </summary>
    WorkspaceRestoration,

    /// <summary>
    /// Project-scoped settings.
    /// </summary>
    ProjectSettings,

    /// <summary>
    /// Document workflow.
    /// </summary>
    Document,

    /// <summary>
    /// Scene authoring workflow.
    /// </summary>
    SceneAuthoring,

    /// <summary>
    /// Live editor-to-engine synchronization.
    /// </summary>
    LiveSync,

    /// <summary>
    /// Native runtime discovery.
    /// </summary>
    RuntimeDiscovery,

    /// <summary>
    /// Runtime surface workflow.
    /// </summary>
    RuntimeSurface,

    /// <summary>
    /// Runtime view workflow.
    /// </summary>
    RuntimeView,

    /// <summary>
    /// Content pipeline workflow.
    /// </summary>
    ContentPipeline,

    /// <summary>
    /// Asset import workflow.
    /// </summary>
    AssetImport,

    /// <summary>
    /// Asset cook workflow.
    /// </summary>
    AssetCook,

    /// <summary>
    /// Asset mount workflow.
    /// </summary>
    AssetMount,

    /// <summary>
    /// Standalone runtime validation.
    /// </summary>
    StandaloneRuntime,

    /// <summary>
    /// Global editor settings infrastructure.
    /// </summary>
    Settings,
}
