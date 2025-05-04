package com.slark.demo

sealed class Screen(val route: String) {
    object HomeScreen: Screen("home")
    object LocalPlayerScreen : Screen("local_player")

    companion object {
        const val ROUTE_PREFIX = "slark://"
        fun getFullRoute(route: String) = ROUTE_PREFIX + route
    }
}
