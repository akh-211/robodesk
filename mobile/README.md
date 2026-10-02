# RoboDesk phone bridge

## Android

`android/` contains the companion app source. Build it with Android Studio or Gradle, install it, grant **Notification access**, grant **Nearby devices** permissions, then pair RoboDesk from Android Bluetooth settings. Keep the phone and robot within BLE range. Choose package IDs in the robot dashboard's notification allowlist.

For BLE trust enrollment, use **Forget current phone and start pairing** on the authenticated robot dashboard. When the Android bridge appears, verify the displayed address and approve it in the dashboard. Only that saved peer is accepted after enrollment; replacing it requires another dashboard pairing window.

The app forwards only app label, title, and a short notification snippet. The firmware drops non-allowlisted messages and keeps accepted messages in RAM for at most five minutes. BLE writes require an encrypted bonded link.

## iPhone

The robot currently does not implement the iPhone ANCS central/client path. Apple’s newer Accessory Notifications app framework is restricted for customer installations to iPhones in the EU with an EU Apple Account, so it cannot provide the selected worldwide iPhone behavior. Implementing worldwide iPhone support requires the firmware to connect to the paired iPhone as a BLE central, subscribe to ANCS, and retrieve the notification attributes. The present BLE transport only exposes the robot as a peripheral for Android forwarding.

## BLE wire format

Service `93de0001-2c7d-4a52-9f1c-6f4b4f424c45`; encrypted write characteristic `93de0002-2c7d-4a52-9f1c-6f4b4f424c45`; encrypted allowlist-read characteristic `93de0003-2c7d-4a52-9f1c-6f4b4f424c45`. A write contains four UTF-8 fields separated by three LF bytes: package/bundle identifier, app label, title, body. The phone reads the allowlist and filters before sending; the robot checks it again and does not persist notification content.
