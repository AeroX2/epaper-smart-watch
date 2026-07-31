package com.james.epaperwatch

import android.content.ComponentName
import android.content.Context
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState

class MediaBridge(
    context: Context,
    private val sendToWatch: (String) -> Unit,
) {
    private val manager = context.getSystemService(MediaSessionManager::class.java)
    private val listenerComponent = ComponentName(context, WatchNotificationListener::class.java)
    private var lastWireValue = ""

    fun publish(force: Boolean = false) {
        val controller = activeController() ?: return
        val state = controller.playbackState
        val metadata = controller.metadata
        val wire = WatchProtocol.music(
            playing = state?.state == PlaybackState.STATE_PLAYING,
            positionSeconds = (state?.position ?: 0L) / 1000L,
            durationSeconds =
                (metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L) / 1000L,
            title = metadata?.getText(MediaMetadata.METADATA_KEY_TITLE) ?: "No title",
            artist = metadata?.getText(MediaMetadata.METADATA_KEY_ARTIST)
                ?: metadata?.getText(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
                ?: controller.packageName,
        )
        if (force || wire != lastWireValue) {
            lastWireValue = wire
            sendToWatch(wire)
        }
    }

    fun handleWatchCommand(command: String) {
        val controls = activeController()?.transportControls ?: return
        when (command) {
            "MEDIA,PREVIOUS" -> controls.skipToPrevious()
            "MEDIA,NEXT" -> controls.skipToNext()
            "MEDIA,PLAY_PAUSE" -> {
                if (activeController()?.playbackState?.state == PlaybackState.STATE_PLAYING) {
                    controls.pause()
                } else {
                    controls.play()
                }
            }
        }
        publish(force = true)
    }

    private fun activeController(): MediaController? {
        return try {
            val sessions = manager.getActiveSessions(listenerComponent)
            sessions.firstOrNull {
                it.playbackState?.state == PlaybackState.STATE_PLAYING
            } ?: sessions.firstOrNull()
        } catch (_: SecurityException) {
            null
        }
    }
}
