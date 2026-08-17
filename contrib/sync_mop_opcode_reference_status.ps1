# SPDX-License-Identifier: GPL-3.0-or-later

<#
.SYNOPSIS
Synchronizes only the server-implementation overlay in Opcodes_reference.h.

.DESCRIPTION
The opcode rows, names, values, ordering, confidence, and binary notes belong to
the clean-room client analysis and are deliberately outside this script's
authority. This script derives ACTIVE/DORMANT/DOC from Opcodes.h plus symbolic
DefC/DefS registrations and updates only:

  * each row's implementation status;
  * the three status-summary lines; and
  * server-binding=<name> where the active server label differs from the
    binary-derived reference label for the same direction and wire value.

Run without -Write to check whether an update is needed.
#>

[CmdletBinding()]
param(
    [Parameter()]
    [string]$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,

    [Parameter()]
    [switch]$Write
)

$ErrorActionPreference = 'Stop'

$opcodesHeader = Join-Path $SourceRoot 'src/game/Server/Opcodes.h'
$opcodesSource = Join-Path $SourceRoot 'src/game/Server/Opcodes.cpp'
$registrationInclude = Join-Path $SourceRoot 'src/game/Server/opcode_register.inc'
$referenceHeader = Join-Path $SourceRoot 'src/game/Server/Opcodes_reference.h'

foreach ($requiredFile in @($opcodesHeader, $opcodesSource, $registrationInclude, $referenceHeader)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required source file is missing: $requiredFile"
    }
}

function Add-SetValue {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [System.Collections.Generic.HashSet[string]]$Set,

        [Parameter(Mandatory)]
        [string]$Value
    )

    [void]$Set.Add($Value)
}

function Get-WireKey {
    param(
        [Parameter(Mandatory)]
        [ValidateSet('C', 'S')]
        [string]$Direction,

        [Parameter(Mandatory)]
        [int]$Value
    )

    return "$Direction|$Value"
}

$enumValues = [System.Collections.Generic.Dictionary[string, int]]::new(
    [System.StringComparer]::Ordinal)
$enumKeys = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal)
$opcodeText = [System.IO.File]::ReadAllText($opcodesHeader)
$enumPattern = '(?m)^[ \t]*((CMSG|SMSG|MSG)_[A-Za-z0-9_]+)[ \t]*=[ \t]*(0x[0-9A-Fa-f]+)'

foreach ($match in [regex]::Matches($opcodeText, $enumPattern)) {
    $name = $match.Groups[1].Value
    $prefix = $match.Groups[2].Value
    $value = [Convert]::ToInt32($match.Groups[3].Value.Substring(2), 16)
    $enumValues[$name] = $value

    if ($prefix -in @('CMSG', 'MSG')) {
        Add-SetValue $enumKeys (Get-WireKey C $value)
    }
    if ($prefix -in @('SMSG', 'MSG')) {
        Add-SetValue $enumKeys (Get-WireKey S $value)
    }
}

if ($enumValues.Count -eq 0) {
    throw "No opcode enum constants were parsed from $opcodesHeader"
}

$activeBindings = [System.Collections.Generic.Dictionary[string, System.Collections.Generic.HashSet[string]]]::new(
    [System.StringComparer]::Ordinal)

# Keep the accepted grammar identical to the CMake gate: a real registration is
# an anchored, single-line DefC/DefS call with a symbolic Opcodes.h argument.
foreach ($registrationPath in @($opcodesSource, $registrationInclude)) {
    foreach ($line in [System.IO.File]::ReadAllLines($registrationPath)) {
        if ($line -notmatch '^[ \t]*Def[CS]\(') {
            continue
        }
        if ($line -notmatch '^[ \t]*Def([CS])\([ \t]*((CMSG|SMSG|MSG)_[A-Za-z0-9_]+)[ \t]*,') {
            throw "DefC/DefS registrations must use a symbolic Opcodes.h constant: $line"
        }

        $direction = $Matches[1]
        $name = $Matches[2]
        if (-not $enumValues.ContainsKey($name)) {
            throw "Registered opcode $name has no hex enum value in Opcodes.h"
        }

        $key = Get-WireKey $direction $enumValues[$name]
        if (-not $activeBindings.ContainsKey($key)) {
            $activeBindings[$key] = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::Ordinal)
        }
        Add-SetValue $activeBindings[$key] $name
    }
}

if ($activeBindings.Count -eq 0) {
    throw 'No symbolic DefC/DefS opcode registrations were parsed'
}

$originalText = [System.IO.File]::ReadAllText($referenceHeader)
$section = ''
$rowCount = 0
$statusChangeCount = 0
$bindingAnnotationCount = 0
$referenceKeys = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal)
$counts = @{
    'TOTAL|ACTIVE' = 0
    'TOTAL|DOC' = 0
    'TOTAL|DORMANT' = 0
    'S|ACTIVE' = 0
    'S|DOC' = 0
    'S|DORMANT' = 0
    'C|ACTIVE' = 0
    'C|DOC' = 0
    'C|DORMANT' = 0
}

$rowPattern = '^(?<prefix>[ \t]*\*[ \t]+(?<name>(?<kind>CMSG|SMSG|MSG)_[A-Za-z0-9_]+)[ \t]+(?<hex>0x[0-9A-Fa-f]+)[ \t]+)(?<status>ACTIVE|DOC|DORMANT)(?<suffix>[^\r\n]*)$'
$linePattern = '(?m)^[^\r\n]*'

