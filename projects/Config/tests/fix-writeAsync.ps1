# Fix WriteAsync calls to use new API

$file = "f:\projects\DroidNet\projects\Config\tests\Sources\JsonSettingsSourceTests.cs"
$content = Get-Content $file -Raw

# Pattern 1: await source.WriteAsync(nameof(TestSettings), settings, metadata);
# Convert to: await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = settings }, metadata);
$content = $content -replace 'await source\.WriteAsync\(nameof\(TestSettings\), ([^,]+), ([^)]+)\);', 'await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = $1 }, $2);'

# Pattern 2: var result = await source.WriteAsync(nameof(TestSettings), settings, metadata);
$content = $content -replace 'var result = await source\.WriteAsync\(nameof\(TestSettings\), ([^,]+), ([^)]+)\);', 'var result = await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = $1 }, $2);'

# Pattern 3: await source.WriteAsync("Section1", updatedSettings, metadata);
$content = $content -replace 'await source\.WriteAsync\("([^"]+)", ([^,]+), ([^)]+)\);', 'await source.WriteAsync(new Dictionary<string, object> { ["$1"] = $2 }, $3);'

# Pattern 4: WriteAsync with cancellation token
$content = $content -replace 'await source\.WriteAsync\(nameof\(TestSettings\), ([^,]+), ([^,]+), ([^)]+)\)', 'await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = $1 }, $2, $3)'

# Pattern 5: var writeResult = await source.WriteAsync(nameof(TestSettings), complexSettings, metadata);
$content = $content -replace 'var writeResult = await source\.WriteAsync\(nameof\(TestSettings\), ([^,]+), ([^)]+)\);', 'var writeResult = await source.WriteAsync(new Dictionary<string, object> { [nameof(TestSettings)] = $1 }, $2);'

$content | Set-Content $file
Write-Host "Fixed WriteAsync calls in JsonSettingsSourceTests.cs"
