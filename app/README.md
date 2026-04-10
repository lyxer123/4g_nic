# nic_ble_remote (Android-first)

Flutter app that talks to the ESP32-S3 firmware over BLE using the GATT service documented in [`doc/ble_protocol.md`](../doc/ble_protocol.md).

## One-time host setup (Windows)

1. **Flutter SDK** — install from [flutter.dev](https://docs.flutter.dev/get-started/install/windows) and add `…\flutter\bin` to `PATH`.
2. **Android toolchain** — install **Android Studio** (or standalone **cmdline-tools**), **JDK 17**, accept SDK licenses:
   - `flutter doctor --android-licenses`
3. **Verify** — in any terminal:
   - `flutter doctor -v`

## Cursor / VS Code

- Extensions: **Dart**, **Flutter** (publisher: Dart Code).
- Point **Flutter: Sdk Path** in settings to your Flutter install if it is not discovered automatically.

## Generate Android (and optional iOS) project files

This folder ships `pubspec.yaml` and `lib/` only. From the repo root:

```bash
cd app
flutter create . --project-name nic_ble_remote --org com.example.nic_ble --platforms=android
```

Add iOS later with:

```bash
flutter create . --platforms=ios
```

(on macOS with Xcode).

## Android 12+ permissions

After `flutter create`, merge into `android/app/src/main/AndroidManifest.xml` inside `<manifest>`:

```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
```

Request runtime permissions in code if needed (see [flutter_blue_plus](https://pub.dev/packages/flutter_blue_plus) README).

## Run on a phone

```bash
cd app
flutter pub get
flutter run
```

Use a USB-debugged device or emulator with BLE support (real hardware recommended).

## iOS (later)

Requires Apple developer account, Bluetooth usage strings in `Info.plist`, and a Mac to build. Same Dart code; enable platforms with `flutter create` on macOS.
