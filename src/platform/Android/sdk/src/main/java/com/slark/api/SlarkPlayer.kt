package com.slark.api

import com.slark.sdk.SlarkPlayerImpl

/**
 * SlarkPlayer interface that defines the methods for a media player.
 */

interface SlarkPlayer {
    /**
     * @param isLoop: value from true to false
     */
    var isLoop: Boolean

    /**
     * @param isMute: value from true to false
     */
    var isMute: Boolean

    /**
     * @param volume: value from 0.0 to 100.0
     */
    var volume: Float

    /**
     * Prepare the player with the given configuration.
     * This method should be called before any playback actions.
     */
    fun prepare()

    /**
     * Get the player ID.
     * This ID is used to identify the player instance in the system.
     */
    fun playerId(): String

    /**
     * Start playback of the media.
     * This method should be called after prepare() to start playing the media.
     */
    fun play()

    /**
     * Pause the playback of the media.
     * This method can be called to pause the media playback.
     */
    fun pause()

    /**
     * Stop the playback of the media.
     * This method stops the media playback and dispose media resources.
     */
    fun release()

    /**
     * @param time: seconds
     * @param isAccurate:  Accurate seek is only performed when the user releases the progress slider.
     */
    fun seekTo(time: Double, isAccurate: Boolean)

    /**
     * Set the observer for player events.
     * The observer will receive notifications about player state changes and events.
     */
    fun setObserver(observer: SlarkPlayerObserver?)

    /**
     * Set the render target for the player.
     * The render target is where the player will render the video frames.
     */
    fun setRenderTarget(renderTarget: SlarkRenderTarget)

    /**
     * Get the current state of the player.
     * This method returns the current state of the player, such as playing, paused, buffering, etc.
     */
    fun state(): SlarkPlayerState

    /**
     * Get the total duration of the media in seconds.
     * This method returns the total duration of the media being played by the player.
     */
    fun totalDuration(): Double

    /**
     * Get the current played time of the media in seconds.
     * This method returns the current played time of the media.
     */
    fun currentTime(): Double

    /**
     * Request a render update with the given texture ID and size.
     * This method is used to request a render update for the player.
     */
    fun setRenderSize(width: Int, height: Int)

    enum class Rotation {
        ROTATION_0,   // 0 degrees
        ROTATION_90,  // 90 degrees
        ROTATION_180, // 180 degrees
        ROTATION_270  // 270 degrees
    }
    /**
     * Set the rotation of the video.
     * The rotation can be 0, 90, 180, or 270 degrees.
     */
    fun setRotation(rotation: Rotation)

    /**
     * Handle background state changes.
     * This method is called when the player goes into the background or comes back to the foreground.
     * @param isBackground: true if the player is in the background, false if it is in the foreground.
     */
    fun onBackground(isBackground: Boolean)
}

object SlarkPlayerFactory {
    /**
     * Create a SlarkPlayer instance with the given configuration.
     * @param config: The configuration for the player.
     * @return A new instance of SlarkPlayer, or null if creation fails.
     */
    fun createPlayer(config: SlarkPlayerConfig): SlarkPlayer? {
        val player = SlarkPlayerImpl.create(config)
        return player
    }
}