// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>
/// Where a mount, a folder or an asset is in the virtual file system. An asset's path is its identity: the engine hashes
/// it into the asset key (<c>/Content/Materials/Red.omat</c>), and the editor stores it as an asset URI
/// (<c>asset:///Content/Materials/Red.omat</c>).
/// </summary>
/// <remarks>
/// This type owns every rule of the identity: equality and order are ordinal, as the engine's are; the URI form escapes
/// each segment on the way out and unescapes it on the way in; a source's identity is its path without the authoring
/// <see cref="AuthoringSuffix"/>. The mount and every segment pass the cooked rules of <see cref="VirtualPath"/>.
/// <see cref="CaseFolded"/> is the one comparison that folds case, to refuse case-only siblings when a name is
/// authored.
/// </remarks>
[JsonConverter(typeof(AssetPathJsonConverter))]
public sealed class AssetPath : IEquatable<AssetPath>, IComparable<AssetPath>
{
    /// <summary>The scheme of an asset URI.</summary>
    public const string UriScheme = "asset";

    /// <summary>The suffix an authored source adds to the identity of the asset it cooks into.</summary>
    public const string AuthoringSuffix = ".json";

    private const string UriPrefix = UriScheme + "://";

    private AssetPath(string virtualPath, int mountLength)
    {
        this.VirtualPath = virtualPath;
        this.MountLength = mountLength;
    }

    /// <summary>Gets the mount of engine-provided assets, <c>/Engine</c>.</summary>
    public static AssetPath Engine { get; } = Create("Engine");

    /// <summary>Gets the mount of the project's content, <c>/Content</c>.</summary>
    public static AssetPath Content { get; } = Create("Content");

    /// <summary>Gets the comparison that treats paths differing only in case as equal, to refuse case-only siblings.</summary>
    public static IEqualityComparer<AssetPath> CaseFolded { get; } = new CaseFoldedComparer();

    /// <summary>Gets the virtual path, unescaped, such as <c>/Content/Materials/Red.omat</c>.</summary>
    public string VirtualPath { get; }

    /// <summary>Gets the mount name, without its leading <c>/</c>.</summary>
    public string Mount => this.VirtualPath[1..this.MountLength];

    /// <summary>Gets the path below the mount, such as <c>Materials/Red.omat</c>; empty for a mount.</summary>
    public string RelativePath => this.IsMount ? string.Empty : this.VirtualPath[(this.MountLength + 1)..];

    /// <summary>Gets a value indicating whether this path names a mount.</summary>
    public bool IsMount => this.MountLength == this.VirtualPath.Length;

    /// <summary>Gets the last segment: the asset's file name, the folder's name, or the mount name.</summary>
    public string Name => this.VirtualPath[(this.VirtualPath.LastIndexOf('/') + 1)..];

    /// <summary>Gets the mount this path is in.</summary>
    public AssetPath MountRoot => this.IsMount ? this : new(this.VirtualPath[..this.MountLength], this.MountLength);

    /// <summary>Gets the folder that holds this path, or <see langword="null"/> for a mount.</summary>
    public AssetPath? Parent => this.IsMount ? null : new(this.VirtualPath[..this.VirtualPath.LastIndexOf('/')], this.MountLength);

    /// <summary>Gets a value indicating whether this path ends with the <see cref="AuthoringSuffix"/> of a source.</summary>
    public bool HasAuthoringSuffix => this.Name.EndsWith(AuthoringSuffix, StringComparison.Ordinal)
        && this.Name[..^AuthoringSuffix.Length].LastIndexOf('.') > 0;

    private int MountLength { get; }

    /// <summary>Tests whether two paths are the same, ordinally.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> when both are null or both name the same path.</returns>
    public static bool operator ==(AssetPath? left, AssetPath? right) => left?.Equals(right) ?? (right is null);

    /// <summary>Tests whether two paths differ, ordinally.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> when the paths differ.</returns>
    public static bool operator !=(AssetPath? left, AssetPath? right) => !(left == right);

    /// <summary>Tests whether one path sorts before another, ordinally.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> when <paramref name="left"/> sorts first.</returns>
    public static bool operator <(AssetPath? left, AssetPath? right) => Compare(left, right) < 0;

    /// <summary>Tests whether one path sorts after another, ordinally.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> when <paramref name="left"/> sorts last.</returns>
    public static bool operator >(AssetPath? left, AssetPath? right) => Compare(left, right) > 0;

    /// <summary>Tests whether one path sorts before or with another, ordinally.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> unless <paramref name="left"/> sorts last.</returns>
    public static bool operator <=(AssetPath? left, AssetPath? right) => Compare(left, right) <= 0;

