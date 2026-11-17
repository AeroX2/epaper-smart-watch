package com.james.epaperwatch.data

import android.os.Handler
import android.os.Looper
import java.util.concurrent.CopyOnWriteArraySet

/**
 * Live, in-process connection state shared between the link service and the UI.
 * Persistent watch data (battery, history, ...) lives in [Prefs]; listeners are told to
 * re-read it via [notifyChanged].
 */
object WatchState {
    enum class Link(val label: String) {
        NOT_PAIRED("Not paired"),
        BLUETOOTH_OFF("Bluetooth off"),
        NO_PERMISSION("Permission needed"),
        WAITING("Waiting for watch"),
        CONNECTING("Connecting"),
        HANDSHAKE("Handshaking"),
        CONNECTED("Connected"),
        INCOMPATIBLE("Incompatible firmware"),
        PAIRING_FAILED("Pairing failed"),
    }

    @Volatile var link: Link = Link.NOT_PAIRED
        private set
    @Volatile var detail: String = ""
        private set

    /** Sleep movement tracking (a Sleep as Android session or the app's own test). */
    @Volatile var sleepTracking = false
        private set
    @Volatile var sleepSamples = 0
        private set
    /** Last movement sample from the watch in milligravity, -1 before the first. */
    @Volatile var sleepLastMg = -1
        private set

    fun sleepStarted() {
        sleepTracking = true
        sleepSamples = 0
        sleepLastMg = -1
        notifyChanged()
    }

    fun sleepStopped() {
        sleepTracking = false
        notifyChanged()
    }

    fun sleepSample(milligravity: Int) {
        sleepSamples++
        sleepLastMg = milligravity
        notifyChanged()
    }

    private val main = Handler(Looper.getMainLooper())
    private val listeners = CopyOnWriteArraySet<() -> Unit>()

    fun update(link: Link, detail: String = link.label) {
        this.link = link
        this.detail = detail
        notifyChanged()
    }

    fun notifyChanged() {
        main.post { listeners.forEach { it() } }
    }

    fun addListener(listener: () -> Unit) {
        listeners += listener
    }

    fun removeListener(listener: () -> Unit) {
        listeners -= listener
    }
}
