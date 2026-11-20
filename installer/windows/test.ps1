# Install tests 1-6 (installer/PLAN.md, Install tests in CI) for the Windows suite setup.
# Usage (elevated PowerShell 7, on a machine without a Myzel Audio install):
#   pwsh installer/windows/test.ps1 -Installer <setup.exe> -Stage <stage-dir>
#   $env:PLUGINVAL = <pluginval.exe> also validates the installed VST3s ($env:PLUGINVAL_STRICTNESS, default 10).
# Plugins come from installer/plugins.tsv: the first two are installed first, the third is added.
#Requires -Version 7
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$Stage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Vendor = 'Myzel Audio'
# Frozen AppId (installer/PLAN.md): the suite's Settings -> Apps entry.
$SuiteKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{0AFB3883-05BD-477F-B777-E539AC381E0E}_is1'
$AppsKey = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
$ReceiptKey = "HKLM:\SOFTWARE\$Vendor"
$CommonFiles = $env:CommonProgramW6432
$AppDir = Join-Path $env:ProgramW6432 $Vendor
$StartMenu = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) $Vendor
$UserData = Join-Path $env:APPDATA $Vendor
$Strictness = if ($env:PLUGINVAL_STRICTNESS) { $env:PLUGINVAL_STRICTNESS } else { '10' }
$Work = Join-Path ([IO.Path]::GetTempPath()) "myzel-installer-test-$PID"
$script:RunCount = 0

function Fail([string]$Message)
{
    throw $Message
}

function Check([bool]$Condition, [string]$Message)
{
    if (-not $Condition) { Fail $Message }
}

function Pass([string]$Message)
{
    Write-Host "PASS $Message"
}

function Read-Plugins
{
    $tsv = Join-Path $PSScriptRoot '..\plugins.tsv'
    $plugins = @()
    foreach ($line in Get-Content -LiteralPath $tsv -Encoding utf8)
    {
        if ($line -match '^\s*(#|$)') { continue }
        $fields = $line -split "`t"
        $id = $fields[0]
        $versionFile = Join-Path $Stage "$id\version"
        Check (Test-Path -LiteralPath $versionFile) "$id is in plugins.tsv but not staged in $Stage."
        $plugins += [pscustomobject]@{
            Id = $id
            Name = $fields[1]
            Version = (Get-Content -LiteralPath $versionFile -Raw).Trim()
        }
    }
    return , $plugins
}

function Vst3Dir($P) { Join-Path $CommonFiles "VST3\$($P.Name).vst3" }
function Vst3Binary($P) { Join-Path (Vst3Dir $P) "Contents\x86_64-win\$($P.Name).vst3" }
function ClapFile($P) { Join-Path $CommonFiles "CLAP\$($P.Name).clap" }
function StandaloneExe($P) { Join-Path $AppDir "$($P.Name).exe" }
function StartMenuLink($P) { Join-Path $StartMenu "$($P.Name).lnk" }
function Receipt($P) { "$ReceiptKey\$($P.Name)" }
function AppsEntry($P) { "$AppsKey\MyzelAudio_$($P.Id)" }

function Names([string]$Dir)
{
    if (Test-Path -LiteralPath $Dir) { return @(Get-ChildItem -LiteralPath $Dir -Name | Sort-Object) }
    return @()
}

function Show-LastLog
{
    $log = Join-Path $Work "setup-$($script:RunCount).log"
    if (Test-Path -LiteralPath $log)
    {
        Write-Host "--- last 40 lines of $log"
        Get-Content -LiteralPath $log -Tail 40 | ForEach-Object { Write-Host $_ }
    }
}

function Invoke-Exe([string]$Exe, [string]$Arguments, [int]$Expect = 0)
{
    Write-Host "> `"$Exe`" $Arguments"
    $process = Start-Process -FilePath $Exe -ArgumentList $Arguments -Wait -PassThru
    if ($process.ExitCode -ne $Expect)
    {
        Show-LastLog
        Fail "`"$Exe`" $Arguments exited with $($process.ExitCode), expected $Expect."
    }
}

