# Microsoft Developer Studio Project File - Name="char_txt" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=char_txt - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "char-server_txt.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "char-server_txt.mak" CFG="char_txt - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "char_txt - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "char_txt - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "char_txt - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir ".."
# PROP Intermediate_Dir "tmp\char_txt\Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /W3 /Zi /O2 /I "..\src\common" /I "..\3rdparty\msinttypes\include" /FI"config.vc.h" /D "WIN32" /D "NDEBUG" /D "TXT_ONLY" /FR /FD /GF /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 ws2_32.lib /nologo /subsystem:console /debug /machine:I386

!ELSEIF  "$(CFG)" == "char_txt - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir ".."
# PROP Intermediate_Dir "tmp\char_txt\Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /Gi /ZI /Od /I "..\src\common" /I "..\3rdparty\msinttypes\include" /FI"config.vc.h" /D "WIN32" /D "_DEBUG" /D "TXT_ONLY" /FR /FD /GZ /c
# SUBTRACT CPP /YX
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo /o"char-server_txt.bsc"
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ws2_32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "char_txt - Win32 Release"
# Name "char_txt - Win32 Debug"
# Begin Source File

SOURCE=..\src\char\char.c
# End Source File
# Begin Source File

SOURCE=..\src\char\char.h
# End Source File
# Begin Source File

SOURCE=..\src\char\int_guild.c
# End Source File
# Begin Source File

SOURCE=..\src\char\int_guild.h
# End Source File
# Begin Source File

SOURCE=..\src\char\int_homun.c
# End Source File
# Begin Source File

SOURCE=..\src\char\int_homun.h
# End Source File
# Begin Source File

SOURCE=..\src\char\int_party.c
# End Source File
# Begin Source File

SOURCE=..\src\char\int_party.h
# End Source File
# Begin Source File

SOURCE=..\src\char\int_pet.c
# End Source File
# Begin Source File

SOURCE=..\src\char\int_pet.h
# End Source File
# Begin Source File

SOURCE=..\src\char\int_status.c
# End Source File
# Begin Source File

SOURCE=..\src\char\int_status.h
# End Source File
# Begin Source File

SOURCE=..\src\char\int_storage.c
# End Source File
# Begin Source File

SOURCE=..\src\char\int_storage.h
# End Source File
# Begin Source File

SOURCE=..\src\char\inter.c
# End Source File
# Begin Source File

SOURCE=..\src\char\inter.h
# End Source File
# End Target
# End Project
