# Courtesy of Devanity https://github.com/Devaniti/GetDXC/blob/master/GetDXC.ps1
# License: The Unlicense (Public Domain)
#
# This script downloads the latest DXC release from the Microsoft DirectXShaderCompiler repository
# and extracts it to the specified folder.
#
# -- Usage --
# `GetDXC.bat PathToFolder`
# or
# `powershell -ExecutionPolicy Bypass -File GetDXC.ps1 PathToFolder`
#
# Where PathToFolder is folder where downloaded files will be placed

$SaveFolder = $args[0]
if ($args.count -lt 1)
{
	Write-Host "Using default save folder parameter"
    $SaveFolder = "packages/DXC"
}

New-Item -ItemType Directory -Force -Path $SaveFolder

$JSONURL = "https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest"
$JSON = Invoke-WebRequest -UseBasicParsing -Uri $JSONURL
$ParsedJSON = ConvertFrom-Json -InputObject $JSON
$Assets = Select-Object -InputObject $ParsedJSON -ExpandProperty assets
Foreach ($Asset IN $Assets)
{
	if ($Asset.name -match 'dxc.*.zip')
	{
		$DownloadURL = $Asset.browser_download_url
		$ZIPPath = Join-Path -Path $SaveFolder -ChildPath "dxc.zip"
		Invoke-WebRequest -UseBasicParsing -Uri $DownloadURL -OutFile $ZIPPath
		Expand-Archive -Path $ZIPPath -DestinationPath $SaveFolder -Force
		Remove-Item -Path $ZIPPath
		Write-Host "Sucessfully downloaded DXC $($Asset.name)"
		exit 0
	}
}

Write-Host "Something went wrong, couldn't download DXC"
exit 1

# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# For more information, please refer to <https://unlicense.org>
