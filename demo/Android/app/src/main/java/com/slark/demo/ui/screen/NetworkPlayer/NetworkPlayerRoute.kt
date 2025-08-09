package com.slark.demo.ui.screen.NetworkPlayer

import androidx.activity.ComponentActivity
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.navigation.NavHostController
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerFactory
import com.slark.demo.ui.component.PlayerScreen
import com.slark.demo.ui.model.PlayerViewModel
import com.slark.demo.ui.navigation.Screen
import com.slark.demo.ui.screen.LocalPlayer.SharedViewModel

@Composable
fun NetworkPlayerScreenRoute(navController: NavHostController, sharedViewModel: SharedViewModel) {
    var playerViewModel by remember { mutableStateOf<PlayerViewModel?>(null) }

    LaunchedEffect(sharedViewModel.selectedVideoUris) {
        if (sharedViewModel.selectedVideoUris.isEmpty()) {
            return@LaunchedEffect
        }
        val config = SlarkPlayerConfig(sharedViewModel.selectedVideoUris[0].toString())
        val slarkPlayer: SlarkPlayer? = SlarkPlayerFactory.createPlayer(config)
        playerViewModel = PlayerViewModel(slarkPlayer)
    }

    DisposableEffect(Unit) {
        onDispose {
            playerViewModel?.let { viewModel ->
                viewModel.release()
                playerViewModel = null
            }
        }
    }

    playerViewModel?.let { playerViewModel ->
        PlayerScreen(
            playerViewModel,
            onBackClick = { navController.popBackStack() },
            onPickClick = { navController.navigate(Screen.SelectUrlScreen) }
        )
    }

}