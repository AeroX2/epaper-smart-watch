package com.james.epaperwatch.ui

import android.Manifest
import android.content.ContentValues
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import com.james.epaperwatch.bridge.CameraRemote
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Camera screen driven by the watch's Camera app (see [CameraRemote]). Shows the back camera,
 * takes a photo on CAMERA|SHUTTER (after an optional countdown with beeps) and saves it to
 * Pictures/E-Paper Watch. It opens over the lock screen, so the phone can be propped up
 * locked; the photos are still saved to the gallery.
 */
class CameraActivity : ComponentActivity() {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var preview: PreviewView
    private lateinit var status: TextView
    private lateinit var countdownText: TextView
    private lateinit var flash: View

    private var capture: ImageCapture? = null
    private var ready = false
    private var countdown = 0
    private var photos = 0
    private var error: String? = null
    private var closedByWatch = false
    private var beeper: ToneGenerator? = null

    private val tick = object : Runnable {
        override fun run() {
            countdown--
            if (countdown <= 0) {
                countdownText.visibility = View.GONE
                takePhoto()
            } else {
                countdownText.text = countdown.toString()
                beep(short = true)
                handler.postDelayed(this, 1_000L)
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (Build.VERSION.SDK_INT >= 27) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        } else {
            @Suppress("DEPRECATION")
            window.addFlags(
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED or
                    WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON,
            )
        }
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        window.statusBarColor = Color.BLACK
        window.navigationBarColor = Color.BLACK
        setContentView(build())
        CameraRemote.attach(this)
        CameraRemote.report("OPENING")

        if (checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            setStatus("Allow the camera so the watch can take photos")
            requestPermissions(arrayOf(Manifest.permission.CAMERA), REQ_CAMERA)
        }
    }

    override fun onDestroy() {
        handler.removeCallbacksAndMessages(null)
        beeper?.release()
        beeper = null
        CameraRemote.detach(this)
        if (!closedByWatch) CameraRemote.report("CLOSED")
        super.onDestroy()
    }

    @Deprecated("Platform permission API")
    override fun onRequestPermissionsResult(
        requestCode: Int, permissions: Array<out String>, grantResults: IntArray,
    ) {
        @Suppress("DEPRECATION")
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQ_CAMERA) return
        if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) {
            startCamera()
        } else {
            fail("NO PERMISSION", "Camera permission was not given")
        }
    }

    // ---- driven by CameraRemote

    /** Tell the watch where things stand (it reopened the app). */
    fun reportState() {
        when {
            error != null -> CameraRemote.report("ERROR", detail = error!!)
            countdown > 0 -> CameraRemote.report("COUNTDOWN", countdown)
            !ready -> CameraRemote.report("OPENING")
            photos > 0 -> CameraRemote.report("SAVED", photos)
            else -> CameraRemote.report("READY")
        }
    }

    fun shutter(delaySeconds: Int) {
        if (!ready || countdown > 0) {
            reportState()
            return
        }
        if (delaySeconds <= 0) {
            takePhoto()
            return
        }
        countdown = delaySeconds
        countdownText.text = countdown.toString()
        countdownText.visibility = View.VISIBLE
        beep(short = true)
        CameraRemote.report("COUNTDOWN", countdown)
        handler.postDelayed(tick, 1_000L)
    }

    fun closeFromWatch() {
        closedByWatch = true
        finish()
    }

    // ---- camera

    private fun startCamera() {
        setStatus("Starting the camera…")
        val future = ProcessCameraProvider.getInstance(this)
        future.addListener({
            try {
                val provider = future.get()
                val previewUse = Preview.Builder().build().also {
                    it.setSurfaceProvider(preview.surfaceProvider)
                }
                val captureUse = ImageCapture.Builder()
                    .setCaptureMode(ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY)
                    .build()
                provider.unbindAll()
                provider.bindToLifecycle(
                    this, CameraSelector.DEFAULT_BACK_CAMERA, previewUse, captureUse,
                )
                capture = captureUse
                ready = true
                error = null
                setStatus("Press select on the watch to take a photo")
                CameraRemote.report("READY")
            } catch (e: Exception) {
                Log.w(TAG, "Camera start failed", e)
                fail("CAMERA BUSY", "The camera could not be started")
            }
        }, mainExecutor)
    }

    private fun takePhoto() {
        val use = capture ?: return
        countdown = 0
        val name = "EPW_" + SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val values = ContentValues().apply {
            put(MediaStore.MediaColumns.DISPLAY_NAME, name)
            put(MediaStore.MediaColumns.MIME_TYPE, "image/jpeg")
            if (Build.VERSION.SDK_INT >= 29) {
                put(MediaStore.MediaColumns.RELATIVE_PATH, "Pictures/E-Paper Watch")
            }
        }
        val options = ImageCapture.OutputFileOptions.Builder(
            contentResolver, MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values,
        ).build()
        beep(short = false)
        flash()
        use.takePicture(options, mainExecutor, object : ImageCapture.OnImageSavedCallback {
            override fun onImageSaved(output: ImageCapture.OutputFileResults) {
                photos++
                setStatus("Saved $photos photo" + (if (photos == 1) "" else "s") +
                    " to Pictures/E-Paper Watch")
                CameraRemote.report("SAVED", photos)
            }

            override fun onError(exception: ImageCaptureException) {
                Log.w(TAG, "Photo failed", exception)
                setStatus("The photo could not be saved")
                CameraRemote.report("ERROR", detail = "PHOTO FAILED")
                // The camera is still usable: let the next press try again.
                handler.postDelayed({ if (ready) CameraRemote.report("READY") }, 2_000L)
            }
        })
    }

    private fun fail(code: String, message: String) {
        ready = false
        error = code
        setStatus(message)
        CameraRemote.report("ERROR", detail = code)
    }

    // ---- views

    private fun build(): View {
        val root = FrameLayout(this).apply { setBackgroundColor(Color.BLACK) }
        preview = PreviewView(this).apply {
            scaleType = PreviewView.ScaleType.FILL_CENTER
        }
        root.addView(preview, FrameLayout.LayoutParams(-1, -1))

        flash = View(this).apply {
            setBackgroundColor(Color.WHITE)
            alpha = 0f
        }
        root.addView(flash, FrameLayout.LayoutParams(-1, -1))

        countdownText = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 120f
            typeface = Typeface.DEFAULT_BOLD
            gravity = Gravity.CENTER
            setShadowLayer(12f, 0f, 0f, Color.BLACK)
            visibility = View.GONE
        }
        root.addView(countdownText, FrameLayout.LayoutParams(-1, -1))

        val bottom = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setBackgroundColor(0x99000000.toInt())
            val pad = dp(16)
            setPadding(pad, pad, pad, pad)
        }
        status = TextView(this).apply {
            setTextColor(Color.WHITE)
            textSize = 16f
            gravity = Gravity.CENTER
        }
        bottom.addView(status)
        val buttons = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }
        buttons.addView(Button(this).apply {
            text = "Take photo"
            setOnClickListener { shutter(0) }
        })
        buttons.addView(Button(this).apply {
            text = "Close"
            setOnClickListener { finish() }
        })
        bottom.addView(buttons, LinearLayout.LayoutParams(-1, -2).apply { topMargin = dp(8) })
        root.addView(
            bottom,
            FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM),
        )
        return root
    }

    private fun setStatus(text: String) {
        status.text = text
    }

    private fun flash() {
        flash.animate().cancel()
        flash.alpha = 0.8f
        flash.animate().alpha(0f).setDuration(250L).start()
    }

    private fun beep(short: Boolean) {
        try {
            val tones = beeper ?: ToneGenerator(AudioManager.STREAM_SYSTEM, 80).also { beeper = it }
            tones.startTone(
                if (short) ToneGenerator.TONE_PROP_BEEP else ToneGenerator.TONE_PROP_ACK,
                if (short) 120 else 200,
            )
        } catch (e: RuntimeException) {
            Log.w(TAG, "Beep failed", e)
        }
    }

    private fun dp(value: Int) = (value * resources.displayMetrics.density).toInt()

    private companion object {
        const val TAG = "CameraActivity"
        const val REQ_CAMERA = 31
    }
}