function Invoke-Setup([string]$Arguments, [int]$Expect = 0)
{
    $script:RunCount++
    $log = Join-Path $Work "setup-$($script:RunCount).log"
    Invoke-Exe $Installer "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=`"$log`" $Arguments" $Expect
}

# The setup's /LIST=<file> table: id, name, status, installed version, formats, offered version, text.
function Get-Status
{
    $file = Join-Path $Work 'list.txt'
    Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
    Invoke-Setup "/LIST=`"$file`""
    Check (Test-Path -LiteralPath $file) '/LIST wrote no file.'
    $status = @{}
    foreach ($line in Get-Content -LiteralPath $file -Encoding utf8)
    {
        if ($line -match '^\s*(#|$)' -or $line.StartsWith([string][char]0xFEFF)) { continue }
        $f = $line -split "`t"
        $status[$f[0]] = [pscustomobject]@{ Status = $f[2]; Installed = $f[3]; Formats = $f[4]; Text = $f[6] }
    }
    return $status
}

function Expect-Status($Status, $P, [string]$Expected)
{
    Check ($Status.ContainsKey($P.Id)) "/LIST has no row for $($P.Id)."
    $row = $Status[$P.Id]
    Check ($row.Status -eq $Expected) "/LIST reports $($P.Id) as '$($row.Status)' ($($row.Text)), expected '$Expected'."
}

function Same-File([string]$A, [string]$B)
{
    return (Get-FileHash -LiteralPath $A).Hash -eq (Get-FileHash -LiteralPath $B).Hash
}