    /// <summary>Tests whether one path sorts after or with another, ordinally.</summary>
    /// <param name="left">The first path.</param>
    /// <param name="right">The second path.</param>
    /// <returns><see langword="true"/> unless <paramref name="left"/> sorts first.</returns>
    public static bool operator >=(AssetPath? left, AssetPath? right) => Compare(left, right) >= 0;

    /// <summary>Creates the path of a mount, or of a path below it.</summary>
    /// <param name="mount">The mount name, without its leading <c>/</c>.</param>
    /// <param name="relativePath">The path below the mount, its segments separated by <c>/</c> or <c>\</c>.</param>
    /// <returns>The path.</returns>
    /// <exception cref="ArgumentException">The mount name or a segment is refused; the message holds the code.</exception>
    public static AssetPath Create(string mount, string? relativePath = null)
    {
        ArgumentNullException.ThrowIfNull(mount);
        return TryCreate(mount, SplitRelative(relativePath), out var path, out var error)
            ? path
            : throw new ArgumentException($"The path is refused ({error.Value.ToCode()}).", nameof(relativePath));
    }

    /// <summary>Creates the path of a mount, or of a path below it, when the rules accept it.</summary>
    /// <param name="mount">The mount name, without its leading <c>/</c>.</param>
    /// <param name="relativePath">The path below the mount, its segments separated by <c>/</c> or <c>\</c>.</param>
    /// <param name="path">The path, when the rules accept the mount name and every segment.</param>
    /// <returns><see langword="true"/> when the rules accept the path.</returns>
    public static bool TryCreate(string mount, string? relativePath, [NotNullWhen(true)] out AssetPath? path)
    {
        ArgumentNullException.ThrowIfNull(mount);
        return TryCreate(mount, SplitRelative(relativePath), out path, out _);
    }

    /// <summary>Reads a virtual path.</summary>
    /// <param name="virtualPath">The virtual path, such as <c>/Content/Materials/Red.omat</c>.</param>
    /// <returns>The path.</returns>
    /// <exception cref="ArgumentException">The path is refused; the message holds the code.</exception>
    public static AssetPath Parse(string virtualPath)
    {
        ArgumentNullException.ThrowIfNull(virtualPath);
        return TryParse(virtualPath, out var path, out var error)
            ? path
            : throw new ArgumentException($"'{virtualPath}' is refused ({error.Value.ToCode()}).", nameof(virtualPath));
    }

    /// <summary>Reads a virtual path.</summary>
    /// <param name="virtualPath">The virtual path.</param>
    /// <param name="path">The path, when the rules accept it.</param>
    /// <returns><see langword="true"/> when <paramref name="virtualPath"/> is a virtual path the rules accept.</returns>
    public static bool TryParse([NotNullWhen(true)] string? virtualPath, [NotNullWhen(true)] out AssetPath? path)
        => TryParse(virtualPath, out path, out _);

    /// <summary>Reads an asset URI.</summary>
    /// <param name="uri">The URI, such as <c>asset:///Content/Materials/Red.omat</c>.</param>
    /// <returns>The path it names.</returns>
    /// <exception cref="ArgumentException">The text is not an asset URI the rules accept.</exception>
    [SuppressMessage("Design", "CA1054:URI-like parameters should not be strings", Justification = "This text is the asset identity's persisted form; AssetPath owns its escaping. System.Uri has no canonical text for it: ToString() unescapes spaces and non-ASCII but not reserved characters, and OriginalString keeps the caller's spelling.")]
    public static AssetPath ParseUri(string uri)
    {
        ArgumentNullException.ThrowIfNull(uri);
        return TryParseUri(uri, out var path)
            ? path
            : throw new ArgumentException($"'{uri}' is not an '{UriScheme}' URI naming a path the rules accept.", nameof(uri));
    }

    /// <summary>
    /// Reads an asset URI: the <see cref="UriScheme"/> scheme, no authority, query or fragment, and an escaped path
    /// whose unescaped segments the rules accept.
    /// </summary>
    /// <param name="uri">The URI.</param>
    /// <param name="path">The path it names, when it is an asset URI.</param>
    /// <returns><see langword="true"/> when <paramref name="uri"/> is an asset URI the rules accept.</returns>
    [SuppressMessage("Design", "CA1054:URI-like parameters should not be strings", Justification = "This text is the asset identity's persisted form; AssetPath owns its escaping. System.Uri has no canonical text for it: ToString() unescapes spaces and non-ASCII but not reserved characters, and OriginalString keeps the caller's spelling.")]
    public static bool TryParseUri([NotNullWhen(true)] string? uri, [NotNullWhen(true)] out AssetPath? path)
    {
        path = null;
        if (uri?.StartsWith(UriPrefix, StringComparison.OrdinalIgnoreCase) != true || uri.AsSpan(UriPrefix.Length).IndexOfAny('?', '#') >= 0)
        {
            return false;
        }

        var escaped = uri[UriPrefix.Length..];
        if (!escaped.StartsWith('/'))
        {
            return false;
        }

        var segments = escaped[1..].Split('/');
        for (var index = 0; index < segments.Length; index++)
        {
            segments[index] = Uri.UnescapeDataString(segments[index]);
        }

        return TryCreate(segments[0], segments.AsSpan(1), out path, out _);
    }

