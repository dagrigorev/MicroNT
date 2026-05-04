Get-ChildItem .\scripts\*.ps1 | Unblock-File
Unblock-File .\tools\mkiso.py

Get-ChildItem .\scripts\*.ps1 | Get-Item -Stream Zone.Identifier -ErrorAction SilentlyContinue