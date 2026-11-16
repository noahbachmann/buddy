package buddy.screener.ime

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.inputmethodservice.InputMethodService
import android.os.Build
import android.util.Base64
import android.view.Gravity
import android.view.View
import android.view.inputmethod.ExtractedTextRequest
import android.view.inputmethod.InputConnection
import android.widget.TextView

/**
 * Keyboard the screener service types through, driven over adb broadcasts.
 *
 * The app's comment field hides its text from the accessibility tree, so `uiautomator` can't read it back; the
 * keyboard holds the field's InputConnection and can. It also replaces the stock keyboard, whose autocorrect and
 * suggestions changed typed text.
 *
 * Commands (`am broadcast -p buddy.screener.ime -a <action> [--es b64 <base64 UTF-8>]`):
 * - `buddy.screener.ime.TYPE`  insert `b64` at the cursor
 * - `buddy.screener.ime.CLEAR` delete the whole field
 * - `buddy.screener.ime.READ`  nothing, just report
 *
 * Every command answers with the field's full text afterwards: result 1 + data = base64 UTF-8, or result 0 + data =
 * an error word (`no_field`, `bad_b64`, `unreadable`). Only senders holding WRITE_SECURE_SETTINGS (adb shell, system)
 * can drive it; apps on the phone can't.
 */
class ScreenerIme : InputMethodService() {

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val (ok, data) = handle(intent)
            resultCode = if (ok) 1 else 0
            resultData = data
        }
    }

    override fun onCreate() {
        super.onCreate()
        val filter = IntentFilter().apply { listOf(TYPE, CLEAR, READ).forEach(::addAction) }
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(receiver, filter, SENDER_PERMISSION, null, Context.RECEIVER_EXPORTED)
        } else {
            registerReceiver(receiver, filter, SENDER_PERMISSION, null)
        }
    }

    override fun onDestroy() {
        unregisterReceiver(receiver)
        super.onDestroy()
    }

    // thin label instead of keys: covers as little of the app as possible, tells a human which keyboard is active
    override fun onCreateInputView(): View = TextView(this).apply {
        text = "Screener keyboard"
        gravity = Gravity.CENTER
        val pad = (6 * resources.displayMetrics.density).toInt()
        setPadding(pad, pad, pad, pad)
    }

    override fun onEvaluateFullscreenMode() = false

    private fun handle(intent: Intent): Pair<Boolean, String> {
        val ic = currentInputConnection ?: return false to "no_field"
        when (intent.action) {
            TYPE -> {
                val text = intent.getStringExtra("b64")?.let(::decode) ?: return false to "bad_b64"
                ic.commitText(text, 1)
            }
            CLEAR -> {
                val len = fieldText(ic)?.length ?: return false to "unreadable"
                ic.beginBatchEdit()
                ic.setSelection(0, len)
                ic.commitText("", 1)
                ic.endBatchEdit()
            }
        }
        // same connection, calls run in order: the read sees the edit above
        val now = fieldText(ic) ?: return false to "unreadable"
        return true to Base64.encodeToString(now.toByteArray(Charsets.UTF_8), Base64.NO_WRAP)
    }

    private fun fieldText(ic: InputConnection): String? {
        ic.getExtractedText(ExtractedTextRequest().apply { hintMaxChars = MAX_CHARS }, 0)?.text?.let {
            return it.toString()
        }
        // editors without extracted-text support: stitch around the cursor
        val before = ic.getTextBeforeCursor(MAX_CHARS, 0) ?: return null
        val selected = ic.getSelectedText(0) ?: ""
        val after = ic.getTextAfterCursor(MAX_CHARS, 0) ?: ""
        return "$before$selected$after"
    }

    private fun decode(b64: String): String? =
        runCatching { String(Base64.decode(b64, Base64.DEFAULT), Charsets.UTF_8) }.getOrNull()

    private companion object {
        const val TYPE = "buddy.screener.ime.TYPE"
        const val CLEAR = "buddy.screener.ime.CLEAR"
        const val READ = "buddy.screener.ime.READ"
        const val SENDER_PERMISSION = "android.permission.WRITE_SECURE_SETTINGS"
        const val MAX_CHARS = 100_000
    }
}
