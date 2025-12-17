@echo off
setlocal

pushd "%~dp0android" || exit /b 1

rem Build all APKs (per ABI) using the Gradle wrapper in /android
call gradlew.bat assembleDebug

popd
