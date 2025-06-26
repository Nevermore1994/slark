package com.slark.sdk

import android.graphics.SurfaceTexture
import android.util.Size
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.TextureView
import android.view.View
import com.slark.api.KtTimeRange
import com.slark.api.SlarkPlayer
import com.slark.api.SlarkPlayerConfig
import com.slark.api.SlarkPlayerObserver
import com.slark.api.SlarkPlayerState
import com.slark.api.SlarkRenderTarget
import kotlin.math.ceil


class SlarkPlayerImpl(val config: SlarkPlayerConfig, private val playerId: String): SlarkPlayer {
    private var playerObserver: SlarkPlayerObserver? = null
    private var renderThread: EGLRenderThread? = null
    private var renderView: View? = null
    private var isPlayingBeforeBackground = false

    override var isLoop: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setLoop(playerId, value)
        }

    override var isMute: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setMute(playerId, value)
        }

    override var volume: Float = 100.0f
        set(value) {
            field = value
            SlarkPlayerManager.setVolume(playerId, value)
        }

    override fun getPlayerId(): String {
        return playerId
    }

    override fun prepare() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PREPARE.ordinal)
    }

    override fun play() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PLAY.ordinal)
    }

    override fun pause() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.PAUSE.ordinal)
    }

    override fun stop() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.STOP.ordinal)
    }

    override fun release() {
        SlarkPlayerManager.doAction(playerId, SlarkPlayerManager.Action.RELEASE.ordinal)
        SlarkPlayerManager.removePlayer(playerId)
    }

    override fun seekTo(time: Double, isAccurate: Boolean) {
        val t = ceil(time * 1000) / 1000.0
        SlarkPlayerManager.seek(playerId, t, isAccurate)
    }

    override fun setObserver(observer: SlarkPlayerObserver?) {
        playerObserver = observer
    }

    override fun setRenderTarget(renderTarget: SlarkRenderTarget) {
        when (renderTarget) {
            is SlarkRenderTarget.FromSurface -> {
                onSurfaceAvailable(renderTarget.surface, renderTarget.width, renderTarget.height)
            }
            is SlarkRenderTarget.FromSurfaceView -> {
                setupSurfaceView(renderTarget.view)
            }

            is SlarkRenderTarget.FromTextureView -> {
                setupTextureView(renderTarget.view)
            }
        }
    }

    override fun setRenderSize(width: Int, height: Int) {
        renderThread?.setRenderSize(width, height)
    }

    override fun setRotation(rotation: Int) {
        renderThread?.setRotation(rotation)
    }

    override fun onBackground(isBackground: Boolean) {
        if (isBackground) {
            if (state() == SlarkPlayerState.Playing) {
                isPlayingBeforeBackground = true
                pause()
            } else {
                isPlayingBeforeBackground = false
            }
        } else {
            if (isPlayingBeforeBackground) {
                play()
            }
        }
    }

    override fun totalDuration(): Double {
        return SlarkPlayerManager.totalDuration(playerId)
    }

    override fun currentTime(): Double {
        return SlarkPlayerManager.currentPlayedTime(playerId)
    }

    fun playerId(): String {
        return playerId
    }

    fun state(): SlarkPlayerState {
        return SlarkPlayerManager.state(playerId)
    }

    fun observer(): SlarkPlayerObserver? {
        return playerObserver
    }

    fun requestRender(textureRenderId: Int, width: Int, height: Int) {
        renderThread?.render(textureRenderId, width, height)
    }

    private fun onSurfaceAvailable(surface: Surface, width: Int, height: Int) {
        renderThread?.shutdown()
        renderThread = null
        renderThread = EGLRenderThread(surface, width, height)
        renderThread?.playerId = playerId
        renderThread?.start()
    }

    private fun setupTextureView(textureView: TextureView) {
        renderView = textureView
        textureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
                onSurfaceAvailable(Surface(surface), width, height)
            }

            override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
                renderThread?.setRenderSize(width, height)
            }

            override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
                renderThread?.shutdown()
                renderThread = null
                return true
            }

            override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {}
        }
    }

    private fun setupSurfaceView(surfaceView: SurfaceView) {
        renderView = surfaceView
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                onSurfaceAvailable(holder.surface, holder.surfaceFrame.width(), holder.surfaceFrame.height())
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {}

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                renderThread?.shutdown()
                renderThread = null
            }
        })
    }

    companion object {
        const val LOG_TAG = "SlarkPlayerImpl"

        fun create(config: SlarkPlayerConfig): SlarkPlayerImpl {
            if (config.dataSource.isEmpty()) {
                throw IllegalArgumentException("dataSource is empty")
            }
            val (start, duration) = config.timeRange.get()
            val playerId = SlarkPlayerManager.createPlayer(config.dataSource, start, duration)
            val player = SlarkPlayerImpl(config, playerId)
            SlarkPlayerManager.addPlayer(playerId, player)
            return player
        }
    }
}