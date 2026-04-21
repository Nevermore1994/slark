
//
// Copyright (c) 2022-2026 Nevermore1994. All rights reserved.
//
// This file is part of Slark.
// Licensed under the MIT License.
//

package com.slark.demo.ui.navigation

sealed class Screen(val route: String) {
    object HomeScreen: Screen("home")
    object VideoPickerScreen : Screen("pick_video")
    object LocalPlayerScreen : Screen("local_player")
    object SelectUrlScreen : Screen("select_url")
    object NetworkPlayerScreen : Screen("network_player")

    companion object {
        const val ROUTE_PREFIX = "slark:"
        fun getFullRoute(route: String) = ROUTE_PREFIX + route
    }
}
