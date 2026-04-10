# nic_ble_remote（以 Android 为优先）

基于 Flutter 的应用，通过 BLE 与 ESP32-S3 固件通信，使用的 GATT 服务见 [`doc/ble_protocol.md`](../doc/ble_protocol.md)。

## 主机一次性配置（Windows）

1. **Flutter SDK** — 从 [flutter.dev](https://docs.flutter.dev/get-started/install/windows) 安装，并将 `…\flutter\bin` 加入 `PATH`。
2. **Android 工具链** — 安装 **Android Studio**（或独立的 **cmdline-tools**）、**JDK 17**，并接受 SDK 许可：
   - `flutter doctor --android-licenses`
3. **验证** — 在任意终端中执行：
   - `flutter doctor -v`

## Cursor / VS Code

- 扩展：**Dart**、**Flutter**（发布者：Dart Code）。
- 若未自动识别 Flutter，请在设置中将 **Flutter: Sdk Path** 指向你的 Flutter 安装目录。

## 生成 Android（及可选 iOS）工程文件

本目录仅包含 `pubspec.yaml` 与 `lib/`。在仓库根目录执行：

```bash
cd app
flutter create . --project-name nic_ble_remote --org com.example.nic_ble --platforms=android
```

稍后可添加 iOS：

```bash
flutter create . --platforms=ios
```

（需在 macOS 上并安装 Xcode。）

## Android 12+ 权限

在 `flutter create` 之后，将以下内容合并进 `android/app/src/main/AndroidManifest.xml` 的 `<manifest>` 内：

```xml
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
<uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />
```

如有需要，在代码中请求运行时权限（参见 [flutter_blue_plus](https://pub.dev/packages/flutter_blue_plus) 的 README）。

## 在手机上运行

```bash
cd app
flutter pub get
flutter run
```

使用已开启 USB 调试的真机，或支持 BLE 的模拟器（更推荐使用真机）。

## iOS（后续）

需要 Apple 开发者账号、在 `Info.plist` 中配置蓝牙用途说明，以及在 Mac 上构建。Dart 代码相同；在 macOS 上用 `flutter create` 启用对应平台即可。
