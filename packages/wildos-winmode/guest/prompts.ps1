# Wild OS Windows mode: "Do you want to allow this app to make changes"
# prompts show as a window beside the app that asked.
#
# Windows normally shows that prompt on the secure desktop, a separate
# screen the rest of Windows can't draw on. Windows mode shows Windows apps
# one window at a time, and the secure desktop is not a window, so the
# prompt never appeared: an installer whose Next button has the shield
# waited for an answer nobody could give and then failed (issue 92). The
# prompt itself still asks the same question and still needs a yes.
#
# Run by the host in every Windows mode, new or set up before this existed.

$ErrorActionPreference = 'Stop'

$version = '1'
$key = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'

if ((Get-ItemProperty -Path $key -Name PromptOnSecureDesktop -ErrorAction SilentlyContinue).PromptOnSecureDesktop -eq 0) {
    'already beside the app'
} else {
    New-ItemProperty -Path $key -Name PromptOnSecureDesktop -Value 0 -PropertyType DWord -Force | Out-Null
    'prompts beside the app'
}