    /// <summary>Reads an asset URI.</summary>
    /// <param name="uri">The URI.</param>
    /// <param name="path">The path it names, when it is an asset URI.</param>
    /// <returns><see langword="true"/> when <paramref name="uri"/> is an asset URI the rules accept.</returns>
    public static bool TryFromUri([NotNullWhen(true)] Uri? uri, [NotNullWhen(true)] out AssetPath? path)
    {
        path = null;
        return uri is { IsAbsoluteUri: true } && TryParseUri(uri.OriginalString, out path);
    }

    /// <summary>Reads an asset URI.</summary>
    /// <param name="uri">The URI.</param>
    /// <returns>The path it names.</returns>
    /// <exception cref="ArgumentException"><paramref name="uri"/> is not an asset URI the rules accept.</exception>
    public static AssetPath FromUri(Uri uri)
    {
        ArgumentNullException.ThrowIfNull(uri);
        return TryFromUri(uri, out var path)
            ? path
            : throw new ArgumentException($"'{uri}' is not an '{UriScheme}' URI naming a path the rules accept.", nameof(uri));
    }

    /// <summary>Gets the path below this one.</summary>
    /// <param name="relativePath">The path below this one, its segments separated by <c>/</c>.</param>
    /// <returns>The path.</returns>
    /// <exception cref="ArgumentException">A segment is refused; the message holds the code.</exception>
    public AssetPath Combine(string relativePath)
    {
        ArgumentException.ThrowIfNullOrEmpty(relativePath);
        return Create(this.Mount, this.IsMount ? relativePath : this.RelativePath + "/" + relativePath);
    }

    /// <summary>Gets the path below this one, when the rules accept it.</summary>
    /// <param name="relativePath">The path below this one, its segments separated by <c>/</c> or <c>\</c>.</param>
    /// <param name="path">The path, when the rules accept every segment.</param>
    /// <returns><see langword="true"/> when the rules accept the path.</returns>
    public bool TryCombine(string relativePath, [NotNullWhen(true)] out AssetPath? path)
    {
        ArgumentNullException.ThrowIfNull(relativePath);
        path = null;
        return relativePath.Length > 0 && TryCreate(this.Mount, this.IsMount ? relativePath : this.RelativePath + "/" + relativePath, out path);
    }

    /// <summary>Gets the path of a sibling: this path with its last segment replaced.</summary>
    /// <param name="name">The new last segment.</param>
    /// <returns>The path.</returns>
    /// <exception cref="InvalidOperationException">This path names a mount.</exception>
    /// <exception cref="ArgumentException"><paramref name="name"/> is refused; the message holds the code.</exception>
    public AssetPath WithName(string name)
        => (this.Parent ?? throw new InvalidOperationException("A mount has no siblings.")).Combine(name);

    /// <summary>Tests whether this path is a folder or lies inside it.</summary>
    /// <param name="folder">The folder or mount.</param>
    /// <returns><see langword="true"/> when this path is <paramref name="folder"/> or one of its descendants.</returns>
    public bool IsSameOrInside(AssetPath folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        return this.VirtualPath.StartsWith(folder.VirtualPath, StringComparison.Ordinal)
            && (this.VirtualPath.Length == folder.VirtualPath.Length || this.VirtualPath[folder.VirtualPath.Length] == '/');
    }

    /// <summary>Gets the path below a folder this path lies inside.</summary>
    /// <param name="folder">The folder or mount.</param>
    /// <returns>The path below <paramref name="folder"/>; empty for the folder itself.</returns>
    /// <exception cref="ArgumentException">This path is not inside <paramref name="folder"/>.</exception>
    public string GetPathBelow(AssetPath folder)
        => !this.IsSameOrInside(folder)
            ? throw new ArgumentException($"'{this}' is not inside '{folder}'.", nameof(folder))
            : this.VirtualPath.Length == folder.VirtualPath.Length ? string.Empty : this.VirtualPath[(folder.VirtualPath.Length + 1)..];

