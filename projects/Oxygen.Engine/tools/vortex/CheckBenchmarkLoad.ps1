<#
.SYNOPSIS
Record a short CPU/GPU background-load window before a benchmark starts.
.DESCRIPTION
Exit 0 means the sampled load is below the declared ceilings; 2 means busy.
Missing counters fail rather than being interpreted as an idle machine.
This does not terminate or reprioritize another application's work.
#>
param(
    [Parameter(Mandatory)][string]$OutputPath,
    [ValidateRange(3, 30)][int]$Samples = 5,
    [double]$MaxMeanCpu = 25,
    [double]$MaxPeakCpu = 50,
    [double]$MaxMeanGpu = 50,
    [double]$MaxPeakGpu = 70
)
$ErrorActionPreference = 'Stop'
$started = (Get-Date).ToUniversalTime().ToString('o')
$samplesOut = @()
for ($i = 0; $i -lt $Samples; ++$i) {
    Start-Sleep -Seconds 1
    $cpu = Get-CimInstance Win32_PerfFormattedData_PerfOS_Processor -Filter "Name='_Total'"
    if ($null -eq $cpu.PercentProcessorTime) { throw 'CPU utilization counter unavailable' }
    $gpuCsv = & nvidia-smi --query-gpu=index,name,utilization.gpu,utilization.memory,clocks.current.graphics,temperature.gpu,memory.used --format=csv,noheader,nounits
    if ($LASTEXITCODE -ne 0 -or !$gpuCsv) { throw 'GPU utilization counter unavailable' }
    $gpus = @($gpuCsv | ConvertFrom-Csv -Header index,name,utilization,memory_utilization,clock_mhz,temperature_c,memory_used_mib)
    foreach ($gpu in $gpus) { if ($gpu.utilization.Trim() -notmatch '^\d+$') { throw 'GPU utilization is not numeric' } }
    $samplesOut += @{ utc=(Get-Date).ToUniversalTime().ToString('o'); cpu_percent=[double]$cpu.PercentProcessorTime; gpus=$gpus }
}
$cpuStats = $samplesOut.cpu_percent | Measure-Object -Average -Maximum
$gpuValues = @($samplesOut | ForEach-Object { ($_.gpus | ForEach-Object { [double]$_.utilization }) | Measure-Object -Maximum | Select-Object -ExpandProperty Maximum })
$gpuStats = $gpuValues | Measure-Object -Average -Maximum
$accepted = $cpuStats.Average -le $MaxMeanCpu -and $cpuStats.Maximum -le $MaxPeakCpu -and $gpuStats.Average -le $MaxMeanGpu -and $gpuStats.Maximum -le $MaxPeakGpu
$cpuProcesses = @(Get-CimInstance Win32_PerfFormattedData_PerfProc_Process | Where-Object { $_.Name -notin @('_Total','Idle') -and $_.PercentProcessorTime -gt 0 } | Sort-Object PercentProcessorTime -Descending | Select-Object -First 8 Name,IDProcess,PercentProcessorTime)
$gpuOwners = @(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine | Where-Object UtilizationPercentage -gt 1 | Sort-Object UtilizationPercentage -Descending | Select-Object -First 8 Name,UtilizationPercentage)
$result = @{
    accepted=$accepted; started_utc=$started; finished_utc=(Get-Date).ToUniversalTime().ToString('o')
    thresholds=@{mean_cpu_percent=$MaxMeanCpu;peak_cpu_percent=$MaxPeakCpu;mean_gpu_percent=$MaxMeanGpu;peak_gpu_percent=$MaxPeakGpu}
    observed=@{mean_cpu_percent=$cpuStats.Average;peak_cpu_percent=$cpuStats.Maximum;mean_gpu_percent=$gpuStats.Average;peak_gpu_percent=$gpuStats.Maximum}
    samples=$samplesOut; cpu_process_counters=$cpuProcesses; gpu_engine_owners=$gpuOwners
    scope='Pre-launch headroom check, allowing ordinary desktop activity rather than requiring idle hardware. GPU values cover NVIDIA adapters. Process CPU counters may sum above 100 across cores. This does not prove absence of interference later in the run.'
}
$result | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Output ("CPU mean/peak {0:N1}/{1:N1}%; GPU mean/peak {2:N1}/{3:N1}%; accepted={4}" -f $cpuStats.Average,$cpuStats.Maximum,$gpuStats.Average,$gpuStats.Maximum,$accepted)
if (!$accepted) { exit 2 }
