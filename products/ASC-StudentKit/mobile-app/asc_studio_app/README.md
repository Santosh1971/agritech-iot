# ASC Studio app

This is the Android app for every ASC Student Kit, Mini or Mega. It finds kits over Bluetooth LE, asks the board for its design (`{"cmd":"get_design"}`), and builds the student's screen from the layout they made in the studio's App stage. There is no code per student.

- `lib/protocol.dart`: the line protocol, the layout model and alerts. Pure Dart, tested by `test/protocol_test.dart`.
- `lib/board.dart`: the Bluetooth link. It uses the firmware's Nordic-UART-style service (see `../../firmware/README.md`).
- `lib/main.dart`: the screens. One finds nearby kits; the other is the student's own screen, with English/हिंदी and Engineer's view.

Switching an output from the phone needs the kit's **PAIR** button pressed within the previous 2 minutes. The firmware enforces this. When the phone connects, it also sets the board's clock, which the DevKit stand-in needs for its rules and field log.

```bash
flutter pub get
flutter test
flutter build apk --release
```

CI (`.github/workflows/asc-studio-app.yml`) builds every push that touches this folder. Pushes to `main` upload a `dev-<sha>` APK as product `ASC_KIT`, and an `asc-app-v*` tag uploads a release. Students download the latest release from the App stage in the studio.
