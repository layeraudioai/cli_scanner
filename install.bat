@echo off

echo IF NOT DKP THEN BYE!

set folderPath="/opt/devkitpro"
if not exist "%folderPath%" (
   echo bye bye
   exit
)

echo onwards

set folderPath="/opt/devkitpro/msys2/usr/bin"
if not exist "%folderPath%" (
   mkdir "%folderPath%"
   echo Folder created successfully
) else (
   echo Folder already exists
)
cp %1 %folderPath%

set folderPath="/bin"
if not exist "%folderPath%" (
   mkdir "%folderPath%"
   echo Folder created successfully
) else (
   echo Folder already exists
)
cp %1 %folderPath%
