// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;

namespace Oxygen.Editor.Services;

/// <summary>
/// Handle registration and chain invocation during activation.
/// </summary>
public sealed partial class ActivationService : IActivationService, IDisposable
{
    private readonly Func<object, Task> afterActivation = (activationData) =>
    {
        _ = activationData; // unused

        return Task.CompletedTask;
    };

    private readonly Func<Task> beforeActivation = () => Task.CompletedTask;
    private readonly Subject<object> subject = new();

    /// <inheritdoc/>
    public IDisposable Subscribe(IObserver<object> observer)
        => this.subject.Subscribe(observer);

    /// <inheritdoc/>
    public async Task ActivateAsync(object activationData)
    {
        await this.beforeActivation().ConfigureAwait(true);
        this.subject.OnNext(activationData);
        await this.afterActivation(activationData).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public void Dispose() => this.subject.Dispose();
}
