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
import com.slark.api.PlayerErrorCode
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerEvent
import com.slark.api.SlarkPlayerObserver
import com.slark.api.SlarkPlayerState
import com.slark.api.SlarkRenderTarget
import com.slark.sdk.SlarkLog

class PlayerViewModel(private var player: SlarkPlayer?) : ViewModel() {
    var isPlaying by mutableStateOf(false)


    var currentTime by mutableStateOf(0.0)

    var totalTime by mutableStateOf(0.0)

    var cacheTime by mutableStateOf(0.0)

    var isLoading by mutableStateOf(false)

    private var _volume = mutableStateOf(100.0f)
    var volume: Float
        get() = _volume.value
        set(value) {
            player?.volume = value
            _volume.value = value
        }

    var isLoop by mutableStateOf(false)

    var isMute by mutableStateOf(false)

    var errorMessage by mutableStateOf<String?>(null)

    private fun setPlayState(isPlaying: Boolean, isLoading: Boolean) {
        this.isPlaying = isPlaying
        this.isLoading = isLoading
    }

    private val observer = object: SlarkPlayerObserver {
        override fun notifyTime(playerId: String, time: Double) {
            currentTime = time
        }

        override fun notifyState(playerId: String, state: SlarkPlayerState) {
            SlarkLog.d("PlayerViewModel", "Player state changed: $state")
            when (state) {
                SlarkPlayerState.Playing -> {
                    setPlayState(isPlaying = true, isLoading = false)
                }
                SlarkPlayerState.Buffering -> {
                    setPlayState(isPlaying = false, isLoading = true)
                }
                SlarkPlayerState.Pause,
                SlarkPlayerState.Stop,
                SlarkPlayerState.Completed -> {
                    setPlayState(isPlaying = false, isLoading = false)
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
                SlarkPlayerEvent.OnError -> {
                    SlarkLog.e("PlayerViewModel", "Player error: $value")
                    errorMessage = when (PlayerErrorCode.fromString(value)) {
                        PlayerErrorCode.OpenFileError -> "Failed to open file"
                        PlayerErrorCode.NetWorkError -> "Network error"
                        PlayerErrorCode.NotSupport -> "Format not supported"
                        PlayerErrorCode.DemuxError -> "Demuxing error"
                        PlayerErrorCode.DecodeError -> "Decoding error"
                        PlayerErrorCode.RenderError -> "Rendering error"
                        else -> "Unknown error"
                    }
                } else -> {}
            }
        }
    }

    init {
        player?.setObserver(observer)
    }

    fun seekTo(time: Double, isAccurate: Boolean) {
        player?.seekTo(time, isAccurate)
    }

    fun skipPrev() {
        // Implement skip previous logic
    }

    fun skipNext() {
        // Implement skip next logic
    }

    fun onBackground(isBackground: Boolean) {
        player?.onBackground(isBackground)
    }

    fun setRenderSize(width: Int, height: Int) {
        player?.setRenderSize(width, height)
    }

    fun loop(isLoop: Boolean) {
        player?.isLoop = isLoop
        this.isLoop = isLoop
    }

    fun mute(isMute: Boolean) {
        player?.isMute = isMute
        this.isMute = isMute
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

    fun release() {
        player?.release()
    }

    override fun onCleared() {
        super.onCleared()
        release()
    }
}
