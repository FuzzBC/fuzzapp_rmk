# Publishes the just-built release APK as a GitHub release on FuzzBC/fuzzapp.
# Tag is V<versionName> (e.g. V1.003) - UpdateChecker.java pulls the numeric
# versionCode back out of it (the part after the last '.') to compare.
#
# Token: reads github_release.properties (githubToken=...) next to this script.
# That file is gitignored - create it locally, it is never committed.

$ErrorActionPreference = 'Stop'

$root  = $PSScriptRoot
$owner = 'FuzzBC'
$repo  = 'fuzzapp'

$propsPath = Join-Path $root 'github_release.properties'
if (-not (Test-Path $propsPath)) {
    Write-Error "Missing $propsPath - copy github_release.properties.example, fill in your token, and retry."
    exit 1
}
$props = @{}
Get-Content $propsPath | ForEach-Object {
    if ($_ -match '^\s*([^#=]+?)\s*=\s*(.*)\s*$') { $props[$matches[1]] = $matches[2] }
}
$token = $props['githubToken']
if ([string]::IsNullOrWhiteSpace($token)) {
    Write-Error "githubToken not set in $propsPath"
    exit 1
}
$tokenKind = if ($token.StartsWith('ghp_')) { 'classic' }
             elseif ($token.StartsWith('github_pat_')) { 'fine-grained' }
             else { 'unrecognized prefix' }
Write-Output "Token read: $tokenKind, length $($token.Length)"

$versionPropsPath = Join-Path $root 'version.properties'
$versionPropsText = Get-Content $versionPropsPath -Raw
if ($versionPropsText -notmatch 'versionCode\s*=\s*(\d+)') { Write-Error "versionCode not found in $versionPropsPath"; exit 1 }
$versionCode = [int]$matches[1]
# versionMajor is its own property in version.properties (not hardcoded here)
# so this always matches whatever app/build.gradle actually built - defaults
# to 1 if the property is missing, same fallback build.gradle uses.
$versionMajor = if ($versionPropsText -match 'versionMajor\s*=\s*(\d+)') { [int]$matches[1] } else { 1 }
$versionName = "$versionMajor." + $versionCode.ToString('000')
$tag = "V$versionName"

$apkPath = "D:\AndroidBuilds\$(Split-Path $root -Leaf)\app\outputs\apk\release\app-release.apk"
if (-not (Test-Path $apkPath)) { Write-Error "APK not found at $apkPath - build the release variant first."; exit 1 }
$apkName = "FuZzLED_$tag.apk"

# Pull this version's entry out of CHANGELOG.md (see AGENTS.md) for the
# release body / in-app "What's new" text. Falls back to a generic note
# (with a loud warning) if nobody added one - publish still proceeds either
# way, this is a reminder, not a gate.
$releaseNotes = "No changelog entry found for $versionName - see AGENTS.md."
$changelogPath = Join-Path $root 'CHANGELOG.md'
if (Test-Path $changelogPath) {
    # -Encoding UTF8 is required: PS 5.1's Get-Content default (system ANSI
    # codepage) misreads this UTF-8-no-BOM file's non-ASCII characters (em
    # dashes, etc.), corrupting them before they ever reach the JSON body.
    $lines = Get-Content $changelogPath -Encoding UTF8
    $heading = "## $versionName"
    $startIdx = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -eq $heading) { $startIdx = $i + 1; break }
    }
    if ($startIdx -ge 0) {
        $endIdx = $lines.Count
        for ($i = $startIdx; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match '^##\s') { $endIdx = $i; break }
        }
        $entryLines = $lines[$startIdx..($endIdx - 1)] | Where-Object { $_.Trim() -ne '' }
        if ($entryLines.Count -gt 0) {
            $releaseNotes = ($entryLines -join "`n")
        }
    }
}
if ($releaseNotes.StartsWith('No changelog entry')) {
    Write-Warning "$releaseNotes  (releasing anyway with a placeholder note)"
} else {
    Write-Output "Changelog entry for $versionName found ($($releaseNotes.Split("`n").Count) line(s))"
}

$headers = @{
    Authorization = "Bearer $token"
    Accept        = 'application/vnd.github+json'
    'User-Agent'  = 'fuzzapp-publish-script'
}

Write-Output "Publishing $tag (versionName $versionName) to $owner/$repo ..."

$existing = $null
try {
    $existing = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases/tags/$tag" -Headers $headers -Method Get
} catch {
    if ($_.Exception.Response.StatusCode.value__ -ne 404) { throw }
}

if ($existing) {
    Write-Output "Release $tag already exists (id $($existing.id)) - not creating a duplicate."
    Write-Output "If you meant to replace its APK, delete the release on GitHub first and rerun."
    exit 0
}

$body = @{
    tag_name = $tag
    name     = $tag
    body     = $releaseNotes
} | ConvertTo-Json
# PowerShell 5.1's Invoke-RestMethod encodes a string -Body using the system
# codepage, not UTF-8, unless given raw bytes - release notes with non-ASCII
# characters (em dashes, etc.) get silently mangled into invalid UTF-8,
# which GitHub's API then rejects with "Problems parsing JSON" (400).
$bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($body)

$release = Invoke-RestMethod -Uri "https://api.github.com/repos/$owner/$repo/releases" -Headers $headers -Method Post -Body $bodyBytes -ContentType 'application/json; charset=utf-8'
Write-Output "Created release id $($release.id)"

$uploadUrl = $release.upload_url -replace '\{\?name,label\}', "?name=$apkName"
$apkBytes = [System.IO.File]::ReadAllBytes($apkPath)
$uploadHeaders = $headers.Clone()
$uploadHeaders['Content-Type'] = 'application/vnd.android.package-archive'

Invoke-RestMethod -Uri $uploadUrl -Headers $uploadHeaders -Method Post -Body $apkBytes | Out-Null

Write-Output "Uploaded $apkName to $tag - https://github.com/$owner/$repo/releases/tag/$tag"
