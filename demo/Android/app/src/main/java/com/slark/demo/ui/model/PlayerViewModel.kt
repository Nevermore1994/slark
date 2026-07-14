//
// Copyright (c) 2026 Nevermore-. All rights reserved.
//

package com.slark.demo.ui.model

import android.view.Surface
import android.view.SurfaceView
import android.view.TextureView
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Size
import androidx.lifecycle.ViewModel
import com.slark.api.SlarkPlayerErrorCode
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerEvent
import com.slark.api.SlarkPlayerObserver
import com.slark.api.SlarkPlayerState
import com.slark.api.SlarkRenderTarget
import com.slark.sdk.GraphicsBackend
import com.slark.sdk.SlarkLog
import com.slark.sdk.SlarkPlayerImpl

class PlayerViewModel(private var player: SlarkPlayer?) : ViewModel() {
    var isPlaying by mutableStateOf(false)
    var currentTime by mutableStateOf(0.0)
    var totalTime by mutableStateOf(0.0)
    var cacheTime by mutableStateOf(0.0)
    var isLoading by mutableStateOf(false)
    var isLoop by mutableStateOf(false)
    var isMute by mutableStateOf(false)
    private var _speed = mutableStateOf(1.0f)
    var speed: Float
        get() = _speed.value
        set(value) {
            // TODO: SlarkPlayer SDK 暂未暴露 speed 属性，待支持后在此设置
            _speed.value = value
        }
    var errorMessage by mutableStateOf<String?>(null)
    var rotationDegrees by mutableIntStateOf(0)
    var graphicsBackend by mutableStateOf(GraphicsBackend.OpenGL)
    var hasVideo by mutableStateOf(false)

    private var _volume = mutableStateOf(100.0f)
    var volume: Float
        get() = _volume.value
        set(value) {
            player?.volume = value
            _volume.value = value
        }

    private fun setPlayState(isPlaying: Boolean, isLoading: Boolean) {
        this.isPlaying = isPlaying
        this.isLoading = isLoading
    }

    private val observer = object : SlarkPlayerObserver {
        override fun notifyTime(playerId: String, time: Double) {
            currentTime = time
        }

        override fun notifyState(playerId: String, state: SlarkPlayerState) {
            SlarkLog.d("PlayerViewModel", "Player state changed: $state")
            when (state) {
                SlarkPlayerState.Playing -> setPlayState(isPlaying = true, isLoading = false)
                SlarkPlayerState.Buffering -> setPlayState(isPlaying = false, isLoading = true)
                SlarkPlayerState.Pause,
                SlarkPlayerState.Stop,
                SlarkPlayerState.Completed -> setPlayState(isPlaying = false, isLoading = false)
                SlarkPlayerState.Prepared -> {
                    totalTime = player?.totalDuration() ?: 0.0
                    hasVideo = player?.hasVideo ?: false
                }
                SlarkPlayerState.Error -> SlarkLog.e("PlayerViewModel", "Player error occurred")
                else -> {}
            }
        }

        override fun notifyEvent(playerId: String, event: SlarkPlayerEvent, value: String) {
            SlarkLog.d("notifyEvent", "$event $value")
            when (event) {
                SlarkPlayerEvent.UpdateCacheTime -> cacheTime = value.toDouble()
                SlarkPlayerEvent.OnError -> {
                    SlarkLog.e("PlayerViewModel", "Player error: $value")
                    errorMessage = when (SlarkPlayerErrorCode.entries.firstOrNull { it.value == value.toIntOrNull() }) {
                        SlarkPlayerErrorCode.MediaOpenFailed,
                        SlarkPlayerErrorCode.PipelineOpenFailed -> "Failed to open file"
                        SlarkPlayerErrorCode.NetworkError,
                        SlarkPlayerErrorCode.ConnectionFailed,
                        SlarkPlayerErrorCode.ConnectionTimeout -> "Network error"
                        SlarkPlayerErrorCode.PipelineFormatNotSupported,
                        SlarkPlayerErrorCode.CodecUnsupportedFormat -> "Format not supported"
                        SlarkPlayerErrorCode.PipelineDemuxError -> "Demuxing error"
                        SlarkPlayerErrorCode.PipelineDecodeError -> "Decoding error"
                        SlarkPlayerErrorCode.PipelineRenderError -> "Rendering error"
                        else -> "Unknown error"
                    }
                }
                else -> {}
            }
        }
    }

    init {
        player?.setObserver(observer)
    }

    fun seekTo(time: Double, isAccurate: Boolean) {
        player?.seekTo(time, isAccurate)
    }

    fun skipPrev() {}

    fun skipNext() {}

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

    fun rotate() {
        rotationDegrees = (rotationDegrees + 90) % 360
        val rotation = when (rotationDegrees) {
            90  -> SlarkPlayer.Rotation.ROTATION_90
            180 -> SlarkPlayer.Rotation.ROTATION_180
            270 -> SlarkPlayer.Rotation.ROTATION_270
            else -> SlarkPlayer.Rotation.ROTATION_0
        }
        player?.setRotation(rotation)
    }

    fun switchRenderBackend() {
        SlarkLog.i("PlayerViewModel", "switchRenderBackend: {} -> {}", graphicsBackend, if (graphicsBackend == GraphicsBackend.OpenGL) "Vulkan" else "OpenGL")
        val next = if (graphicsBackend == GraphicsBackend.OpenGL) GraphicsBackend.Vulkan
                   else GraphicsBackend.OpenGL
        (player as? SlarkPlayerImpl)?.switchGraphicsBackend(next)
        graphicsBackend = next
    }

    fun prepare() {
        player?.prepare()
    }

    fun play(playing: Boolean) {
        if (playing && !isPlaying) {
            player?.play()
        } else if (!playing && isPlaying) {
            player?.pause()
        }
    }

    private var currentRenderTarget: SlarkRenderTarget? = null

    fun setRenderTarget(target: SlarkRenderTarget) {
        currentRenderTarget = target
        player?.setRenderTarget(target)
    }

    fun setRenderTarget(textureView: TextureView) {
        setRenderTarget(SlarkRenderTarget.FromTextureView(textureView))
    }

    fun setRenderTarget(surfaceView: SurfaceView) {
        setRenderTarget(SlarkRenderTarget.FromSurfaceView(surfaceView))
    }

    fun setRenderTarget(surface: Surface, size: Size) {
        setRenderTarget(
            SlarkRenderTarget.FromSurface(surface, size.width.toInt(), size.height.toInt())
        )
    }

    fun release() {
        player?.release()
    }

    override fun onCleared() {
        super.onCleared()
        release()
    }
}
