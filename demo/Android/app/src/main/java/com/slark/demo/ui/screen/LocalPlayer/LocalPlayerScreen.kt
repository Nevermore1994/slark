package com.slark.demo.ui.screen.LocalPlayer

import android.view.TextureView
import android.view.ViewGroup
import android.view.WindowManager
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.viewinterop.AndroidView
import com.slark.demo.ui.component.PlayerControls
import com.slark.demo.ui.model.PlayerViewModel

@Composable
fun LocalPlayerScreen(
    viewModel: PlayerViewModel,
    onBackClick: () -> Unit,
    onPickClick: () -> Unit
) {
    Scaffold(
        modifier = Modifier.fillMaxSize()
    ) { innerPadding->
        Column(
            modifier = Modifier.padding(innerPadding)
        ) {
            AndroidView(
                factory = { context ->
                    TextureView(context).apply {
                        layoutParams = ViewGroup.LayoutParams(
                            ViewGroup.LayoutParams.MATCH_PARENT,
                            ViewGroup.LayoutParams.MATCH_PARENT
                        )
                        viewModel.setRenderTarget(this)
                    }
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .background(Color.DarkGray)
            )

            PlayerControls(viewModel)
        }
    }

}
