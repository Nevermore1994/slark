package com.slark.demo.ui.model

import android.view.Surface
import android.view.SurfaceView
import android.view.TextureView
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Size
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerEvent
import com.slark.api.SlarkPlayerObserver
import com.slark.api.SlarkPlayerState
import com.slark.api.SlarkRenderTarget
import com.slark.sdk.SlarkLog

class PlayerViewModel(private var player: SlarkPlayer?) : ViewModel() {
    var isPlaying by mutableStateOf(false)
        //private set

    var currentTime by mutableStateOf(0.0)
       // private set

    var totalTime by mutableStateOf(0.0)
        //private set

    var cacheTime by mutableStateOf(0.0)
        //private set

    var volume by mutableStateOf(100.0f)

    private val observer = object: SlarkPlayerObserver {
        override fun notifyTime(playerId: String, time: Double) {
            currentTime = time
        }

        override fun notifyState(playerId: String, state: SlarkPlayerState) {
            SlarkLog.d("PlayerViewModel", "Player state changed: $state")
            when (state) {
                SlarkPlayerState.Playing -> {
                    isPlaying = true
                }
                SlarkPlayerState.Pause -> {
                    isPlaying = false
                }
                SlarkPlayerState.Stop -> {
                    isPlaying = false
                }
                SlarkPlayerState.Buffering -> {
                    isPlaying = false
                }
                SlarkPlayerState.Completed -> {
                    isPlaying = false
                }
                SlarkPlayerState.Prepared -> {
                    totalTime = player?.totalDuration() ?: 0.0
                }
                SlarkPlayerState.Error -> {
                    SlarkLog.e("PlayerViewModel", "Player error occurred")
                }
                else -> {}
            }
        }

        override fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
            SlarkLog.d("notifyEvent","$event $value")
            when (event) {
                SlarkPlayerEvent.FirstFrameRendered -> {
                }
                SlarkPlayerEvent.SeekDone -> {

                }
                SlarkPlayerEvent.UpdateCacheTime -> {
                    cacheTime = value.toDouble()
                }
                else -> {}
            }
        }
    }

    init {
        player?.setObserver(observer)
    }

    fun seekTo(time: Double) {
        player?.seekTo(time)
    }

    fun skipPrev() {
        // Implement skip previous logic
    }

    fun skipNext() {
        // Implement skip next logic
    }

    fun prepare() {
        player?.prepare()
    }

    fun play(playing : Boolean) {
        if (playing && !isPlaying) {
            player?.play()
        } else if (!playing && isPlaying) {
            player?.pause()
        }
    }

    fun setRenderTarget(textureView: TextureView) {
        player?.setRenderTarget(SlarkRenderTarget.FromTextureView(textureView))
    }

    fun setRenderTarget(surfaceView: SurfaceView) {
        player?.setRenderTarget(SlarkRenderTarget.FromSurfaceView(surfaceView))
    }

    fun setRenderTarget(surface: Surface, size: Size) {
        player?.setRenderTarget(SlarkRenderTarget.FromSurface(surface, size.width.toInt(), size.height.toInt()))
    }

    fun stop() {
        player?.stop()
    }

    fun release() {
        player?.stop()
        player?.release()
    }

    override fun onCleared() {
        super.onCleared()
        release()
    }
}
