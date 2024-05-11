// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using System.Collections.Specialized;
using System.Web;
using Microsoft.UI.Dispatching;
using Microsoft.Windows.AppNotifications;

public class AppNotificationService : IAppNotificationService
{
    public void Initialize()
    {
        AppNotificationManager.Default.NotificationInvoked += this.OnNotificationInvoked;

        AppNotificationManager.Default.Register();
    }

    public bool Show(string payload)
    {
        var appNotification = new AppNotification(payload);

        AppNotificationManager.Default.Show(appNotification);

        return appNotification.Id != 0;
    }

    public NameValueCollection ParseArguments(string arguments)
        => HttpUtility.ParseQueryString(arguments);

    public void Unregister() => AppNotificationManager.Default.Unregister();

    public void OnNotificationInvoked(AppNotificationManager sender, AppNotificationActivatedEventArgs args) =>

        // TODO: Handle notification invocations when your app is already running.
        //// // Navigate to a specific page based on the notification arguments.
        //// if (ParseArguments(args.Argument)["action"] == "Settings")
        //// {
        ////    App.MainWindow.DispatcherQueue.TryEnqueue(() =>
        ////    {
        ////        _navigationService.NavigateTo(typeof(SettingsViewModel).FullName!);
        ////    });
        //// }
        DispatcherQueue.GetForCurrentThread()
            .TryEnqueue(
                () =>
                {
                    /* TODO: refactor how notifications are handled
                    App.MainWindow.ShowMessageDialogAsync(
                        "TODO: Handle notification invocations when your app is already running.",
                        "Notification Invoked");

                    App.MainWindow.BringToFront();
                    */
                });

    /// <summary>
    /// Finalizes an instance of the <see cref="AppNotificationService" />
    /// class.
    /// </summary>
    ~AppNotificationService() => this.Unregister();
}
