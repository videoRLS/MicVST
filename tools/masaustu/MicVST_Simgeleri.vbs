Option Explicit
' Masaustundeki MicVST kisayollarina (Renk Ince, Renk Tok, Ince, Tok) MicVST'nin simgesini verir.
' Kisayollarin neyi calistirdigina dokunmaz: yalnizca simgeleri degisir.
Dim WshShell, fso, strIcon, arrTargets, arrFolders, strFolder, objFile, objLink
Dim strTarget, strName, i, bMatch, nDone, strList

strIcon = "C:\MicVST\MicVST.exe"
' Kisayollarin calistirdigi betikler (dosya adinin sonu)
arrTargets = Array("\Renk_Ince.vbs", "\Renk_Tok.vbs", "\Ince VST.vbs", "\Tok VST.vbs")

Set WshShell = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

If Not fso.FileExists(strIcon) Then
    MsgBox "MicVST bulunamadi:" & vbCrLf & strIcon, 16, "MicVST simgeleri"
    WScript.Quit
End If

' Kullanicinin ve ortak masaustu (OneDrive'a tasinmis olsa da)
arrFolders = Array(WshShell.SpecialFolders("Desktop"), WshShell.SpecialFolders("AllUsersDesktop"))
nDone = 0
strList = ""
For Each strFolder In arrFolders
    If strFolder <> "" Then
        If fso.FolderExists(strFolder) Then
            For Each objFile In fso.GetFolder(strFolder).Files
                If LCase(fso.GetExtensionName(objFile.Name)) = "lnk" Then
                    Set objLink = WshShell.CreateShortcut(objFile.Path)
                    strTarget = LCase(objLink.TargetPath)
                    bMatch = False
                    For i = 0 To UBound(arrTargets)
                        strName = LCase(arrTargets(i))
                        If Len(strTarget) >= Len(strName) Then
                            If Right(strTarget, Len(strName)) = strName Then bMatch = True
                        End If
                    Next
                    If bMatch Then
                        objLink.IconLocation = strIcon & ",0"
                        On Error Resume Next
                        objLink.Save
                        If Err.Number = 0 Then
                            nDone = nDone + 1
                            strList = strList & vbCrLf & "  " & fso.GetBaseName(objFile.Name)
                        End If
                        Err.Clear
                        On Error GoTo 0
                    End If
                End If
            Next
        End If
    End If
Next

' Windows'un simge onbellegini yenile (yeni simgeler hemen gorunsun)
On Error Resume Next
WshShell.Run "ie4uinit.exe -show", 0, False
On Error GoTo 0

If nDone = 0 Then
    MsgBox "Masaustunde MicVST kisayolu bulunamadi.", 48, "MicVST simgeleri"
Else
    MsgBox nDone & " kisayolun simgesi MicVST oldu:" & strList, 64, "MicVST simgeleri"
End If
