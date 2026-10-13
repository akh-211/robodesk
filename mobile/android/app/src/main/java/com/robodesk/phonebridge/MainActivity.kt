package com.robodesk.phonebridge

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import com.robodesk.phonebridge.ui.RoboApp
import com.robodesk.phonebridge.ui.RoboRoot

/** Single Compose host (A3). All screens live in `ui/`; the link itself is owned by [RobotLinkService]. */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        RoboApp.init(this)
        setContent { RoboRoot() }
    }

    override fun onResume() {
        super.onResume()
        RobotLinkHub.appForeground = true
        RoboApp.foreground = true
        RoboApp.resumeTick++
        RoboApp.hasRobot = TrustedRobotStore.address(this) != null
        if (RoboApp.hasRobot) RobotLinkService.start(this)
    }

    override fun onPause() {
        RobotLinkHub.appForeground = false
        RoboApp.foreground = false
        super.onPause()
    }
}
