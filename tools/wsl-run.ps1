# Run a command inside WSL from the project root, e.g.:
#   pwsh tools/wsl-run.ps1 make test
# Arguments are joined into one bash -lc command line. Single quotes are
# passed through bash; avoid $ expansion surprises by quoting in bash.
$root = "/mnt/" + ($PSScriptRoot.Substring(0,1).ToLower()) + ($PSScriptRoot.Substring(2) -replace '\\','/') + "/.."
$cmd = $args -join ' '
wsl -d Ubuntu -- bash -c "cd '$root' && . tools/ps2env.sh && $cmd"
exit $LASTEXITCODE
