# Releasing RealXI

Debug APKs need no secrets. A release APK is signed only when a keystore is supplied; otherwise `assembleRelease` produces an unsigned APK.

## 1. Gradle wrapper (one time)

The wrapper scripts (`gradlew`, `gradlew.bat`, `gradle/wrapper/gradle-wrapper.properties`) are committed, but the binary
`gradle/wrapper/gradle-wrapper.jar` is not. Generate it once and commit it:

```
gradle wrapper --gradle-version 8.11.1
git add gradle/wrapper/gradle-wrapper.jar
```

(Opening the project in Android Studio also generates it.) After that, `./gradlew :app:assembleDebug` works anywhere.

## 2. Create a keystore (one time, keep it safe)

```
keytool -genkeypair -v -keystore realxi-release.jks -alias realxi -keyalg RSA -keysize 2048 -validity 10000
```

Back the file and its passwords up somewhere private. If you lose it you cannot update the app on Play Store. Never commit it (`*.jks` is git-ignored).

## 3. Build a signed release locally

Add to `~/.gradle/gradle.properties` (not the repo):

```
KEYSTORE_FILE=/absolute/path/realxi-release.jks
KEYSTORE_PASSWORD=...
KEY_ALIAS=realxi
KEY_PASSWORD=...
```

or export the same names as environment variables, then:

```
./gradlew :app:assembleRelease      # APK in app/build/outputs/apk/release/
./gradlew :app:bundleRelease        # AAB for Play Store
```

## 4. Release from GitHub

1. Base64 the keystore: `base64 -w0 realxi-release.jks` and add GitHub repository secrets `KEYSTORE_BASE64`, `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`.
2. In the workflow, before the Gradle step, decode it and export the path:
   ```
   - run: echo "$KEYSTORE_BASE64" | base64 -d > "$RUNNER_TEMP/release.jks"
     env: { KEYSTORE_BASE64: "${{ secrets.KEYSTORE_BASE64 }}" }
   - run: gradle :app:assembleRelease
     env:
       KEYSTORE_FILE: ${{ runner.temp }}/release.jks
       KEYSTORE_PASSWORD: ${{ secrets.KEYSTORE_PASSWORD }}
       KEY_ALIAS: ${{ secrets.KEY_ALIAS }}
       KEY_PASSWORD: ${{ secrets.KEY_PASSWORD }}
   ```
3. Bump `versionCode` and `versionName` in `app/build.gradle.kts`, commit, then tag and push:
   ```
   git tag v1.0.1 && git push origin v1.0.1
   ```
   The `v*` tag triggers the build workflow, which attaches the APK to a GitHub Release.
