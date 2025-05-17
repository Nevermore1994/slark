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


class SlarkPlayerImpl: SlarkPlayer {
    private var playerObserver: SlarkPlayerObserver? = null
    private var renderThread: EGLRenderThread? = null
    private lateinit var playerId: String
    private var renderView: View? = null

    var isLoop: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setLoop(playerId, value)
        }

    var isMute: Boolean = false
        set(value) {
            field = value
            SlarkPlayerManager.setMute(playerId, value)
        }

    var volume: Float = 100.0f
        set(value) {
            field = value
            SlarkPlayerManager.setVolume(playerId, value)
        }

    override fun setConfig(config: SlarkPlayerConfig): Boolean {
        if (config.dataSource.isEmpty()) {
            SlarkLog.e(LOG_TAG, "dataSource is empty")
            return false
        }
        val (start, duration) = config.timeRange.get()
        playerId = SlarkPlayerManager.createPlayer(config.dataSource, start, duration)
        SlarkPlayerManager.addPlayer(playerId, this)
        return true
    }

    override fun getPlayerId(): String {
        return playerId
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

    override fun seekTo(time: Double) {
        SlarkPlayerManager.seek(playerId, time)
    }

    override fun setObserver(observer: SlarkPlayerObserver) {
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

    fun setRenderSize(size: Size) {
        SlarkPlayerManager.setRenderSize(playerId, size.width, size.height)
    }

    fun totalDuration(): Double {
        return SlarkPlayerManager.totalDuration(playerId)
    }

    fun currentTime(): Double {
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

    fun requestRender(textureRenderId: Int) {
        renderThread?.render(textureRenderId)
    }

    private fun onSurfaceAvailable(surface: Surface, width: Int, height: Int) {
        renderThread?.shutdown()
        renderThread = null
        renderThread = EGLRenderThread(surface, width, height)
        renderThread?.start()
    }

    private fun setupTextureView(textureView: TextureView) {
        renderView = textureView
        textureView.surfaceTextureListener = object : TextureView.SurfaceTextureListener {
            override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
                onSurfaceAvailable(Surface(surface), width, height)
            }

            override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {}

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
    }
}