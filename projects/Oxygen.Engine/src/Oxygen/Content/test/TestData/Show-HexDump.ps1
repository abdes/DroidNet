<#
.SYNOPSIS
    Displays a hexadecimal dump of a file's contents, similar to the `hexdump`
    utility.

.DESCRIPTION
    Reads a file as binary and outputs its contents in hexadecimal format, with
    an optional ASCII representation. Useful for inspecting binary files or
    debugging file contents.

    ---
    IMPORTANT: To view this help in PowerShell 7+ (pwsh), you must first
    dot-source this script to load the function into your session:

        . ./Show-HexDump.ps1

    Then run:

        Get-Help Show-HexDump -Full
    ---

.PARAMETER Path
    The path to the file to display as a hex dump. This parameter is required.

.PARAMETER BytesPerRow
    The number of bytes to display per row. Default is 16.

.PARAMETER ShowAscii
    If specified, displays the ASCII representation of each row alongside the
    hex output.

.EXAMPLE
    Show-HexDump -Path "file.bin"
    # Displays a hex dump of file.bin with default settings.

.EXAMPLE
    Show-HexDump -Path "file.bin" -ShowAscii
    # Displays a hex dump with ASCII representation.

.EXAMPLE
    Show-HexDump -Path "file.bin" -BytesPerRow 32
    # Displays a hex dump with 32 bytes per row.

.EXAMPLE
    Show-HexDump -Path "file.bin" -BytesPerRow 8 -ShowAscii
    # Displays a hex dump with 8 bytes per row and ASCII output.

.EXAMPLE
    Get-ChildItem *.bin | ForEach-Object { Show-HexDump -Path $_.FullName -ShowAscii }
    # Dumps all .bin files in the current directory with ASCII output.

.EXAMPLE
    Show-HexDump -Path $env:SystemRoot\System32\notepad.exe -BytesPerRow 32
    # Displays a hex dump of notepad.exe with 32 bytes per row.

.NOTES
    Reads the file in chunks for memory efficiency. Suitable for large files.
    Output format: offset, hex bytes, and (optionally) ASCII text.

.LINK
    https://docs.microsoft.com/en-us/powershell/
#>
function Show-HexDump {
    [CmdletBinding()]
    param (
        [Parameter(Mandatory)]
        [string]$Path,

        [int]$BytesPerRow = 16,

        [switch]$ShowAscii
    )

    $offset = 0
    Get-Content -Path $Path -AsByteStream -ReadCount $BytesPerRow | ForEach-Object {
        $hex = ($_ | ForEach-Object { '{0:X2}' -f $_ }) -join ' '
        if ($ShowAscii) {
            $ascii = ($_ | ForEach-Object {
                    if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { '.' }
                }) -join ''
            '{0,8:D}: {1,-48}  {2}' -f $offset, $hex, $ascii
        }
        else {
            '{0,8:D}: {1}' -f $offset, $hex
        }
        $offset += $_.Length
    }
}
