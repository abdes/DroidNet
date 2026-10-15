// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for play sessions.
/// </summary>
public static class PlayDiagnosticCodes
{
    /// <summary>
    /// A document could not be saved, so Play stopped before cooking.
    /// </summary>
    public const string SaveFailed = DiagnosticCodes.PlayPrefix + "SAVE_FAILED";

    /// <summary>
    /// The scene or its content failed to cook, so Play stopped before the player.
    /// </summary>
    public const string CookFailed = DiagnosticCodes.PlayPrefix + "COOK_FAILED";

    /// <summary>
    /// The installed SDK has no player.
    /// </summary>
    public const string PlayerMissing = DiagnosticCodes.PlayPrefix + "PLAYER_MISSING";

    /// <summary>
    /// The player could not be started or reached.
    /// </summary>
    public const string LaunchFailed = DiagnosticCodes.PlayPrefix + "LAUNCH_FAILED";

    /// <summary>
    /// The player could not load the scene.
    /// </summary>
    public const string SceneFailed = DiagnosticCodes.PlayPrefix + "SCENE_FAILED";

    /// <summary>
    /// The player loaded the scene but could not apply one of its records.
    /// </summary>
    public const string SceneIssue = DiagnosticCodes.PlayPrefix + "SCENE_ISSUE";

    /// <summary>
    /// The player ended without being asked to.
    /// </summary>
    public const string SessionEndedUnexpectedly = DiagnosticCodes.PlayPrefix + "SESSION_ENDED_UNEXPECTEDLY";
}
