package com.james.epaperwatch.ui

import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.OpenableColumns
import android.util.Log
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import com.james.epaperwatch.WatchLinkService
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.data.WatchState
import com.james.epaperwatch.ota.FirmwareImage
import com.james.epaperwatch.ota.OtaState
import com.james.epaperwatch.ota.OtaState.Phase
import java.util.Locale

/**
 * "Update watch firmware": pick a `.bin`, check it, and send it to the watch over Bluetooth.
 *
 * This screen only picks the file and renders [OtaState]. The transfer runs in
 * [WatchLinkService], so it survives rotation, leaving the screen and the display turning off.
 */
class FirmwareUpdateActivity : Activity() {
    private lateinit var ui: Ui
    private lateinit var prefs: Prefs

    private lateinit var firmwareValue: TextView
    private lateinit var linkValue: TextView
    private lateinit var pickButton: Button
    private lateinit var fileInfo: TextView
    private lateinit var sendButton: Button
    private lateinit var progressBar: ProgressBar
    private lateinit var progressText: TextView
    private lateinit var statusText: TextView
    private lateinit var cancelButton: Button

    private val stateListener: () -> Unit = { render() }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ui = Ui(this)
        prefs = Prefs(this)
        window.statusBarColor = Ui.PAPER
        window.navigationBarColor = Ui.PAPER
        setContentView(build())
    }

    override fun onStart() {
        super.onStart()
        WatchState.addListener(stateListener)
        render()
    }

    override fun onStop() {
        WatchState.removeListener(stateListener)
        super.onStop()
    }

    @Deprecated("Platform Activity result API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode == REQ_PICK && resultCode == RESULT_OK && uri != null) load(uri)
    }

    // ------------------------------------------------------------------ actions

    private fun pick() {
        if (OtaState.busy) return
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*") // .bin has no dependable MIME type
        try {
            @Suppress("DEPRECATION")
            startActivityForResult(intent, REQ_PICK)
        } catch (e: Exception) {
            toast("No file picker available")
        }
    }

    /** Read and validate off the main thread; the result goes to [OtaState]. */
    private fun load(uri: Uri) {
        val app = applicationContext
        OtaState.setLoading(true)
        Thread {
            val result = try {
                val name = displayName(app, uri)
                val bytes = app.contentResolver.openInputStream(uri)?.use { input ->
                    // One byte more than the largest valid file is enough to know it is too big.
                    val buffer = ByteArray(FirmwareImage.MAX_FILE_BYTES + 1)
                    var total = 0
                    while (total < buffer.size) {
                        val n = input.read(buffer, total, buffer.size - total)
                        if (n < 0) break
                        total += n
                    }
                    buffer.copyOf(total)
                }
                when {
                    bytes == null -> FirmwareImage.Result.Invalid("Could not open the file.")
                    bytes.size > FirmwareImage.MAX_FILE_BYTES ->
                        FirmwareImage.Result.Invalid("The file is too large to be a watch firmware image.")
                    else -> FirmwareImage.parse(name, bytes)
                }
            } catch (e: Exception) {
                Log.w(TAG, "Could not read firmware file", e)
                FirmwareImage.Result.Invalid("Could not read the file: ${e.message ?: e.javaClass.simpleName}")
            }
            Handler(Looper.getMainLooper()).post {
                when (result) {
                    is FirmwareImage.Result.Ok -> OtaState.select(result.image, null)
                    is FirmwareImage.Result.Invalid -> OtaState.select(null, result.reason)
                }
            }
        }.start()
    }

    private fun displayName(context: Context, uri: Uri): String {
        try {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { cursor ->
                    if (cursor.moveToFirst()) cursor.getString(0)?.let { return it }
                }
        } catch (e: Exception) {
            Log.w(TAG, "No display name", e)
        }
        return uri.lastPathSegment?.substringAfterLast('/') ?: "firmware.bin"
    }

    private fun confirmSend() {
        val image = OtaState.selected ?: return
        AlertDialog.Builder(this)
            .setTitle("Update the watch?")
            .setMessage(
                "Send ${image.name} (${"%,d".format(image.size)} bytes) to the watch.\n\n" +
                    "You then have to accept the update on the watch itself. Keep the watch " +
                    "next to the phone until it has restarted. If anything goes wrong the " +
                    "current firmware stays, and USB DFU remains available.",
            )
            .setPositiveButton("Send") { _, _ -> send(image) }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun send(image: FirmwareImage) {
        val service = WatchLinkService.instance
        val error = if (service == null) "Watch is not connected" else service.startOta(image)
        if (error != null) toast(error)
        render()
    }

    private fun cancelOrDismiss() {
        WatchLinkService.instance?.cancelOta() ?: OtaState.dismiss()
        render()
    }

    // ------------------------------------------------------------------ rendering

    private fun render() {
        val service = WatchLinkService.instance
        val connected = service?.sessionActive == true
        val supported = service?.otaSupported == true
        val phase = OtaState.phase
        val busy = OtaState.busy
        val image = OtaState.selected

        firmwareValue.text = prefs.firmware?.ifEmpty { null } ?: "—"
        linkValue.text = when {
            !connected -> "Not connected"
            supported -> "Ready for updates"
            else -> "No Bluetooth update"
        }

        pickButton.isEnabled = !busy && !OtaState.loadingFile
        val error = OtaState.selectionError
        fileInfo.setTextColor(if (error != null && image == null) Ui.ALERT else Ui.INK)
        fileInfo.text = when {
            OtaState.loadingFile -> "Reading the file…"
            image != null -> buildString {
                append(image.name).append('\n')
                append("%,d".format(image.size)).append(" bytes · CRC-32 ").append(image.crcHex)
                if (image.suffixStripped) append("\nDFU suffix removed (16 bytes)")
            }
            error != null -> error
            else -> "No file chosen. Use the firmware.bin of an application build."
        }

        sendButton.isEnabled = image != null && supported && !busy
        sendButton.alpha = if (sendButton.isEnabled) 1f else 0.4f
        sendButton.text = when {
            image == null -> "Send to watch"
            !connected -> "Send to watch (not connected)"
            !supported -> "Send to watch (not supported by this watch)"
            else -> "Send to watch"
        }

        val showProgress = phase == Phase.SENDING || phase == Phase.VERIFYING || phase == Phase.INSTALLING
        progressBar.visibility = if (showProgress) View.VISIBLE else View.GONE
        progressText.visibility = if (showProgress) View.VISIBLE else View.GONE
        if (showProgress) {
            val total = OtaState.totalBytes.coerceAtLeast(1)
            val sent = OtaState.sentBytes.coerceIn(0, total)
            progressBar.isIndeterminate = phase != Phase.SENDING
            progressBar.max = total
            progressBar.progress = sent
            progressText.text = if (phase == Phase.SENDING) progressLine(sent, total) else ""
        }

        statusText.setTextColor(if (phase == Phase.FAILED) Ui.ALERT else Ui.INK)
        statusText.text = when (phase) {
            Phase.IDLE -> when {
                !connected -> "Connect the watch to update it."
                !supported ->
                    "This watch does not offer updates over Bluetooth. It needs the " +
                        "resident-bootloader firmware build and a working external flash; " +
                        "otherwise update over USB DFU."
                else -> ""
            }
            Phase.DONE, Phase.FAILED ->
                OtaState.message + "\nWatch firmware: " + (prefs.firmware?.ifEmpty { null } ?: "unknown")
            else -> OtaState.message
        }

        cancelButton.visibility = when (phase) {
            Phase.IDLE, Phase.INSTALLING -> View.GONE
            else -> View.VISIBLE
        }
        cancelButton.text = if (phase == Phase.DONE || phase == Phase.FAILED) "Dismiss" else "Cancel update"

        // The service holds a CPU wake lock for the transfer; while this screen is in front,
        // keep the display on too so the progress stays visible.
        if (busy) {
            window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        } else {
            window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        }
    }

    private fun progressLine(sent: Int, total: Int): String {
        val percent = sent * 100L / total
        val speed = OtaState.bytesPerSecond
        val line = StringBuilder()
        line.append("%,d / %,d bytes (%d%%)".format(sent, total, percent))
        if (speed > 0) {
            line.append(String.format(Locale.US, " · %.1f kB/s", speed / 1000.0))
            val seconds = (total - sent) / speed
            line.append(
                if (seconds >= 60) " · about ${seconds / 60} min ${seconds % 60} s left"
                else " · about $seconds s left",
            )
        }
        if (OtaState.chunkBytes > 0) line.append("\n${OtaState.chunkBytes}-byte chunks")
        return line.toString()
    }

    private fun toast(message: String) = Toast.makeText(this, message, Toast.LENGTH_LONG).show()

    // ------------------------------------------------------------------ layout

    private fun build(): View {
        val root = ui.column().apply {
            setPadding(ui.dp(22), ui.dp(24), ui.dp(22), ui.dp(40))
            setBackgroundColor(Ui.PAPER)
        }
        root.addView(ui.eyebrow("WATCH"))
        root.addView(ui.label("Firmware update", 30).apply {
            setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            setPadding(0, ui.dp(4), 0, ui.dp(8))
        })
        root.addView(ui.note(
            "Sends a firmware image to the watch over Bluetooth. The watch stores it in its " +
                "external flash, checks it, and its bootloader installs it after a restart. " +
                "The update has to be accepted on the watch.",
        ))

        val details = ui.column().apply { setPadding(0, ui.dp(8), 0, ui.dp(8)) }
        firmwareValue = ui.keyValue(details, "Current firmware")
        linkValue = ui.keyValue(details, "Watch")
        root.addView(details)

        pickButton = ui.button("Choose firmware file (.bin)", primary = false) { pick() }
        root.addView(pickButton)
        fileInfo = ui.label("", 13, Ui.INK, mono = true).apply {
            setPadding(0, ui.dp(8), 0, ui.dp(8))
            setLineSpacing(0f, 1.2f)
        }
        root.addView(fileInfo)

        sendButton = ui.button("Send to watch") { confirmSend() }
        root.addView(sendButton)

        progressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply {
            layoutParams = LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(0, ui.dp(14), 0, ui.dp(4)) }
        }
        root.addView(progressBar)
        progressText = ui.label("", 12, Ui.MUTED, mono = true).apply { setLineSpacing(0f, 1.2f) }
        root.addView(progressText)
        statusText = ui.label("", 14).apply {
            setPadding(0, ui.dp(12), 0, ui.dp(8))
            setLineSpacing(0f, 1.2f)
        }
        root.addView(statusText)
        cancelButton = ui.button("Cancel update", primary = false) { cancelOrDismiss() }
        root.addView(cancelButton)

        return ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(Ui.PAPER)
            addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
    }

    private companion object {
        const val TAG = "FirmwareUpdate"
        const val REQ_PICK = 1
    }
}
