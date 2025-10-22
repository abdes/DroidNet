// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     The primary contract for a strongly-typed settings service used at runtime.
///     <para>
///     This is the canonical service contract consumers should use to access and mutate settings.
///     Implementations MUST also implement the <typeparamref name="TSettingsInterface"/> interface so the service
///     instance exposes typed properties directly for UI binding and programmatic access.
///     </para>
/// </summary>
/// <typeparam name="TSettingsInterface">
///     The strongly-typed settings interface that the service implements (for example, <c>IAppearanceSettings</c>).
///     The service instance returned by DI will implement this interface so consumers can read and write
///     strongly-typed settings properties directly via the service itself.
/// </typeparam>
/// <remarks>
///     Concrete services should be registered and consumed as <c>ISettingsService&lt;TSettingsInterface&gt;</c> so the
///     `SettingsManager` can initialize and coordinate their lifecycle (loading, validation, and persistence).
///     <para>
///     Consumers **DO NOT** need to access settings via the <see cref="Settings"/> property; instead, they can read and
///     write settings properties directly on the service instance since it implements <typeparamref name="TSettingsInterface"/>.
///    </para>
/// </remarks>
public interface ISettingsService<TSettingsInterface> : ISettingsService
    where TSettingsInterface : class
{
    /// <summary>
    ///     Gets the settings instance that implements the <typeparamref name="TSettingsInterface"/> interface.
    /// </summary>
    /// <note>
    ///     This property provides typed access to all settings properties implemented by the service, useful for
    ///     property enumeration and generic processing. Prefer to access settings directly via the service instance.
    /// </note>
    /// <remarks>
    ///     Changes made to the settings properties should normally participate in the service's change-tracking (for
    ///     example updating <see cref="ISettingsService.IsDirty"/>) and be persisted via
    ///     <see cref="ISettingsService.SaveAsync(CancellationToken)"/> or the manager's auto-save.
    /// </remarks>
    public TSettingsInterface Settings { get; }
}
