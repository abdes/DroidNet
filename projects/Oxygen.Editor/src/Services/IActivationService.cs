// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

/// <summary>Handle the activation.</summary>
/// Only the following 4 activation types are supported as of now (
/// <see href="https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/applifecycle/applifecycle-rich-activation#activation-details-for-unpackaged-apps">
/// Activation
/// details for unpackaged apps.
/// </see>
/// ).
/// <list type="table">
/// <item>
/// <term>Launch</term>
/// <description>
/// Activate the app from the command line, when the user double-clicks the app's
/// icon, or programmatically via ShellExecute or CreateProcess. Data type is
/// <c>LaunchActivatedEventArgs</c>.
/// </description>
/// </item>
/// <item>
/// <term>File</term>
/// <description>
/// Activate an app that has registered for a file type when a file of the type is
/// opened via ShellExecute, Launcher.LaunchFileAsync, or the command line. Data
/// type is <c>IFileActivatedEventArgs</c>.
/// </description>
/// </item>
/// <item>
/// <term>Protocol</term>
/// <description>
/// Activate an app that has registered for a protocol when a string of that
/// protocol is executed via ShellExecute, Launcher.LaunchUriAsync, or the
/// command-line. Data type is <c>IProtocolActivatedEventArgs</c>.
/// </description>
/// </item>
/// <item>
/// <term>StartupTask</term>
/// <description>
/// Activate the app when the user logs into Windows, either because of a registry
/// key, or because of a shortcut in a well-known startup folder. Data type is
/// <c>IStartupTaskActivatedEventArgs</c>.
/// </description>
/// </item>
/// <item>
/// <term>Notification</term>
/// <description>
/// This activation is originated from clicking on a notification while the
/// application is running. It is not checked by the Application inside the
/// <c>OnLaunched</c> lifecycle event handler like the previous activation kinds.
/// Instead it is managed via the notification service. Data type is
/// <c>AppNotificationActivatedEventArgs</c>.
/// </description>
/// </item>
/// </list>
public interface IActivationService : IObservable<object>
{
    void Activate(object activationData);
}
