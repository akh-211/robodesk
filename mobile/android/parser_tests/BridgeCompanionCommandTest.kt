package com.robodesk.phonebridge

fun verifyBridgeCompanionCommands() {
    check(BridgeCompanionCommand.intensity(0)!!.contentEquals(byteArrayOf(1, 0)))
    check(BridgeCompanionCommand.intensity(2)!!.contentEquals(byteArrayOf(1, 2)))
    check(BridgeCompanionCommand.intensity(3) == null)
    check(BridgeCompanionCommand.activities.size == 13)
    check(BridgeCompanionCommand.startActivity(0)!!.contentEquals(byteArrayOf(2, 1)))
    check(BridgeCompanionCommand.startActivity(12)!!.contentEquals(byteArrayOf(2, 13)))
    check(BridgeCompanionCommand.startActivity(13) == null)
    check(BridgeCompanionCommand.pauseActivity().contentEquals(byteArrayOf(3)))
    check(BridgeCompanionCommand.resumeActivity().contentEquals(byteArrayOf(4)))
    check(BridgeCompanionCommand.cancelActivity().contentEquals(byteArrayOf(5)))
    check(BridgeCompanionCommand.quietEnabled(true).contentEquals(byteArrayOf(6, 1)))
    check(BridgeCompanionCommand.quietHours(false, 1320, 420)!!.contentEquals(byteArrayOf(6, 0, 0x28, 0x05, 0xa4.toByte(), 0x01)))
    check(BridgeCompanionCommand.quietHours(true, 1440, 420) == null)
}
