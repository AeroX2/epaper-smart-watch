package com.james.epaperwatch.ota

import com.james.epaperwatch.data.WatchState

/**
 * Process-wide, observable state of the firmware update: the picked image and the running
 * transfer. The transfer itself lives in the link service ([OtaManager]); the update screen
 * only renders this, so rotation or the screen turning off cannot disturb a transfer.
 * Changes are announced through [WatchState.notifyChanged].
 */
object OtaState {
    enum class Phase {
        IDLE,
        /** OTA_BEGIN sent, not yet acknowledged. */
        REQUESTING,
        /** Waiting for the wearer to accept on the watch. */
        WAITING_CONFIRM,
        SENDING,
        /** OTA_END sent; the watch checks size and CRC of its staged copy. */
        VERIFYING,
        /** The watch reboots and its bootloader installs the image. */
        INSTALLING,
        DONE,
        FAILED,
    }

    @Volatile var phase: Phase = Phase.IDLE
        private set
    @Volatile var message: String = ""
        private set
    @Volatile var sentBytes: Int = 0
        private set
    @Volatile var totalBytes: Int = 0
        private set
    @Volatile var bytesPerSecond: Int = 0
        private set
    @Volatile var chunkBytes: Int = 0
        private set

    /** The image picked on the update screen (kept across rotation). */
    @Volatile var selected: FirmwareImage? = null
        private set
    @Volatile var selectionError: String? = null
        private set
    @Volatile var loadingFile: Boolean = false
        private set

    /** A transfer or install is under way. */
    val busy: Boolean
        get() = when (phase) {
            Phase.IDLE, Phase.DONE, Phase.FAILED -> false
            else -> true
        }

    fun setPhase(phase: Phase, message: String) {
        this.phase = phase
        this.message = message
        WatchState.notifyChanged()
    }

    fun setProgress(sent: Int, total: Int, bytesPerSecond: Int, chunk: Int) {
        sentBytes = sent
        totalBytes = total
        this.bytesPerSecond = bytesPerSecond
        chunkBytes = chunk
        WatchState.notifyChanged()
    }

    fun setLoading(loading: Boolean) {
        loadingFile = loading
        WatchState.notifyChanged()
    }

    /** Clear a finished result (used when the link service is not running). */
    fun dismiss() {
        if (!busy) setPhase(Phase.IDLE, "")
    }

    fun select(image: FirmwareImage?, error: String?) {
        selected = image
        selectionError = error
        loadingFile = false
        WatchState.notifyChanged()
    }
}
