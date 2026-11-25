
//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.screen.NetworkPlayer

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.NavHostController
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerFactory
import com.slark.demo.ui.component.PlayerScreen
import com.slark.demo.ui.model.DemoFlowConfig
import com.slark.demo.ui.model.PlayerViewModel
import com.slark.demo.ui.navigation.Screen
import com.slark.demo.ui.screen.LocalPlayer.SharedViewModel

@Composable
fun NetworkPlayerScreenRoute(navController: NavHostController, sharedViewModel: SharedViewModel) {
    val context = LocalContext.current
    var playerViewModel by remember { mutableStateOf<PlayerViewModel?>(null) }

    LaunchedEffect(sharedViewModel.uriVersion, DemoFlowConfig.version) {
        if (sharedViewModel.uriVersion == 0) return@LaunchedEffect
        val uri = sharedViewModel.firstUri ?: return@LaunchedEffect
        playerViewModel?.release()
        val config = SlarkPlayerConfig(uri.toString(), flowConfig = DemoFlowConfig.config)
        val slarkPlayer: SlarkPlayer? = SlarkPlayerFactory.createPlayer(config)
        playerViewModel = PlayerViewModel(slarkPlayer, context.applicationContext)
    }

    DisposableEffect(Unit) {
        onDispose {
            playerViewModel?.release()
            playerViewModel = null
        }
    }

    playerViewModel?.let { viewModel ->
        key(viewModel) {
            PlayerScreen(
                viewModel,
                onBackClick = { navController.popBackStack() },
                onConfigClick = { navController.navigate(Screen.FlowConfigScreen.route) }
            )
        }
    }
}
