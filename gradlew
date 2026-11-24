#!/bin/sh
# Gradle wrapper launcher (POSIX). Uses gradle/wrapper/gradle-wrapper.jar when present; otherwise falls back to a
# system `gradle` and tells you how to generate the jar once: gradle wrapper --gradle-version 8.11.1
set -e
app_path=$0
while [ -h "$app_path" ]; do
  ls=$(ls -ld "$app_path"); link=${ls#*' -> '}
  case $link in /*) app_path=$link ;; *) app_path=$(dirname "$app_path")/$link ;; esac
done
APP_HOME=$(cd "$(dirname "$app_path")" > /dev/null && pwd -P)
WRAPPER_JAR="$APP_HOME/gradle/wrapper/gradle-wrapper.jar"

if [ ! -f "$WRAPPER_JAR" ]; then
  if command -v gradle > /dev/null 2>&1; then
    echo "gradle-wrapper.jar not found; generating it with the installed Gradle (one time)..." >&2
    (cd "$APP_HOME" && gradle wrapper --gradle-version 8.11.1 > /dev/null) || exec gradle "$@"
  else
    echo "ERROR: gradle/wrapper/gradle-wrapper.jar is missing and no 'gradle' command was found." >&2
    echo "Open the project in Android Studio (it generates the wrapper) or install Gradle 8.11.1 and run:" >&2
    echo "    gradle wrapper --gradle-version 8.11.1" >&2
    exit 1
  fi
fi

if [ -n "$JAVA_HOME" ] && [ -x "$JAVA_HOME/bin/java" ]; then JAVACMD="$JAVA_HOME/bin/java"; else JAVACMD=java; fi
if ! command -v "$JAVACMD" > /dev/null 2>&1; then
  echo "ERROR: JAVA_HOME is not set and no 'java' command was found (JDK 17 required)." >&2
  exit 1
fi
exec "$JAVACMD" -Xmx64m -Xms64m -Dorg.gradle.appname=gradlew \
  -classpath "$WRAPPER_JAR" org.gradle.wrapper.GradleWrapperMain "$@"
