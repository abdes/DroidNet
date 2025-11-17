// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Data;

/// <summary>
/// Indicates that a property should be persisted by the `EditorSettingsManager`.
/// </summary>
/// <remarks>
/// The <see cref="PersistedAttribute"/> is used to mark properties in a class that should be saved
/// and loaded by the `EditorSettingsManager`.
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <![CDATA[
/// public class MyModuleSettings : ModuleSettings
/// {
///     [Persisted]
///     public int MySetting { get; set; }
///
///     public MyModuleSettings(string moduleName)
///         : base(moduleName)
///     {
///     }
/// }
///
/// // Usage
/// var settingsManager = new EditorSettingsManager(context);
/// var mySettings = new MyModuleSettings("MyModule");
/// await mySettings.LoadAsync(settingsManager);
/// mySettings.MySetting = 42;
/// await mySettings.SaveAsync(settingsManager);
/// ]]>
/// </example>
[AttributeUsage(AttributeTargets.Property)]
public sealed class PersistedAttribute : Attribute;
