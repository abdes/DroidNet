// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing;

/// <summary>
/// Represents a routing target within the navigation system.
/// </summary>
/// <remarks>
/// <para>
/// The <see cref="Target"/> class serves as an abstraction for defining distinct navigation targets
/// within the routing infrastructure. Each target is identified by a unique name, facilitating
/// the association of specific routes and view models to different parts of the application.
/// </para>
/// <para>
/// In a windowed navigation system, each window represents a <see cref="Target"/>. This allows the
/// router to manage multiple windows independently, enabling dynamic UI updates such as opening,
/// closing, or modifying specific windows without affecting others. For instance, in a desktop
/// application with a main window and multiple auxiliary panels, each panel can be associated with
/// its own <see cref="Target"/>, allowing precise control over their visibility and content.
/// </para>
/// <para>
/// <strong>Special Targets</strong>
/// </para>
/// <para>
/// The <see cref="Target"/> class provides predefined instances for common navigation contexts. The
/// <see cref="Main"/> target refers to the main top-level window of the application, while the
/// <see cref="Self"/> target refers to the current active window or context.
/// </para>
/// <para>
/// It is recommended to use these predefined instances instead of literal strings to ensure
/// consistency and avoid errors.
/// </para>
/// <para>
/// <strong>Creating Custom Targets</strong>
/// </para>
/// <para>
/// While <see cref="Main"/> and <see cref="Self"/> are available for standard scenarios, developers
/// can define additional targets by instantiating new <see cref="Target"/> objects with unique names.
/// This flexibility allows for scalable and organized navigation structures, especially in complex
/// applications with multiple windows or panels.
/// </para>
/// <example>
/// <strong>Example Usage</strong>
/// <code><![CDATA[
/// // Using predefined special targets
/// var mainTarget = Target.Main;
/// var selfTarget = Target.Self;
///
/// // Creating a custom target for a settings window
/// var settingsTarget = new Target { Name = "settings" };
///
/// // Adding a new window to the routing system
/// var routeChange = new RouteChangeItem
/// {
///     ChangeAction = RouteChangeAction.Add,
///     Outlet = settingsTarget,
///     ViewModelType = typeof(SettingsViewModel),
///     Parameters = new Parameters { ["theme"] = "dark" }
/// };
///
/// // Navigating to add the settings window relative to the main window
/// router.Navigate(new List<RouteChangeItem> { routeChange }, new PartialNavigation
/// {
///     RelativeTo = Target.Main
/// });
///
/// // Checking target types
/// if (settingsTarget.IsSelf)
/// {
///     // Handle actions specific to the current window
/// }
/// else if (settingsTarget.IsMain)
/// {
///     // Handle actions specific to the main window
/// }
/// else
/// {
///     // Handle actions for custom targets like settings
/// }
/// ]]></code>
/// </example>
/// <para><strong>Guidelines for Implementors</strong></para>
/// <para>
/// When implementing navigation logic, always prefer using the predefined <see cref="Main"/> and
/// <see cref="Self"/> targets to leverage built-in behaviors and avoid discrepancies. For custom
/// targets, ensure that each target name is unique and descriptive to maintain clarity within the
/// routing system.
/// </para>
/// <para><strong>Corner Cases</strong></para>
/// <para>
/// <em>Immutable Names:</em> Since <see cref="Target"/> is a record with an immutable <see cref="Name"/>
/// property, once a target is created, its name cannot be changed. This ensures the stability of
/// navigation references throughout the application's lifecycle.
/// </para>
/// <para>
/// <em>Case Sensitivity:</em> Target names are case-sensitive. Ensure consistency in naming
/// conventions to prevent mismatches and unintended behaviors within the routing mechanism.
/// </para>
/// </remarks>
public record Target
{
    /// <summary>
    /// Gets the name identifying the routing target.
    /// </summary>
    /// <value>
    /// A <see cref="string"/> representing the unique name of the target.
    /// </value>
    public required string Name { get; init; }

    /// <summary>
    /// Gets the special target used to refer to the main top-level navigation context.
    /// </summary>
    /// <value>
    /// A predefined <see cref="Target"/> instance representing the main navigation target.
    /// </value>
    public static Target Main { get; } = new() { Name = "_main" };

    /// <summary>
    /// Gets the special target used to refer to the current active navigation context.
    /// </summary>
    /// <remarks>
    /// When a target is not specified for a navigation request, it is assumed
    /// to be <see cref="Self"/>.
    /// </remarks>
    /// <value>
    /// A predefined <see cref="Target"/> instance representing the current active navigation target.
    /// </value>
    public static Target Self { get; } = new() { Name = "_self" };

    /// <summary>
    /// Gets a value indicating whether determines whether this target refers to the <see cref="Self"/> target.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the target is <see cref="Self"/>; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsSelf => this.Name.Equals(Self.Name, StringComparison.Ordinal);

    /// <summary>
    /// Gets a value indicating whether determines whether this target refers to the <see cref="Main"/> target.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the target is <see cref="Main"/>; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsMain => this.Name.Equals(Main.Name, StringComparison.Ordinal);

    /// <summary>
    /// Implicitly converts a <see cref="string"/> to a <see cref="Target"/>.
    /// </summary>
    /// <param name="name">The target name as a string.</param>
    public static implicit operator Target(string? name) => FromString(name);

    /// <summary>
    /// Implicitly converts a <see cref="Target"/> to its <see cref="string"/> representation.
    /// </summary>
    /// <param name="source">The <see cref="Target"/> object to convert.</param>
    public static implicit operator string(Target source) => source.Name;

    /// <summary>
    /// Creates a new <see cref="Target"/> instance from a specified name.
    /// </summary>
    /// <param name="name">The target name as a string.</param>
    /// <returns>
    /// A <see cref="Target"/> instance with the specified name. If <paramref name="name"/> is
    /// <see langword="null"/>, returns the <see cref="Self"/> target.
    /// </returns>
    /// <remarks>
    /// This method ensures that null values default to the <see cref="Self"/> target,
    /// maintaining consistent navigation behavior.
    /// </remarks>
    public static Target FromString(string? name) => name is null ? Self : new Target { Name = name };

    /// <summary>
    /// Returns the string representation of the target.
    /// </summary>
    /// <returns>
    /// The target name as a <see cref="string"/>.
    /// </returns>
    public override string ToString() => this.Name;
}
