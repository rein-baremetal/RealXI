@rem Gradle wrapper launcher (Windows). Needs gradle\wrapper\gradle-wrapper.jar; generate it once with:
@rem     gradle wrapper --gradle-version 8.11.1
@if "%DEBUG%"=="" @echo off
setlocal
set DIRNAME=%~dp0
if "%DIRNAME%"=="" set DIRNAME=.
set APP_HOME=%DIRNAME%
set WRAPPER_JAR=%APP_HOME%gradle\wrapper\gradle-wrapper.jar

if exist "%WRAPPER_JAR%" goto findJava
where gradle >NUL 2>&1
if %ERRORLEVEL% neq 0 goto noWrapper
echo gradle-wrapper.jar not found; generating it with the installed Gradle 1>&2
pushd "%APP_HOME%" & call gradle wrapper --gradle-version 8.11.1 >NUL & popd
if exist "%WRAPPER_JAR%" goto findJava
gradle %*
exit /b %ERRORLEVEL%

:noWrapper
echo ERROR: gradle\wrapper\gradle-wrapper.jar is missing and no 'gradle' command was found. 1>&2
echo Open the project in Android Studio or run: gradle wrapper --gradle-version 8.11.1 1>&2
exit /b 1

:findJava
if defined JAVA_HOME (set JAVA_EXE=%JAVA_HOME%\bin\java.exe) else (set JAVA_EXE=java.exe)
"%JAVA_EXE%" -version >NUL 2>&1
if %ERRORLEVEL% neq 0 (
  echo ERROR: Java not found. Install JDK 17 and set JAVA_HOME. 1>&2
  exit /b 1
)
"%JAVA_EXE%" -Xmx64m -Xms64m -Dorg.gradle.appname=gradlew -classpath "%WRAPPER_JAR%" org.gradle.wrapper.GradleWrapperMain %*
exit /b %ERRORLEVEL%
