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