    /// <summary>
    /// Gets the identity of the asset an authored source cooks into: the source's path without its
    /// <see cref="AuthoringSuffix"/>, so <c>/Content/M/Red.omat.json</c> is <c>/Content/M/Red.omat</c>.
    /// </summary>
    /// <returns>The identity; this path when it has no authoring suffix.</returns>
    public AssetPath WithoutAuthoringSuffix()
        => this.HasAuthoringSuffix ? new(this.VirtualPath[..^AuthoringSuffix.Length], this.MountLength) : this;

    /// <summary>Gets the path of the authored source of an asset: this identity with the <see cref="AuthoringSuffix"/>.</summary>
    /// <returns>The source's path.</returns>
    /// <exception cref="InvalidOperationException">This path names a mount.</exception>
    public AssetPath WithAuthoringSuffix()
        => this.IsMount ? throw new InvalidOperationException("A mount has no source.") : new(this.VirtualPath + AuthoringSuffix, this.MountLength);

    /// <summary>Gets the asset URI of this path, each segment escaped.</summary>
    /// <returns>The URI, such as <c>asset:///Content/Materials/Red%20Wood.omat</c>.</returns>
    [SuppressMessage("Design", "CA1055:URI-like return values should not be strings", Justification = "This text is the asset identity's persisted form; AssetPath owns its escaping. System.Uri has no canonical text for it: ToString() unescapes spaces and non-ASCII but not reserved characters, and OriginalString keeps the caller's spelling.")]
    public string ToUriString()
        => UriPrefix + string.Join('/', this.VirtualPath.Split('/').Select(Uri.EscapeDataString));

    /// <summary>Gets the asset URI of this path, each segment escaped.</summary>
    /// <returns>The URI.</returns>
    public Uri ToUri() => new(this.ToUriString(), UriKind.Absolute);

    /// <inheritdoc/>
    public bool Equals(AssetPath? other) => other is not null && string.Equals(this.VirtualPath, other.VirtualPath, StringComparison.Ordinal);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as AssetPath);

    /// <inheritdoc/>
    public override int GetHashCode() => StringComparer.Ordinal.GetHashCode(this.VirtualPath);

    /// <inheritdoc/>
    public int CompareTo(AssetPath? other) => other is null ? 1 : string.CompareOrdinal(this.VirtualPath, other.VirtualPath);

    /// <summary>Gets the virtual path.</summary>
    /// <returns>The virtual path, such as <c>/Content/Materials/Red.omat</c>.</returns>
    public override string ToString() => this.VirtualPath;

    private static int Compare(AssetPath? left, AssetPath? right)
        => left is null ? (right is null ? 0 : -1) : left.CompareTo(right);

    private static string[] SplitRelative(string? relativePath)
        => string.IsNullOrEmpty(relativePath) ? [] : relativePath.Replace('\\', '/').Split('/');

    private static bool TryParse(string? virtualPath, [NotNullWhen(true)] out AssetPath? path, [NotNullWhen(false)] out VirtualPathError? error)
    {
        path = null;
        if (string.IsNullOrEmpty(virtualPath))
        {
            error = VirtualPathError.Empty;
            return false;
        }

        if (virtualPath[0] != '/')
        {
            error = VirtualPathError.NotAbsolute;
            return false;
        }

        var segments = virtualPath[1..].Split('/');
        return TryCreate(segments[0], segments.AsSpan(1), out path, out error);
    }

    private static bool TryCreate(string mount, ReadOnlySpan<string> segments, [NotNullWhen(true)] out AssetPath? path, [NotNullWhen(false)] out VirtualPathError? error)
    {
        path = null;
        error = Core.VirtualPath.ValidateMountName(mount);
        foreach (var segment in segments)
        {
            error ??= Core.VirtualPath.ValidateSegment(segment, VirtualPathRuleSet.Cooked);
        }

        if (error is not null)
        {
            return false;
        }

        path = new("/" + mount + (segments.IsEmpty ? string.Empty : "/" + string.Join('/', segments!)), mount.Length + 1);
        return true;
    }

    private sealed class CaseFoldedComparer : IEqualityComparer<AssetPath>
    {
        public bool Equals(AssetPath? x, AssetPath? y)
            => x is null || y is null ? ReferenceEquals(x, y) : VirtualPathComparer.CaseFolded.Equals(x.VirtualPath, y.VirtualPath);

        public int GetHashCode(AssetPath obj)
        {
            ArgumentNullException.ThrowIfNull(obj);
            return VirtualPathComparer.CaseFolded.GetHashCode(obj.VirtualPath);
        }
    }
}
