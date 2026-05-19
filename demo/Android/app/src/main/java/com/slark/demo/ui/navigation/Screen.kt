//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.navigation

sealed class Screen(val route: String) {
    object VideoPickerScreen : Screen("pick_video")
    object LocalPlayerScreen : Screen("local_player")
    object SelectUrlScreen : Screen("select_url")
    object NetworkPlayerScreen : Screen("network_player")
    object AudioPlayerScreen : Screen("audio_player")
}
