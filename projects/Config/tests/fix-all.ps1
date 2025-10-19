# Comprehensive fix for all remaining SettingsService instantiations

# Fix ValidationTests.cs
$file1 = "f:\projects\DroidNet\projects\Config\tests\Validation\ValidationTests.cs"
$content1 = Get-Content $file1 -Raw
$content1 = $content1 -creplace 'new SettingsService<TestSettings>', 'new TestSettingsService'
$content1 = $content1 -creplace 'new SettingsService<InvalidTestSettings>', 'new InvalidTestSettingsService'
$content1 | Set-Content $file1
Write-Host "Fixed ValidationTests.cs"

# Fix NewSettingsServiceTests.cs
$file2 = "f:\projects\DroidNet\projects\Config\tests\SettingsService\NewSettingsServiceTests.cs"
$content2 = Get-Content $file2 -Raw
$content2 = $content2 -creplace 'new SettingsService<ITestSettings>', 'new TestSettingsService'
$content2 = $content2 -creplace 'new SettingsService<InvalidITestSettings>', 'new InvalidTestSettingsService'
$content2 = $content2 -creplace 'InvalidITestSettings', 'IInvalidTestSettings'
$content2 | Set-Content $file2
Write-Host "Fixed NewSettingsServiceTests.cs"

# Fix JsonSettingsSourceTests.cs - Version issue
$file3 = "f:\projects\DroidNet\projects\Config\tests\Sources\JsonSettingsSourceTests.cs"
$content3 = Get-Content $file3 -Raw
$content3 = $content3 -replace 'new SettingsMetadata\(new Version\("1\.0"\), "20251019"\)', 'new SettingsMetadata("1.0", "20251019")'
# Fix the WriteAsync dictionary issue
$content3 = $content3 -creplace '\.Value', '.Value = 1 }, metadata);
            await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = new TestSettings { Name = "Second", Value = 2 }, metadata);
            await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = new TestSettings { Name = "Third", Value = 3 }'
$content3 | Set-Content $file3
Write-Host "Fixed JsonSettingsSourceTests.cs"
