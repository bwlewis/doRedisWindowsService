on error resume next
Dim cmd
dim f1
Set cmd = CreateObject("WScript.Shell")

dim home
home = Session.Property("CustomActionData")
Dim regEx
Set regEx = New RegExp
regEx.Pattern = "\\"
regEx.IgnoreCase = True
regEx.Global = True
home = regEx.Replace(home, "\\")

dim rpath
rpath = ""
dim filesys
Set filesys = CreateObject("Scripting.FileSystemObject") 
const HKEY_CURRENT_USER = &H80000001
const HKEY_LOCAL_MACHINE = &H80000002
strComputer = "."
Set oReg=GetObject("winmgmts:{impersonationLevel=impersonate}!\\" &_ 
strComputer & "\root\default:StdRegProv")
strKeyPath = "Software\R-core\R"
oReg.EnumKey HKEY_LOCAL_MACHINE, strKeyPath, arrSubKeys
For Each subkey In arrSubKeys
  x = subkey
Next
if len(x) < 2 then
  oReg.EnumKey HKEY_CURRENT_USER, strKeyPath, arrSubKeys
  For Each subkey In arrSubKeys
    x = subkey
  Next
end if
if len(x) > 2 then
  rpath = cmd.RegRead("HKLM\Software\R-Core\R\" & x &"\InstallPath")
  if filesys.FileExists(rpath & "\bin\x64\Rterm.exe") then
    rpath = rpath & "\bin\x64\Rterm.exe"
  else
    rpath = rpath & "\R.exe"
  end if 
end if

on error goto 0

dim rterm
rterm = rpath
if len(rpath) < 2 then
  Const WINDOW_HANDLE = 0
  Const NO_OPTIONS = 0
  Set sap = CreateObject("Shell.Application")
  Set objFolder = sap.BrowseForFolder (WINDOW_HANDLE, "The R Redis Worker Setup program needs to know where to find Rterm.exe.  Select the directory containing the desired Rterm.exe program:", NO_OPTIONS, "C:\")
  if (objFolder is nothing) then
    quit 2
  end if
  Set objFolderItem = objFolder.Self
  rpath = objFolderItem.Path
  rpath = Replace(rpath, "\", "\\")
  rterm = rpath & "\\Rterm.exe"
else
  rterm = Replace(rterm, "\", "\\")
end if

f1 = "%COMSPEC% /C echo Rterm=" & rterm & " >> """ & home & "\\doRedis.ini"""
cmd.run f1, 1, true

set d = CreateObject("Scripting.FileSystemObject")
If d.FileExists(rterm) Then
  rem OK!
Else
  ret = MsgBox("Error: Invalid path.", 4096)
  quit 2
End If


Rem INSTALL REQUIRED R PACKAGES
f1 = """" & rterm & """ --slave -e """
f1 = f1 & "if(is.na(packageDescription('doRedis')[[1]])) install.packages('foreach', repos='http://cran.r-project.org');"
dim ret
ret = cmd.run(f1, 1, true)
if ret <> 0 then
  ret = MsgBox("Error: R package installation failed.", 4096)
  quit ret
end if

f1 = "%COMSPEC% /C sc create doRedis binPath= """ & home & "\\rworkers.exe"" start= auto"
cmd.run f1, 1, true 
cmd.run "%COMSPEC% /C sc start doRedis", 1, true

msgbox "Please review the configuration settings in " & home & "doRedis.ini, adjusting them as required. See the README file for help.", 4096 + 48, "IMPORTANT NOTE"
