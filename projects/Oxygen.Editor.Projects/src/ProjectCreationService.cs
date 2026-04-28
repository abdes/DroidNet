// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using Oxygen.Core;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Utils;
using Oxygen.Storage;

namespace Oxygen.Editor.Projects;

/// <summary>
///     Storage-backed project creation service.
/// </summary>
/// <param name="storage">The storage provider.</param>
/// <param name="validation">The project validation service.</param>
public sealed class ProjectCreationService(
    IStorageProvider storage,
    IProjectValidationService validation) : IProjectCreationService
{
    private const string TemplateManifestFileName = "Template.json";
    private static readonly string[] RequiredTemplateFolders =
    [
        "Content",
        "Content/Scenes",
        "Content/Materials",
        "Content/Geometry",
        "Content/Textures",
        "Content/Audio",
        "Content/Video",
        "Content/Scripts",
        "Content/Prefabs",
        "Content/Animations",
        "Content/SourceMedia",
        "Content/SourceMedia/Images",
        "Content/SourceMedia/Audio",
        "Content/SourceMedia/Video",
        "Content/SourceMedia/DCC",
        "Config",
    ];

    /// <inheritdoc/>
    public async Task<bool> CanCreateProjectAsync(
        string projectName,
        string parentLocation,
        CancellationToken cancellationToken = default)
    {
        if (!IsValidProjectFolderName(projectName))
        {
            return false;
        }

        var parentRoot = storage.Normalize(parentLocation);
        if (!await storage.FolderExistsAsync(parentRoot).ConfigureAwait(false))
        {
            return false;
        }

        var projectRoot = storage.NormalizeRelativeTo(parentRoot, projectName);
        var projectFolder = await storage.GetFolderFromPathAsync(projectRoot, cancellationToken)
            .ConfigureAwait(false);
        return !await projectFolder.ExistsAsync().ConfigureAwait(false)
            || !await projectFolder.HasItemsAsync().ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task<ProjectCreationResult> CreateFromTemplateAsync(
        ProjectCreationRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);

        var projectRoot = string.Empty;
        var rootCreated = false;
        var createdItems = new List<IStorageItem>();
        try
        {
            var parentRoot = storage.Normalize(request.ParentLocation);
            projectRoot = storage.NormalizeRelativeTo(parentRoot, request.ProjectName);
            if (!await this.CanCreateProjectAsync(request.ProjectName, parentRoot, cancellationToken)
                    .ConfigureAwait(false))
            {
                return ProjectCreationResult.Failure(
                    projectRoot,
                    "Project destination is not available.");
            }

            var templateFolder = await storage.GetFolderFromPathAsync(request.TemplateRoot, cancellationToken)
                .ConfigureAwait(false);
            if (!await templateFolder.ExistsAsync().ConfigureAwait(false))
            {
                return ProjectCreationResult.Failure(projectRoot, "Project template folder does not exist.");
            }

            var templateDescriptor = await this.LoadTemplateDescriptorAsync(templateFolder, cancellationToken)
                .ConfigureAwait(false);
            var sourceFolder = await this.GetTemplateSourceFolderAsync(templateFolder, templateDescriptor, cancellationToken)
                .ConfigureAwait(false);
            await this.ValidateTemplateDescriptorAsync(templateFolder, sourceFolder, templateDescriptor, cancellationToken)
                .ConfigureAwait(false);
            if (await this.ContainsProjectManifestAsync(sourceFolder, cancellationToken).ConfigureAwait(false))
            {
                return ProjectCreationResult.Failure(
                    projectRoot,
                    $"Project template source folder must not contain '{Constants.ProjectFileName}'.");
            }

            var missingFolder = await this.FindMissingRequiredTemplateFolderAsync(sourceFolder, cancellationToken)
                .ConfigureAwait(false);
            if (missingFolder is not null)
            {
                return ProjectCreationResult.Failure(
                    projectRoot,
                    $"Project template is missing required folder '{missingFolder}'.");
            }

            var projectFolder = await storage.GetFolderFromPathAsync(projectRoot, cancellationToken)
                .ConfigureAwait(false);
            rootCreated = !await projectFolder.ExistsAsync().ConfigureAwait(false);
            await projectFolder.CreateAsync(cancellationToken).ConfigureAwait(false);

            await this.CopyTemplatePayloadAsync(sourceFolder, projectFolder, createdItems, cancellationToken)
                .ConfigureAwait(false);
            await this.EnsureRequiredFolderSkeletonAsync(projectFolder, createdItems, cancellationToken)
                .ConfigureAwait(false);
            await this.WriteProjectManifestAsync(projectFolder, request, templateDescriptor, cancellationToken)
                .ConfigureAwait(false);

            var validationResult = await validation.ValidateAsync(projectRoot, cancellationToken).ConfigureAwait(false);
            if (!validationResult.IsValid || validationResult.ProjectInfo is null)
            {
                await CleanupCreatedItemsAsync(projectFolder, rootCreated, createdItems, cancellationToken)
                    .ConfigureAwait(false);
                return ProjectCreationResult.Failure(
                    projectRoot,
                    validationResult.Message ?? "Created project did not pass validation.",
                    validationResult);
            }

            return ProjectCreationResult.Success(
                projectRoot,
                validationResult.ProjectInfo,
                validationResult,
                templateDescriptor.StarterScene?.OpenOnCreate == true,
                templateDescriptor.StarterScene?.AssetUri);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            if (!string.IsNullOrEmpty(projectRoot))
            {
                var projectFolder = await storage.GetFolderFromPathAsync(projectRoot, CancellationToken.None)
                    .ConfigureAwait(false);
                await CleanupCreatedItemsAsync(projectFolder, rootCreated, createdItems, CancellationToken.None)
                    .ConfigureAwait(false);
            }

            return ProjectCreationResult.Failure(projectRoot, ex.Message, exception: ex);
        }
    }

    private static bool IsValidProjectFolderName(string projectName)
        => !string.IsNullOrWhiteSpace(projectName)
            && projectName.IndexOfAny(Path.GetInvalidFileNameChars()) < 0
            && !projectName.Contains(Path.DirectorySeparatorChar, StringComparison.Ordinal)
            && !projectName.Contains(Path.AltDirectorySeparatorChar, StringComparison.Ordinal);

    private static async Task CleanupCreatedItemsAsync(
        IFolder projectFolder,
        bool rootCreated,
        IReadOnlyList<IStorageItem> createdItems,
        CancellationToken cancellationToken)
    {
        if (rootCreated)
        {
            await projectFolder.DeleteRecursiveAsync(cancellationToken).ConfigureAwait(false);
            return;
        }

        foreach (var item in createdItems.OrderByDescending(item => item.Location.Length))
        {
            switch (item)
            {
                case IDocument document:
                    await document.DeleteAsync(cancellationToken).ConfigureAwait(false);
                    break;
                case IFolder folder:
                    await folder.DeleteRecursiveAsync(cancellationToken).ConfigureAwait(false);
                    break;
            }
        }
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helper keeps the recursive template-copy workflow grouped with this service.")]
    private async Task CopyTemplatePayloadAsync(
        IFolder templateFolder,
        IFolder projectFolder,
        ICollection<IStorageItem> createdItems,
        CancellationToken cancellationToken)
    {
        await foreach (var folder in templateFolder.GetFoldersAsync(cancellationToken).ConfigureAwait(false))
        {
            var destination = await projectFolder.GetFolderAsync(folder.Name, cancellationToken).ConfigureAwait(false);
            await destination.CreateAsync(cancellationToken).ConfigureAwait(false);
            createdItems.Add(destination);
            await this.CopyTemplatePayloadAsync(folder, destination, createdItems, cancellationToken)
                .ConfigureAwait(false);
        }

        await foreach (var document in templateFolder.GetDocumentsAsync(cancellationToken).ConfigureAwait(false))
        {
            if (string.Equals(document.Name, TemplateManifestFileName, StringComparison.Ordinal))
            {
                continue;
            }

            if (string.Equals(document.Name, Constants.ProjectFileName, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            var copied = await document.CopyOverwriteAsync(projectFolder, cancellationToken).ConfigureAwait(false);
            createdItems.Add(copied);
        }
    }

    private async Task WriteProjectManifestAsync(
        IFolder projectFolder,
        ProjectCreationRequest request,
        TemplateDescriptor templateDescriptor,
        CancellationToken cancellationToken)
    {
        var projectInfo = new ProjectInfo(
            Guid.NewGuid(),
            request.ProjectName,
            templateDescriptor.Category ?? request.Category,
            projectFolder.Location,
            templateDescriptor.Thumbnail ?? request.Thumbnail);

        foreach (var mount in templateDescriptor.AuthoringMounts)
        {
            projectInfo.AuthoringMounts.Add(mount);
        }

        foreach (var mount in templateDescriptor.LocalFolderMounts)
        {
            projectInfo.LocalFolderMounts.Add(mount);
        }

        if (projectInfo.AuthoringMounts.Count == 0)
        {
            projectInfo.AuthoringMounts.Add(new ProjectMountPoint("Content", "Content"));
            var contentFolder = await projectFolder.GetFolderAsync("Content", cancellationToken).ConfigureAwait(false);
            await contentFolder.CreateAsync(cancellationToken).ConfigureAwait(false);
        }

        var manifest = await projectFolder.GetDocumentAsync(Constants.ProjectFileName, cancellationToken)
            .ConfigureAwait(false);
        await manifest.WriteAllTextAsync(ProjectInfo.ToJson(projectInfo), cancellationToken).ConfigureAwait(false);
    }

    private async Task ValidateTemplateDescriptorAsync(
        IFolder templateFolder,
        IFolder sourceFolder,
        TemplateDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        if (descriptor.SchemaVersion != 1)
        {
            throw new InvalidDataException("Project template SchemaVersion must be 1.");
        }

        RequireDescriptorText(descriptor.TemplateId, "TemplateId");
        RequireDescriptorText(descriptor.DisplayName, "DisplayName");
        RequireDescriptorText(descriptor.Description, "Description");
        RequireDescriptorText(descriptor.Icon, "Icon");
        RequireDescriptorText(descriptor.Preview, "Preview");
        RequireDescriptorText(descriptor.SourceFolder, "SourceFolder");

        if (descriptor.Category is null)
        {
            throw new InvalidDataException("Project template Category is required.");
        }

        await this.RequireTemplateFileExistsAsync(templateFolder, descriptor.Icon!, "Icon", cancellationToken)
            .ConfigureAwait(false);
        await this.RequireTemplateFileExistsAsync(templateFolder, descriptor.Preview!, "Preview", cancellationToken)
            .ConfigureAwait(false);
        foreach (var preview in descriptor.PreviewImages)
        {
            await this.RequireTemplateFileExistsAsync(templateFolder, preview, "PreviewImages", cancellationToken)
                .ConfigureAwait(false);
        }

        if (descriptor.AuthoringMounts.Count == 0)
        {
            throw new InvalidDataException("Project template AuthoringMounts is required.");
        }

        if (!descriptor.AuthoringMounts.Any(static mount =>
                string.Equals(mount.Name, Constants.ContentFolderName, StringComparison.OrdinalIgnoreCase)
                && string.Equals(mount.RelativePath, Constants.ContentFolderName, StringComparison.OrdinalIgnoreCase)))
        {
            throw new InvalidDataException("Project template AuthoringMounts must include Content -> Content.");
        }

        foreach (var mount in descriptor.AuthoringMounts)
        {
            ValidateMount(mount);
        }

        if (descriptor.StarterScene is null)
        {
            throw new InvalidDataException("Project template StarterScene is required.");
        }

        var starterSceneRelativePath = this.ResolveTemplateAssetRelativePath(
            descriptor,
            descriptor.StarterScene.AssetUri,
            descriptor.StarterScene.RelativePath,
            "StarterScene");
        await this.RequireSourceFileExistsAsync(sourceFolder, starterSceneRelativePath, "StarterScene", cancellationToken)
            .ConfigureAwait(false);

        foreach (var starterContent in descriptor.StarterContent)
        {
            RequireDescriptorText(starterContent.Kind, "StarterContent.Kind");
            var relativePath = this.ResolveTemplateAssetRelativePath(
                descriptor,
                starterContent.AssetUri,
                starterContent.RelativePath,
                "StarterContent");
            await this.RequireSourceFileExistsAsync(sourceFolder, relativePath, "StarterContent", cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async Task RequireTemplateFileExistsAsync(
        IFolder templateFolder,
        string relativePath,
        string fieldName,
        CancellationToken cancellationToken)
    {
        ValidateRelativeDescriptorPath(relativePath, fieldName);

        var document = await storage.GetDocumentFromPathAsync(
                storage.NormalizeRelativeTo(templateFolder.Location, NormalizeTemplatePath(relativePath)),
                cancellationToken)
            .ConfigureAwait(false);
        if (!await document.ExistsAsync().ConfigureAwait(false))
        {
            throw new InvalidDataException($"Project template {fieldName} file '{relativePath}' does not exist.");
        }
    }

    private async Task RequireSourceFileExistsAsync(
        IFolder sourceFolder,
        string relativePath,
        string fieldName,
        CancellationToken cancellationToken)
    {
        ValidateRelativeDescriptorPath(relativePath, fieldName);

        var document = await storage.GetDocumentFromPathAsync(
                storage.NormalizeRelativeTo(sourceFolder.Location, NormalizeTemplatePath(relativePath)),
                cancellationToken)
            .ConfigureAwait(false);
        if (!await document.ExistsAsync().ConfigureAwait(false))
        {
            throw new InvalidDataException($"Project template {fieldName} file '{relativePath}' does not exist.");
        }
    }

    private string ResolveTemplateAssetRelativePath(
        TemplateDescriptor descriptor,
        Uri assetUri,
        string? declaredRelativePath,
        string fieldName)
    {
        if (!string.Equals(assetUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException($"Project template {fieldName} URI must use the asset scheme.");
        }

        var assetPath = Uri.UnescapeDataString(assetUri.AbsolutePath).TrimStart('/');
        var slash = assetPath.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            throw new InvalidDataException($"Project template {fieldName} URI must include an authoring mount.");
        }

        var mountName = assetPath[..slash];
        var mountRelativePath = NormalizeTemplatePath(assetPath[(slash + 1)..]);
        var mount = descriptor.AuthoringMounts.FirstOrDefault(mount =>
            string.Equals(mount.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            throw new InvalidDataException($"Project template {fieldName} URI targets unknown authoring mount '{mountName}'.");
        }

        var expectedRelativePath = NormalizeTemplatePath(Path.Combine(mount.RelativePath, mountRelativePath));
        if (!string.IsNullOrWhiteSpace(declaredRelativePath))
        {
            var normalizedDeclaredPath = NormalizeTemplatePath(declaredRelativePath);
            ValidateRelativeDescriptorPath(normalizedDeclaredPath, $"{fieldName}.RelativePath");
            if (!string.Equals(normalizedDeclaredPath, expectedRelativePath, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"Project template {fieldName}.RelativePath does not match its AssetUri.");
            }
        }

        return expectedRelativePath;
    }

    [SuppressMessage(
        "Performance",
        "CA1822:Mark members as static",
        Justification = "Instance helper keeps template-manifest loading grouped with this service.")]
    private async Task<TemplateDescriptor> LoadTemplateDescriptorAsync(
        IFolder templateFolder,
        CancellationToken cancellationToken)
    {
        var manifest = await templateFolder.GetDocumentAsync(TemplateManifestFileName, cancellationToken)
            .ConfigureAwait(false);
        if (!await manifest.ExistsAsync().ConfigureAwait(false))
        {
            throw new InvalidDataException($"Project template is missing required '{TemplateManifestFileName}'.");
        }

        var json = await manifest.ReadAllTextAsync(cancellationToken).ConfigureAwait(false);
        return TemplateDescriptor.FromJson(json);
    }

    private async Task<IFolder> GetTemplateSourceFolderAsync(
        IFolder templateFolder,
        TemplateDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        var sourceFolderPath = string.IsNullOrWhiteSpace(descriptor.SourceFolder)
            ? "."
            : descriptor.SourceFolder;
        if (Path.IsPathRooted(sourceFolderPath))
        {
            throw new InvalidDataException("Project template SourceFolder must be relative to the template folder.");
        }

        var sourceFolder = sourceFolderPath is "." or "./"
            ? templateFolder
            : await storage.GetFolderFromPathAsync(storage.NormalizeRelativeTo(templateFolder.Location, sourceFolderPath), cancellationToken)
                .ConfigureAwait(false);
        if (!await sourceFolder.ExistsAsync().ConfigureAwait(false))
        {
            throw new InvalidDataException($"Project template SourceFolder '{sourceFolderPath}' does not exist.");
        }

        return sourceFolder;
    }

    private static void RequireDescriptorText(string? value, string fieldName)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidDataException($"Project template {fieldName} is required.");
        }
    }

    private static void ValidateMount(ProjectMountPoint mount)
    {
        RequireDescriptorText(mount.Name, "AuthoringMounts.Name");
        RequireDescriptorText(mount.RelativePath, "AuthoringMounts.RelativePath");
        ValidateRelativeDescriptorPath(mount.RelativePath, "AuthoringMounts.RelativePath");
    }

    private static void ValidateRelativeDescriptorPath(string relativePath, string fieldName)
    {
        if (string.IsNullOrWhiteSpace(relativePath))
        {
            throw new InvalidDataException($"Project template {fieldName} is required.");
        }

        if (Path.IsPathRooted(relativePath))
        {
            throw new InvalidDataException($"Project template {fieldName} must be relative.");
        }

        var segments = NormalizeTemplatePath(relativePath)
            .Split('/', StringSplitOptions.RemoveEmptyEntries);
        if (segments.Any(static segment => segment is "." or ".."))
        {
            throw new InvalidDataException($"Project template {fieldName} must not contain '.' or '..' segments.");
        }
    }

    private static string NormalizeTemplatePath(string path)
        => path.Replace('\\', '/').Trim().Trim('/');

    private async Task<string?> FindMissingRequiredTemplateFolderAsync(IFolder sourceFolder, CancellationToken cancellationToken)
    {
        foreach (var relativePath in RequiredTemplateFolders)
        {
            var folder = await GetNestedFolderAsync(sourceFolder, relativePath, cancellationToken).ConfigureAwait(false);
            if (!await folder.ExistsAsync().ConfigureAwait(false))
            {
                return relativePath;
            }
        }

        return null;
    }

    private async Task EnsureRequiredFolderSkeletonAsync(
        IFolder projectFolder,
        ICollection<IStorageItem> createdItems,
        CancellationToken cancellationToken)
    {
        foreach (var relativePath in RequiredTemplateFolders)
        {
            _ = await EnsureNestedFolderAsync(projectFolder, relativePath, createdItems, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async Task<bool> ContainsProjectManifestAsync(IFolder folder, CancellationToken cancellationToken)
    {
        await foreach (var document in folder.GetDocumentsAsync(cancellationToken).ConfigureAwait(false))
        {
            if (string.Equals(document.Name, Constants.ProjectFileName, StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }
        }

        await foreach (var child in folder.GetFoldersAsync(cancellationToken).ConfigureAwait(false))
        {
            if (await this.ContainsProjectManifestAsync(child, cancellationToken).ConfigureAwait(false))
            {
                return true;
            }
        }

        return false;
    }

    private static async Task<IFolder> GetNestedFolderAsync(
        IFolder root,
        string relativePath,
        CancellationToken cancellationToken)
    {
        var current = root;
        foreach (var segment in relativePath.Split(['/', '\\'], StringSplitOptions.RemoveEmptyEntries))
        {
            current = await current.GetFolderAsync(segment, cancellationToken).ConfigureAwait(false);
        }

        return current;
    }

    private static async Task<IFolder> EnsureNestedFolderAsync(
        IFolder root,
        string relativePath,
        ICollection<IStorageItem> createdItems,
        CancellationToken cancellationToken)
    {
        var current = root;
        foreach (var segment in relativePath.Split(['/', '\\'], StringSplitOptions.RemoveEmptyEntries))
        {
            var next = await current.GetFolderAsync(segment, cancellationToken).ConfigureAwait(false);
            if (!await next.ExistsAsync().ConfigureAwait(false))
            {
                await next.CreateAsync(cancellationToken).ConfigureAwait(false);
                createdItems.Add(next);
            }

            current = next;
        }

        return current;
    }

    private sealed record TemplateDescriptor(
        int SchemaVersion,
        string? TemplateId,
        Category? Category,
        string? DisplayName,
        string? Description,
        string? Icon,
        string? Preview,
        string? Thumbnail,
        string? SourceFolder,
        IReadOnlyList<ProjectMountPoint> AuthoringMounts,
        IReadOnlyList<LocalFolderMount> LocalFolderMounts,
        TemplateStarterScene? StarterScene,
        IReadOnlyList<TemplateStarterContent> StarterContent,
        IReadOnlyList<string> PreviewImages)
    {
        private static readonly JsonSerializerOptions JsonOptions = new()
        {
            AllowTrailingCommas = true,
            PropertyNameCaseInsensitive = false,
            Converters = { new CategoryJsonConverter() },
        };

        public static TemplateDescriptor FromJson(string json)
        {
            var descriptor = JsonSerializer.Deserialize<TemplateDescriptorData>(json, JsonOptions)
                ?? new TemplateDescriptorData();

            var thumbnail = descriptor.Thumbnail
                ?? descriptor.Preview
                ?? descriptor.PreviewImages?.FirstOrDefault()
                ?? descriptor.Icon;

            return new TemplateDescriptor(
                descriptor.SchemaVersion,
                descriptor.TemplateId,
                descriptor.Category,
                descriptor.DisplayName,
                descriptor.Description,
                descriptor.Icon,
                descriptor.Preview,
                thumbnail,
                descriptor.SourceFolder,
                descriptor.AuthoringMounts ?? [],
                descriptor.LocalFolderMounts ?? [],
                descriptor.StarterScene,
                descriptor.StarterContent ?? [],
                descriptor.PreviewImages ?? []);
        }
    }

    private sealed class TemplateDescriptorData
    {
        public int SchemaVersion { get; set; }

        public string? TemplateId { get; set; }

        public Category? Category { get; set; }

        public string? DisplayName { get; set; }

        public string? Description { get; set; }

        public string? Thumbnail { get; set; }

        public string? Preview { get; set; }

        public List<string>? PreviewImages { get; set; }

        public string? Icon { get; set; }

        public string? SourceFolder { get; set; }

        public List<ProjectMountPoint>? AuthoringMounts { get; set; }

        public List<LocalFolderMount>? LocalFolderMounts { get; set; }

        public TemplateStarterScene? StarterScene { get; set; }

        public List<TemplateStarterContent>? StarterContent { get; set; }
    }

    private sealed record TemplateStarterScene(Uri AssetUri, string? RelativePath, bool OpenOnCreate);

    private sealed record TemplateStarterContent(Uri AssetUri, string? RelativePath, string? Kind);
}
