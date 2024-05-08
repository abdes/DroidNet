// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using System.Diagnostics;
using System.Reactive.Subjects;
using Oxygen.Editor.ViewModels;

public class ActivationService : IActivationService, IDisposable
{
    private readonly Func<object, Task> afterActivation;

    private readonly Func<Task> beforeActivation;
    private readonly Subject<object> subject = new();

    /// <summary>
    /// Initializes a new instance of the <see cref="ActivationService" />
    /// class.
    /// </summary>
    /// <param name="navigation"></param>
    /// <param name="theme"></param>
    public ActivationService(INavigationService navigation, IThemeSelectorService theme)
    {
        this.beforeActivation = () => Task.CompletedTask;

        this.afterActivation = (object activationData) =>
        {
            if (navigation?.Frame?.Content == null)
            {
                // None of the ActivationHandlers has handled the activation.
                Debug.WriteLine("Default activation handler => no view set, navigate to main view");
                _ = navigation?.NavigateTo(typeof(MainViewModel).FullName!, activationData);
            }

            theme.ApplyTheme();

            return Task.CompletedTask;
        };
    }

    public IDisposable Subscribe(IObserver<object> observer)
        => this.subject.Subscribe(observer);

    public async void Activate(object activationData)
    {
        await this.beforeActivation().ConfigureAwait(false);
        this.subject.OnNext(activationData);
        await this.afterActivation(activationData).ConfigureAwait(false);
    }

    public void Dispose()
    {
        this.subject.Dispose();
        GC.SuppressFinalize(this);
    }
}
