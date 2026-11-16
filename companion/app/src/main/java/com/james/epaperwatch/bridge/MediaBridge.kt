package com.james.epaperwatch.bridge

import android.content.ComponentName
import android.content.Context
import android.media.AudioManager
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Handler
import android.os.SystemClock
import android.view.KeyEvent
import com.james.epaperwatch.WatchNotificationListener
import com.james.epaperwatch.link.PowerPolicy
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import com.james.epaperwatch.protocol.WireText

/**
 * Event-driven media relay: an active-sessions listener plus a MediaController.Callback on each
 * session. [onChange] fires (debounced) when anything changes; [messageIfChanged] then decides
 * whether the watch needs a new MUSIC message. Requires notification-listener access.
 */
class MediaBridge(
    private val context: Context,
    private val handler: Handler,
    private val onChange: () -> Unit,
) {
    private data class Snapshot(
        val state: Int,
        val position: Long,
        val duration: Long,
        val title: String,
        val artist: String,
    )

    private val manager = context.getSystemService(MediaSessionManager::class.java)
    private val component = ComponentName(context, WatchNotificationListener::class.java)
    private var controllers: List<MediaController> = emptyList()
    private var started = false
    private var lastSent: Snapshot? = null
    private var lastSentAt = 0L
    /** When the last position-only MUSIC message (a seek or drift correction) went out. */
    private var lastPositionFixAt = 0L

    private val debounced = Runnable { onChange() }

    private val callback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) = changed()
        override fun onMetadataChanged(metadata: MediaMetadata?) = changed()
        override fun onSessionDestroyed() = changed()
    }

    private val sessionsListener = MediaSessionManager.OnActiveSessionsChangedListener { list ->
        bind(list ?: emptyList())
    }

    /** Starts listening; silently does nothing until notification access is granted. */
    fun start() {
        if (started) return
        try {
            manager.addOnActiveSessionsChangedListener(sessionsListener, component, handler)
            started = true
            bind(manager.getActiveSessions(component))
        } catch (_: SecurityException) {
            started = false
        }
    }

    fun stop() {
        handler.removeCallbacks(debounced)
        if (!started) return
        manager.removeOnActiveSessionsChangedListener(sessionsListener)
        controllers.forEach { it.unregisterCallback(callback) }
        controllers = emptyList()
        started = false
    }

    private fun bind(list: List<MediaController>) {
        controllers.forEach { it.unregisterCallback(callback) }
        controllers = list
        list.forEach { it.registerCallback(callback, handler) }
        changed()
    }

    /**
     * Players report in bursts (a track change is several metadata and state callbacks), and
     * some report their position every second: wait for the burst to end, then look once.
     */
    private fun changed() {
        handler.removeCallbacks(debounced)
        handler.postDelayed(debounced, DEBOUNCE_MS)
    }

    /** The current MUSIC message, recorded as sent (used at session start). */
    fun currentMessage(): OutMessage {
        val snap = snapshot()
        lastSent = snap
        lastSentAt = SystemClock.elapsedRealtime()
        return toMessage(snap)
    }

    /** The last MUSIC message did not reach the watch: forget what was "sent". */
    fun invalidate() {
        lastSent = null
    }

    /**
     * A MUSIC message if what the watch shows is now wrong, else null. A new track or play
     * state is reported at once; position-only corrections are rate-limited
     * ([PowerPolicy.musicSendDelayMs]) and, when held back, looked at again later.
     */
    fun messageIfChanged(): OutMessage? {
        val snap = snapshot()
        val prev = lastSent
        val now = SystemClock.elapsedRealtime()
        if (prev != null) {
            val same = prev.state == snap.state && prev.duration == snap.duration &&
                prev.title == snap.title && prev.artist == snap.artist
            val wait = PowerPolicy.musicSendDelayMs(
                sameTrackAndState = same,
                nothingPlaying = snap.state == 0,
                driftSeconds = snap.position - expectedPosition(prev, now),
                sincePositionFixMs = if (lastPositionFixAt == 0L) Long.MAX_VALUE else now - lastPositionFixAt,
            )
            if (wait == PowerPolicy.MUSIC_NOTHING) return null
            if (wait > 0L) {
                handler.removeCallbacks(debounced)
                handler.postDelayed(debounced, wait)
                return null
            }
            if (same) lastPositionFixAt = now
        }
        lastSent = snap
        lastSentAt = now
        return toMessage(snap)
    }

    /** Where the watch, which advances a playing track by itself, has counted to. */
    private fun expectedPosition(prev: Snapshot, now: Long): Long {
        if (prev.state != 1) return prev.position
        val counted = prev.position + (now - lastSentAt) / 1000L
        return if (prev.duration > 0) counted.coerceAtMost(prev.duration) else counted
    }

    fun handleAction(action: String) {
        val controller = activeController() ?: controllers.firstOrNull()
        if (controller == null) {
            if (action == "PLAY_PAUSE") dispatchMediaKey(KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE)
            return
        }
        val controls = controller.transportControls
        when (action) {
            "PREV" -> controls.skipToPrevious()
            "NEXT" -> controls.skipToNext()
            "PLAY_PAUSE" -> if (isPlaying(controller)) controls.pause() else controls.play()
        }
    }

    private fun dispatchMediaKey(code: Int) {
        val audio = context.getSystemService(AudioManager::class.java) ?: return
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, code))
        audio.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, code))
    }

    private fun toMessage(s: Snapshot): OutMessage =
        Out.music(s.state, s.position, s.duration, s.title, s.artist)

    private fun activeController(): MediaController? =
        controllers.firstOrNull { isPlaying(it) }
            ?: controllers.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PAUSED }

    private fun isPlaying(c: MediaController): Boolean = when (c.playbackState?.state) {
        PlaybackState.STATE_PLAYING, PlaybackState.STATE_BUFFERING,
        PlaybackState.STATE_FAST_FORWARDING, PlaybackState.STATE_REWINDING -> true
        else -> false
    }

    private fun snapshot(): Snapshot {
        val controller = activeController() ?: return Snapshot(0, 0, 0, "", "")
        val playback = controller.playbackState
        val metadata = controller.metadata
        val playing = isPlaying(controller)
        var positionMs = playback?.position ?: 0L
        if (playing && playback != null && playback.lastPositionUpdateTime > 0) {
            val delta = SystemClock.elapsedRealtime() - playback.lastPositionUpdateTime
            positionMs += (delta * playback.playbackSpeed).toLong()
        }
        val durationMs = metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L
        val duration = (durationMs / 1000L).coerceAtLeast(0)
        var position = (positionMs / 1000L).coerceAtLeast(0)
        if (duration > 0) position = position.coerceAtMost(duration)
        val title = metadata?.getText(MediaMetadata.METADATA_KEY_TITLE)
            ?: metadata?.getText(MediaMetadata.METADATA_KEY_DISPLAY_TITLE)
        val artist = metadata?.getText(MediaMetadata.METADATA_KEY_ARTIST)
            ?: metadata?.getText(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
            ?: metadata?.getText(MediaMetadata.METADATA_KEY_DISPLAY_SUBTITLE)
            ?: NotificationRelay.appLabel(context, controller.packageName)
        return Snapshot(
            state = if (playing) 1 else 2,
            position = position,
            duration = duration,
            title = WireText.sanitize(title, 24),
            artist = WireText.sanitize(artist, 24),
        )
    }

    companion object {
        private const val DEBOUNCE_MS = 500L
    }
}
