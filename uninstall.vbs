on error resume next
Dim cmd
Set cmd = CreateObject ("WScript.Shell")

cmd.run "%COMSPEC% /C sc stop doRedis", 1, true
cmd.run "%COMSPEC% /C sc delete doRedis", 1, true
