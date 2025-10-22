// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     The primary contract for strongly-typed settings service used at runtime.
///     This is the single canonical service contract consumers should use.
///     The service implementation must implement the TSettings interface for direct property access.
/// </summary>
/// <typeparam name="TSettings">The strongly-typed settings interface.</typeparam>
/// <remarks>
///     All concrete services must derive from <see cref="ISettingsService{TSettings}"/> or have methods that
///     are equivalent to all its public members.
/// </remarks>
public interface ISettingsService<TSettings> : ISettingsService
    where TSettings : class
{
    /// <summary>
    ///     Gets the settings instance that implements the TSettings interface.
    ///     This provides typed access to all settings properties.
    /// </summary>
    /// <remarks>
    ///     The Settings property provides direct access to settings properties:
    ///     <code><![CDATA[
    ///     ISettingsService&lt;IEditorSettings&gt; service = ...;
    ///     var fontSize = service.Settings.FontSize;
    ///     service.Settings.FontSize = 14;
    ///     ]]></code>
    /// </remarks>
    public TSettings Settings { get; }
}
