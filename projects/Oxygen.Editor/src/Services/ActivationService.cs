// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Services;

using System.Reactive.Subjects;

/// <summary>
/// Handle registration and chain invocation during activation.
/// </summary>
/// <param name="theme">
/// The service that can be used to change the application theme.
/// TODO: Review and refactor this class
/// </param>
public partial class ActivationService(IThemeSelectorService theme) : IActivationService, IDisposable
{
    private readonly Func<object, Task> afterActivation = (activationData) =>
    {
        _ = activationData; // unused

        theme.ApplyTheme();
        return Task.CompletedTask;
    };

    private readonly Func<Task> beforeActivation = () => Task.CompletedTask;
    private readonly Subject<object> subject = new();

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