function Assert-Installed($P, [string[]]$Formats)
{
    $staged = Join-Path $Stage $P.Id
    if ($Formats -contains 'VST3')
    {
        Check (Test-Path -LiteralPath (Vst3Binary $P)) "$(Vst3Binary $P) is missing."
        Check (Same-File (Vst3Binary $P) (Join-Path $staged "VST3\$($P.Name).vst3\Contents\x86_64-win\$($P.Name).vst3")) "$(Vst3Binary $P) differs from the stage."
    }
    if ($Formats -contains 'CLAP')
    {
        Check (Test-Path -LiteralPath (ClapFile $P)) "$(ClapFile $P) is missing."
        Check (Same-File (ClapFile $P) (Join-Path $staged "CLAP\$($P.Name).clap")) "$(ClapFile $P) differs from the stage."
    }
    if ($Formats -contains 'Standalone')
    {
        Check (Test-Path -LiteralPath (StandaloneExe $P)) "$(StandaloneExe $P) is missing."
        Check (Test-Path -LiteralPath (StartMenuLink $P)) "$(StartMenuLink $P) is missing."
    }
    Check (Test-Path -LiteralPath (Receipt $P)) "Receipt $(Receipt $P) is missing."
    $receipt = Get-ItemProperty -LiteralPath (Receipt $P)
    Check ($receipt.Version -eq $P.Version) "Receipt of $($P.Name) says version $($receipt.Version), expected $($P.Version)."
    Check ($receipt.Id -eq $P.Id) "Receipt of $($P.Name) says id $($receipt.Id), expected $($P.Id)."
    foreach ($format in $Formats)
    {
        Check (($receipt.Formats -split ',') -contains $format) "Receipt of $($P.Name) lists formats '$($receipt.Formats)', missing $format."
    }
    Check (Test-Path -LiteralPath (AppsEntry $P)) "Settings -> Apps entry $(AppsEntry $P) is missing."
    $entry = Get-ItemProperty -LiteralPath (AppsEntry $P)
    Check ($entry.DisplayName -eq "$Vendor $($P.Name)") "Apps entry of $($P.Name) is named '$($entry.DisplayName)'."
    Check ($entry.DisplayVersion -eq $P.Version) "Apps entry of $($P.Name) has version '$($entry.DisplayVersion)'."
    Check ($entry.UninstallString -like "*\MyzelAudioSetup.exe`" /REMOVE=$($P.Id) *") "Apps entry of $($P.Name) runs '$($entry.UninstallString)'."
    Check ($entry.UninstallString -notlike '*/SUPPRESSMSGBOXES*') "Apps entry of $($P.Name) hides the message that a host blocks the removal: '$($entry.UninstallString)'."
    Check ($entry.QuietUninstallString -eq "$($entry.UninstallString) /SUPPRESSMSGBOXES") "Apps entry of $($P.Name) has QuietUninstallString '$($entry.QuietUninstallString)'."
}

function Assert-Absent($P)
{
    foreach ($path in @((Vst3Dir $P), (ClapFile $P), (StandaloneExe $P), (StartMenuLink $P)))
    {
        Check (-not (Test-Path -LiteralPath $path)) "$path is still there."
    }
    Check (-not (Test-Path -LiteralPath (Receipt $P))) "Receipt $(Receipt $P) is still there."
    Check (-not (Test-Path -LiteralPath (AppsEntry $P))) "Apps entry $(AppsEntry $P) is still there."
}

function Invoke-Pluginval([object[]]$Plugins)
{
    if (-not $env:PLUGINVAL)
    {
        Write-Host 'SKIP pluginval: $env:PLUGINVAL is not set.'
        return
    }
    foreach ($p in $Plugins)
    {
        $out = Join-Path $Work "pluginval-$($p.Id).txt"
        $arguments = "--strictness-level $Strictness --validate-in-process --timeout-ms 600000 --validate `"$(Vst3Dir $p)`""
        Write-Host "> pluginval $arguments"
        $process = Start-Process -FilePath $env:PLUGINVAL -ArgumentList $arguments -Wait -PassThru -NoNewWindow `
            -RedirectStandardOutput $out -RedirectStandardError "$out.err"
        Get-Content -LiteralPath $out -Tail 15 | ForEach-Object { Write-Host $_ }
        if ($process.ExitCode -ne 0)
        {
            Get-Content -LiteralPath $out, "$out.err" | ForEach-Object { Write-Host $_ }
            Fail "pluginval failed on the installed $(Vst3Dir $p) (exit $($process.ExitCode))."
        }
        Pass "pluginval $Strictness on the installed $($p.Name).vst3"
    }
}

function Wait-Gone([string]$Path, [int]$Seconds)
{
    for ($i = 0; $i -lt $Seconds -and (Test-Path -LiteralPath $Path); $i++) { Start-Sleep -Seconds 1 }
    return -not (Test-Path -LiteralPath $Path)
}

function Get-AppsEntries
{
    return @(Get-ChildItem -LiteralPath $AppsKey | Where-Object { $_.PSChildName -like 'MyzelAudio_*' } |
        ForEach-Object { $_.PSChildName } | Sort-Object)
}

function Get-Receipts
{
    if (-not (Test-Path -LiteralPath $ReceiptKey)) { return @() }
    return @(Get-ChildItem -LiteralPath $ReceiptKey | ForEach-Object { $_.PSChildName } | Sort-Object)
}

$userDataMarker = $null
$createdUserData = $false
$exitCode = 0
try
{
    Check ($IsWindows) 'Run the Windows install tests on Windows.'
    $principal = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    Check ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) 'Run the install tests elevated (the setup installs system-wide).'
    $Installer = (Resolve-Path -LiteralPath $Installer).Path
    $Stage = (Resolve-Path -LiteralPath $Stage).Path
    $plugins = Read-Plugins
    Check ($plugins.Count -ge 3) 'The install tests need at least three plugins in installer/plugins.tsv.'
    $a, $b, $c = $plugins[0], $plugins[1], $plugins[2]
    Check (-not (Test-Path -LiteralPath $SuiteKey)) "$Vendor is already installed here; the tests need a machine without it."
    foreach ($p in $plugins)
    {
        Check (-not (Test-Path -LiteralPath (Vst3Dir $p)) -and -not (Test-Path -LiteralPath (ClapFile $p))) "$($p.Name) is already on this machine; the tests need a machine without it."
    }
    New-Item -ItemType Directory -Path $Work -Force | Out-Null

    # User data the setup must never touch.
    $userDataDir = Join-Path $UserData $a.Name
    $createdUserData = -not (Test-Path -LiteralPath $UserData)
    New-Item -ItemType Directory -Path $userDataDir -Force | Out-Null
    $userDataMarker = Join-Path $userDataDir 'installer-test-keep.txt'
    Set-Content -LiteralPath $userDataMarker -Value 'keep' -Encoding utf8

    $vst3Before = @(Names (Join-Path $CommonFiles 'VST3'))
    $clapBefore = @(Names (Join-Path $CommonFiles 'CLAP'))

    # 1. Fresh install of two plugins, two formats.
    Invoke-Setup "/COMPONENTS=`"$($a.Id),$($b.Id)`" /TASKS=`"vst3,clap`""
    Assert-Installed $a @('VST3', 'CLAP')
    Assert-Installed $b @('VST3', 'CLAP')
    Assert-Absent $c
    foreach ($p in @($a, $b))
    {
        Check (-not (Test-Path -LiteralPath (StandaloneExe $p))) "$(StandaloneExe $p) was installed although Standalone was not ticked."
    }
    $vst3New = @(Names (Join-Path $CommonFiles 'VST3') | Where-Object { $vst3Before -notcontains $_ })
    Check ((($vst3New | Sort-Object) -join '|') -eq ((@("$($a.Name).vst3", "$($b.Name).vst3") | Sort-Object) -join '|')) "VST3 folder gained: $($vst3New -join ', ')."
    $clapNew = @(Names (Join-Path $CommonFiles 'CLAP') | Where-Object { $clapBefore -notcontains $_ })
    Check ((($clapNew | Sort-Object) -join '|') -eq ((@("$($a.Name).clap", "$($b.Name).clap") | Sort-Object) -join '|')) "CLAP folder gained: $($clapNew -join ', ')."
    $appFiles = @(Names $AppDir | Where-Object { $_ -notmatch '^unins\d{3}\.(exe|dat|msg)$' -and $_ -ne 'MyzelAudioSetup.exe' -and $_ -ne 'LICENSES.txt' })
    Check ($appFiles.Count -eq 0) "$AppDir holds unexpected files: $($appFiles -join ', ')."
    Check (Test-Path -LiteralPath (Join-Path $AppDir 'MyzelAudioSetup.exe')) 'The maintenance copy MyzelAudioSetup.exe is missing.'
    Check (Test-Path -LiteralPath (Join-Path $AppDir 'LICENSES.txt')) 'LICENSES.txt is missing.'
    Check (-not (Test-Path -LiteralPath $StartMenu)) "$StartMenu exists although Standalone was not ticked."
    Check (((Get-Receipts) -join '|') -eq ((@($a.Name, $b.Name) | Sort-Object) -join '|')) "Receipts: $((Get-Receipts) -join ', ')."
    Check (((Get-AppsEntries) -join '|') -eq ((@("MyzelAudio_$($a.Id)", "MyzelAudio_$($b.Id)") | Sort-Object) -join '|')) "Apps entries: $((Get-AppsEntries) -join ', ')."
    Check (Test-Path -LiteralPath $SuiteKey) "The suite's Apps entry $SuiteKey is missing."
    Pass '1 fresh install of two plugins in two formats'

    # 2. Detection.
    $status = Get-Status
    Expect-Status $status $a 'current'
    Expect-Status $status $b 'current'
    Expect-Status $status $c 'notinstalled'
    Set-ItemProperty -LiteralPath (Receipt $a) -Name Version -Value '0.0.1'
    Expect-Status (Get-Status) $a 'older'
    Set-ItemProperty -LiteralPath (Receipt $a) -Name Version -Value '9999.0.0'
    Expect-Status (Get-Status) $a 'newer'
    Set-ItemProperty -LiteralPath (Receipt $a) -Name Version -Value $a.Version
    # A receipt whose files were deleted by hand counts as not installed.
    New-Item -Path (Receipt $c) -Force | Out-Null
    Set-ItemProperty -LiteralPath (Receipt $c) -Name Version -Value $c.Version
    Expect-Status (Get-Status) $c 'notinstalled'
    Remove-Item -LiteralPath (Receipt $c) -Recurse -Force
    # A hand copy with no receipt; a CLAP carries no version the setup reads.
    Copy-Item -LiteralPath (Join-Path $Stage "$($c.Id)\CLAP\$($c.Name).clap") -Destination (ClapFile $c)
    $status = Get-Status
    Expect-Status $status $c 'unknown'
    Expect-Status $status $a 'current'
    Pass '2 detection: up to date, older, newer, unknown version, not installed, stale receipt'

    # 3. Add the third plugin; the first two stay untouched.
    $before = @{}
    foreach ($path in @((Vst3Binary $a), (ClapFile $a), (Vst3Binary $b), (ClapFile $b)))
    {
        $before[$path] = (Get-Item -LiteralPath $path).LastWriteTimeUtc
    }
    Invoke-Setup "/COMPONENTS=`"$($c.Id)`" /TASKS=`"vst3,clap,standalone`""
    foreach ($path in $before.Keys)
    {
        Check ((Get-Item -LiteralPath $path).LastWriteTimeUtc -eq $before[$path]) "$path changed although its plugin was not ticked."
    }
    Assert-Installed $a @('VST3', 'CLAP')
    Assert-Installed $b @('VST3', 'CLAP')
    Assert-Installed $c @('VST3', 'CLAP', 'Standalone')
    $status = Get-Status
    foreach ($p in @($a, $b, $c)) { Expect-Status $status $p 'current' }
    Pass '3 third plugin added, the first two untouched'

    # 4. pluginval against the installed VST3s.
    Invoke-Pluginval @($a, $b, $c)

    # 5. Remove one plugin through the installer's Remove group, one through its Settings -> Apps entry.
    Invoke-Setup "/COMPONENTS=`"$($a.Id)`" /TASKS=`"vst3,clap`" /REMOVE=`"$($b.Id)`""
    Assert-Absent $b
    Assert-Installed $a @('VST3', 'CLAP')
    Assert-Installed $c @('VST3', 'CLAP', 'Standalone')
    Invoke-Pluginval @($a, $c)
    Pass "5a $($b.Name) removed through the installer"

    # The quiet form of the Settings -> Apps command: a message box would hang the headless run.
    $command = (Get-ItemProperty -LiteralPath (AppsEntry $c)).QuietUninstallString
    Check ($command -match '^"([^"]+)"\s*(.*)$') "Apps entry of $($c.Name) has an unexpected UninstallString: $command"
    Invoke-Exe $Matches[1] $Matches[2]
    Assert-Absent $c
    Assert-Installed $a @('VST3', 'CLAP')
    Check (Test-Path -LiteralPath $SuiteKey) 'The suite entry went away although a plugin is still installed.'
    Invoke-Pluginval @($a)
    Pass "5b $($c.Name) removed through Settings -> Apps"

    # 6. Full uninstall.
    $uninstaller = (Get-ItemProperty -LiteralPath $SuiteKey).UninstallString.Trim('"')
    Invoke-Exe $uninstaller '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART'
    # The uninstaller hands over to a copy of itself in TEMP, which finishes after the exit code.
    Check (Wait-Gone $uninstaller 120) "$uninstaller is still there two minutes after uninstalling."
    Check (Wait-Gone $SuiteKey 30) "$SuiteKey is still there after uninstalling."
    foreach ($p in $plugins) { Assert-Absent $p }
    Check (-not (Test-Path -LiteralPath $AppDir)) "$AppDir is still there after uninstalling."
    Check (-not (Test-Path -LiteralPath $StartMenu)) "$StartMenu is still there after uninstalling."
    Check (@(Get-Receipts).Count -eq 0) "Receipts left after uninstalling: $((Get-Receipts) -join ', ')."
    Check (@(Get-AppsEntries).Count -eq 0) "Apps entries left after uninstalling: $((Get-AppsEntries) -join ', ')."
    Check ((Get-Content -LiteralPath $userDataMarker -Raw).Trim() -eq 'keep') "$userDataMarker was changed."
    Pass '6 full uninstall removed everything, user data untouched'

    # Removing the last plugin removes the suite too.
    Invoke-Setup "/COMPONENTS=`"$($a.Id)`" /TASKS=`"vst3`""
    Assert-Installed $a @('VST3')
    Invoke-Setup "/REMOVE=`"$($a.Id)`""
    Assert-Absent $a
    Check (Wait-Gone $SuiteKey 120) 'The suite entry is still there two minutes after removing the last plugin.'
    Check (Wait-Gone $AppDir 30) "$AppDir is still there after removing the last plugin."
    Check (Test-Path -LiteralPath $userDataMarker) "$userDataMarker is gone."
    Pass 'removing the last plugin ran the suite uninstaller'
}
catch
{
    Write-Host "FAIL $($_.Exception.Message)"
    Write-Host $_.ScriptStackTrace
    $exitCode = 1
}
finally
{
    if ($userDataMarker) { Remove-Item -LiteralPath $userDataMarker -Force -ErrorAction SilentlyContinue }
    if ($createdUserData) { Remove-Item -LiteralPath $UserData -Recurse -Force -ErrorAction SilentlyContinue }
}
exit $exitCode