$updatedText = [regex]::Replace($originalText, $linePattern, {
    param($lineMatch)

    $line = $lineMatch.Value
    if ($line -match '==== SMSG') {
        $script:section = 'S'
    }
    elseif ($line -match '==== CMSG') {
        $script:section = 'C'
    }

    $rowMatch = [regex]::Match($line, $rowPattern)
    if (-not $rowMatch.Success) {
        return $line
    }
    if (-not $script:section) {
        throw "Reference opcode row appears before an SMSG/CMSG section: $line"
    }

    $script:rowCount++
    $name = $rowMatch.Groups['name'].Value
    $value = [Convert]::ToInt32($rowMatch.Groups['hex'].Value.Substring(2), 16)
    $key = Get-WireKey $script:section $value
    Add-SetValue $referenceKeys $key

    if ($activeBindings.ContainsKey($key)) {
        $expectedStatus = 'ACTIVE'
    }
    elseif ($enumKeys.Contains($key)) {
        $expectedStatus = 'DORMANT'
    }
    else {
        $expectedStatus = 'DOC'
    }

    if ($rowMatch.Groups['kind'].Value -ne 'MSG') {
        $counts["TOTAL|$expectedStatus"]++
        $counts["$($script:section)|$expectedStatus"]++
    }
    if ($rowMatch.Groups['status'].Value -ne $expectedStatus) {
        $script:statusChangeCount++
    }

    # Keep all binary-evidence notes verbatim except the source-derived binding
    # annotation, which is recomputed so stale aliases cannot survive renames.
    $note = $rowMatch.Groups['suffix'].Value.Trim()
    $existingBindingMatch = [regex]::Match(
        $note,
        '(^|[ \t]+)server-binding=([A-Za-z0-9_,]+)(?=$|[ \t]+)')
    $existingBinding = if ($existingBindingMatch.Success) {
        $existingBindingMatch.Groups[2].Value
    }
    else {
        ''
    }
    $note = [regex]::Replace(
        $note,
        '(^|[ \t]+)server-binding=[A-Za-z0-9_,]+(?=$|[ \t]+)',
        '').Trim()

    $bindingAnnotation = ''
    $expectedBinding = ''
    if ($expectedStatus -eq 'ACTIVE') {
        [string[]]$bindingNames = @($activeBindings[$key])
        [Array]::Sort($bindingNames, [System.StringComparer]::Ordinal)
        if ($bindingNames -notcontains $name) {
            $expectedBinding = $bindingNames -join ','
            $bindingAnnotation = 'server-binding=' + $expectedBinding
            $script:bindingAnnotationCount++
        }
    }

    if ($bindingAnnotation) {
        if ($note) {
            $note += "  $bindingAnnotation"
        }
        else {
            $note = $bindingAnnotation
        }
    }

    if ($rowMatch.Groups['status'].Value -eq $expectedStatus -and
            $existingBinding -eq $expectedBinding) {
        return $line
    }

    $replacement = $rowMatch.Groups['prefix'].Value + $expectedStatus
    if ($note) {
        $replacement = $replacement.PadRight(
            $rowMatch.Groups['prefix'].Value.Length + 8) + " $note"
    }
    return $replacement
})

if ($rowCount -ne 1520) {
    throw "Expected 1520 reference opcode rows, parsed $rowCount"
}

$summaryReplacements = @{
    '(?m)^[ \t]*\*[ \t]+STATUS TOTALS:[^\r\n]*(\r?)$' =
        " * STATUS TOTALS: ACTIVE=$($counts['TOTAL|ACTIVE']), DOC=$($counts['TOTAL|DOC']), DORMANT=$($counts['TOTAL|DORMANT'])"
    '(?m)^[ \t]*\*[ \t]+SMSG: ACTIVE=[^\r\n]*(\r?)$' =
        " *   SMSG: ACTIVE=$($counts['S|ACTIVE']), DOC=$($counts['S|DOC']), DORMANT=$($counts['S|DORMANT'])"
    '(?m)^[ \t]*\*[ \t]+CMSG: ACTIVE=[^\r\n]*(\r?)$' =
        " *   CMSG: ACTIVE=$($counts['C|ACTIVE']), DOC=$($counts['C|DOC']), DORMANT=$($counts['C|DORMANT'])"
}
foreach ($summaryPattern in $summaryReplacements.Keys) {
    $updatedText = [regex]::Replace(
        $updatedText,
        $summaryPattern,
        $summaryReplacements[$summaryPattern] + '$1')
}

$missingBindings = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal)
foreach ($entry in $activeBindings.GetEnumerator()) {
    if (-not $referenceKeys.Contains($entry.Key)) {
        foreach ($bindingName in $entry.Value) {
            Add-SetValue $missingBindings $bindingName
        }
    }
}
$actualMissing = @($missingBindings) | Sort-Object
$expectedMissing = @('CMSG_TUTORIAL_FLAG')
if (($actualMissing -join ',') -ne ($expectedMissing -join ',')) {
    throw "Registered bindings absent from the reference changed. Expected '$expectedMissing'; actual '$actualMissing'"
}

$changed = $updatedText -cne $originalText
Write-Host (
    "MoP opcode reference overlay: rows=$rowCount, " +
    "status-changes=$statusChangeCount, binding-annotations=$bindingAnnotationCount, " +
    "ACTIVE=$($counts['TOTAL|ACTIVE']), DOC=$($counts['TOTAL|DOC']), " +
    "DORMANT=$($counts['TOTAL|DORMANT'])")

if (-not $changed) {
    Write-Host 'Opcodes_reference.h is already synchronized.'
    exit 0
}

if (-not $Write) {
    throw 'Opcodes_reference.h is stale. Re-run with -Write to synchronize it.'
}

[System.IO.File]::WriteAllText(
    $referenceHeader,
    $updatedText,
    [System.Text.UTF8Encoding]::new($false))
Write-Host "Updated $referenceHeader"
