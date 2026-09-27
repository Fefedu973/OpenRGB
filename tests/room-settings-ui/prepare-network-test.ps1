# SPDX-License-Identifier: GPL-2.0-or-later
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source=[IO.File]::ReadAllText((Join-Path $repo 'NetworkClient.cpp'))
$start=$source.IndexOf('void NetworkClient::ProfileManagerListenThread(')
$end=$source.IndexOf('void NetworkClient::ConnectionThreadFunction()', $start)
if ($start -lt 0 -or $end -le $start) { throw 'Production profile worker was not found.' }
$target=Join-Path $PSScriptRoot '.build\network-profile-worker.inc'
[IO.File]::WriteAllText($target,$source.Substring($start,$end-$start))
