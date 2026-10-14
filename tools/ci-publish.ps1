# Pushes a results directory as a single commit to a results branch (force), so the results can
# be fetched with plain git. Needs GITHUB_TOKEN / GITHUB_REPOSITORY in the environment.
param(
    [Parameter(Mandatory = $true)][string] $Dir,
    [Parameter(Mandatory = $true)][string] $Branch,
    [string] $Message = "CI results"
)
$ErrorActionPreference = "Stop"
Push-Location $Dir
try {
    git init -q
    git config user.name  "github-actions"
    git config user.email "github-actions@users.noreply.github.com"
    git add -A
    git commit -q -m "$Message ($env:GITHUB_SHA)"
    git push -q --force "https://x-access-token:$($env:GITHUB_TOKEN)@github.com/$($env:GITHUB_REPOSITORY).git" "HEAD:refs/heads/$Branch"
    Write-Output "results pushed to $Branch"
} finally { Pop-Location }
