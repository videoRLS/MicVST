Option Explicit
' MicVST islem onceligi: Normal / Normalin Ustunde / Yuksek
'
'  Cift tiklayinca (bilgi verilmeden): masaustune 3 kisayol yapar, MicVST simgesiyle.
'  Kisayollar bu dosyayi 0, 1 ya da 2 ile calistirir: calisan MicVST'nin onceligi hemen degisir.
'  Bu dosyayi kalici bir yere koyun (ornegin C:\MicVST); kisayollar onu buradan calistirir.
Dim WshShell, fso, strExe, arrNames, arrClass, arrBase, nChoice

strExe = "C:\MicVST\MicVST.exe"
' Turkce harfler: O-umlaut = ChrW(214), U-umlaut = ChrW(220), u-umlaut = ChrW(252)
arrNames = Array("Normal", "Normalin " & ChrW(220) & "st" & ChrW(252) & "nde", "Y" & ChrW(252) & "ksek")
arrClass = Array(32, 32768, 128)   ' NORMAL / ABOVE_NORMAL / HIGH_PRIORITY_CLASS
arrBase  = Array(8, 10, 13)        ' Win32_Process.Priority icin beklenen deger

Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

If WScript.Arguments.Count = 0 Then
    MakeShortcuts
Else
    nChoice = CInt(WScript.Arguments(0))
    If nChoice < 0 Or nChoice > 2 Then WScript.Quit 1
    SetPriority nChoice
End If

Sub MakeShortcuts()
    Dim strDesktop, i, objLink, strTitle
    strDesktop = WshShell.SpecialFolders("Desktop")
    For i = 0 To 2
        strTitle = "MicVST " & ChrW(214) & "ncelik - " & arrNames(i)
        Set objLink = WshShell.CreateShortcut(strDesktop & "\" & strTitle & ".lnk")
        objLink.TargetPath = WshShell.ExpandEnvironmentStrings("%SystemRoot%") & "\System32\wscript.exe"
        objLink.Arguments = """" & WScript.ScriptFullName & """ " & i
        objLink.WorkingDirectory = fso.GetParentFolderName(WScript.ScriptFullName)
        If fso.FileExists(strExe) Then objLink.IconLocation = strExe & ",0"
        objLink.Description = "MicVST i" & ChrW(351) & "lem " & ChrW(246) & "nceli" & ChrW(287) & "i: " & arrNames(i)
        objLink.Save
    Next
    On Error Resume Next
    WshShell.Run "ie4uinit.exe -show", 0, False
    On Error GoTo 0
    MsgBox "Masa" & ChrW(252) & "st" & ChrW(252) & "ne 3 k" & ChrW(305) & "sayol eklendi:" & vbCrLf & _
           "  MicVST " & ChrW(214) & "ncelik - Normal" & vbCrLf & _
           "  MicVST " & ChrW(214) & "ncelik - " & arrNames(1) & vbCrLf & _
           "  MicVST " & ChrW(214) & "ncelik - " & arrNames(2) & vbCrLf & vbCrLf & _
           "Bu dosyay" & ChrW(305) & " ta" & ChrW(351) & ChrW(305) & "may" & ChrW(305) & "n ya da silmeyin: k" & ChrW(305) & "sayollar onu kullan" & ChrW(305) & "r.", 64, "MicVST"
End Sub

Sub SetPriority(n)
    Dim objWMI, colProc, objProc, nFound, nOk
    Set objWMI = GetObject("winmgmts:\\.\root\cimv2")
    Set colProc = objWMI.ExecQuery("Select * from Win32_Process Where Name = 'MicVST.exe'")
    nFound = 0 : nOk = 0
    For Each objProc In colProc
        nFound = nFound + 1
        If objProc.SetPriority(arrClass(n)) = 0 Then nOk = nOk + 1
    Next
    If nFound = 0 Then
        MsgBox "MicVST " & ChrW(231) & "al" & ChrW(305) & ChrW(351) & "m" & ChrW(305) & "yor. " & ChrW(214) & "nce MicVST'yi a" & ChrW(231) & ChrW(305) & "n.", 48, "MicVST"
        Exit Sub
    End If
    ' Windows'tan geri oku
    Dim bConfirmed : bConfirmed = True
    Set colProc = objWMI.ExecQuery("Select * from Win32_Process Where Name = 'MicVST.exe'")
    For Each objProc In colProc
        If objProc.Priority <> arrBase(n) Then bConfirmed = False
    Next
    ' Basarili olunca sessiz kalir; yalnizca Windows reddederse uyarir.
    If Not (nOk = nFound And bConfirmed) Then
        MsgBox "Windows " & ChrW(246) & "nceli" & ChrW(287) & "i de" & ChrW(287) & "i" & ChrW(351) & "tirmedi (" & arrNames(n) & ").", 48, "MicVST"
    End If
End Sub
