package org.yfmredecomp.game;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * The first run's document picker (Platform_SelectDisc,
 * src/pc/platform/android.c), and the box that shows the disc image's copy.
 *
 * <p>SDL's file dialog keeps what it waits for in statics of the game's
 * process: when the system ends that process while the picker is in front
 * (it often does on a phone short of memory; the game's process is then a
 * background one), the player's choice comes back to a new process that no
 * longer waits for it, and is dropped: the welcome comes back as if nothing
 * had been chosen. This activity asks for the document itself, in a task of
 * its own (the game's activity is singleInstance), and writes the answer to
 * a small file in the internal files folder, {@link #RECORD}, before it
 * ends. The system keeps an activity's pending result across the end of
 * its process and gives it to the activity it makes again, so the answer is
 * written whichever process receives it; the game reads the file while it
 * waits, or at its next start when the process was ended in between.
 *
 * <p>The record is two lines: "chosen", "cancelled" or "failed" and the
 * pick's id (the Intent's "id" extra, so that the game takes only the
 * answer to the pick it waits for), then the document's URI or the reason.
 * Ended without an answer (its task closed, a free-form window shut, a
 * newer picker clearing this one), it answers "cancelled": where the game's
 * activity never left the screen, nothing else would tell the game the
 * picker is gone. The read right is taken as a lasting one
 * (takePersistableUriPermission), since the process that copies the image
 * may not be the one the result was given to; the game gives it back after
 * the copy ({@link #release}), and any left over at its next start
 * ({@link #releaseAll}).
 *
 * <p>The static methods are called by android.c through JNI, on the game's
 * own thread, never the main one.
 */
public class DiscPicker extends Activity {
    private static final String TAG = "memories";
    /** The answer's file, in getFilesDir(); android.c reads and removes it. */
    public static final String RECORD = "disc-pick.txt";
    private static final int PICK = 1;
    private static final String ASKED = "asked";

    private boolean asked;
    private boolean answered;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        // Made again after the system ended the process, or for a
        // configuration change it did not take: the picker is up already
        // and its answer is on its way to onActivityResult.
        asked = state != null && state.getBoolean(ASKED);
        if (asked)
            return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        // Any document: the system knows no type for a .bin, and the image
        // is checked by what it holds.
        intent.setType("*/*");
        try {
            startActivityForResult(intent, PICK);
            asked = true;
        } catch (ActivityNotFoundException e) {
            Log.e(TAG, "memories-pc: disc picker: no document picker: " + e);
            answer("failed", "This device has no app to choose a file with.");
        } catch (RuntimeException e) {
            Log.e(TAG, "memories-pc: disc picker: the document picker could not be opened: " + e);
            answer("failed", "The file picker could not be opened.");
        }
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBoolean(ASKED, asked);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        // A picker cleared by a new one (the game starts this activity with
        // FLAG_ACTIVITY_CLEAR_TASK) or closed with its task: onDestroy says
        // so, under this pick's id.
        if (request != PICK || isFinishing())
            return;
        Uri uri = data != null ? data.getData() : null;
        Log.i(TAG, "memories-pc: disc picker: result " + result + (data == null ? ", no data" : "")
                + (uri == null ? ", no document" : ", " + uri.toString().length() + " characters"));
        if (result != RESULT_OK) {
            // Back, or the picker closed without a choice: the welcome again.
            answer("cancelled", "result " + result);
            return;
        }
        if (uri == null) {
            answer("failed", "The picker gave back no document.");
            return;
        }
        try {
            getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (RuntimeException e) {
            // Not offered for this document (the system's picker offers it
            // for every document it hands back to ACTION_OPEN_DOCUMENT): the
            // right given with the result ends with this activity, and the
            // game says the file could not be opened if it is gone by then.
            Log.i(TAG, "memories-pc: disc picker: no lasting read right for the document (" + e + ")");
        }
        answer("chosen", uri.toString());
    }

    @Override
    protected void onDestroy() {
        // Ended for good with no answer written: the picker's task closed,
        // its window shut, or a newer picker cleared it. Not when the system
        // only takes the activity down to make it again (a configuration
        // change; or its process ended, where onDestroy does not run): its
        // answer still comes then.
        if (!answered && isFinishing() && !isChangingConfigurations()) {
            Log.i(TAG, "memories-pc: disc picker: closed without a choice");
            write("cancelled", "the picker closed without a choice");
        }
        super.onDestroy();
    }

    /** Writes the record and ends. */
    private void answer(String what, String text) {
        write(what, text);
        finish();
    }

    /**
     * Writes the record through a temporary file, renamed only once whole:
     * one that could not be written is removed, and no record is left.
     */
    private void write(String what, String text) {
        String id = getIntent() != null ? getIntent().getStringExtra("id") : null;
        File record = new File(getFilesDir(), RECORD);
        File temporary = new File(getFilesDir(), RECORD + ".tmp");
        answered = true;
        try (FileOutputStream out = new FileOutputStream(temporary)) {
            out.write((what + " " + (id != null ? id : "") + "\n" + text + "\n").getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        } catch (IOException e) {
            Log.e(TAG, "memories-pc: disc picker: could not write " + temporary + ": " + e);
            if (!temporary.delete() && temporary.exists())
                Log.e(TAG, "memories-pc: disc picker: could not remove " + temporary);
            return;
        }
        if (!temporary.renameTo(record))
            Log.e(TAG, "memories-pc: disc picker: could not write " + record);
    }

    /** Gives back the lasting read right {@link #onActivityResult} took. */
    public static void release(Context context, String uri) {
        try {
            context.getContentResolver().releasePersistableUriPermission(Uri.parse(uri),
                    Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (RuntimeException e) {
            // none was taken
        }
    }

    /**
     * Gives back every lasting read right the app holds but `keep`'s (""
     * for none): the game takes them for the disc image only, so one left
     * over is from a copy cut short or a pick never taken up.
     */
    public static void releaseAll(Context context, String keep) {
        try {
            ContentResolver resolver = context.getContentResolver();
            for (UriPermission held : resolver.getPersistedUriPermissions()) {
                if (held.getUri().toString().equals(keep))
                    continue;
                int flags = (held.isReadPermission() ? Intent.FLAG_GRANT_READ_URI_PERMISSION : 0)
                        | (held.isWritePermission() ? Intent.FLAG_GRANT_WRITE_URI_PERMISSION : 0);
                try {
                    resolver.releasePersistableUriPermission(held.getUri(), flags);
                    Log.i(TAG, "memories-pc: disc picker: a lasting read right left over given back");
                } catch (RuntimeException e) {
                    Log.i(TAG, "memories-pc: disc picker: a lasting read right could not be given back (" + e + ")");
                }
            }
        } catch (RuntimeException e) {
            Log.i(TAG, "memories-pc: disc picker: the lasting read rights could not be listed (" + e + ")");
        }
    }

    // --- the copy's box ---------------------------------------------------

    private static AlertDialog box;     // on the main thread only
    private static TextView boxText;
    private static ProgressBar boxBar;

    /**
     * Shows (or updates) the box: `text` over a bar `done` of `total` full,
     * a moving bar when total is 0. Returns at once; the box is the main
     * thread's.
     */
    public static void progress(final Activity activity, final String title, final String text, final int done,
                                final int total) {
        activity.runOnUiThread(() -> {
            try {
                if (box == null) {
                    LinearLayout layout = new LinearLayout(activity);
                    layout.setOrientation(LinearLayout.VERTICAL);
                    int pad = (int) (24 * activity.getResources().getDisplayMetrics().density);
                    layout.setPadding(pad, pad / 2, pad, pad / 2);
                    boxText = new TextView(activity);
                    boxBar = new ProgressBar(activity, null, android.R.attr.progressBarStyleHorizontal);
                    layout.addView(boxText);
                    layout.addView(boxBar);
                    box = new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle(title).setView(layout).setCancelable(false).create();
                    box.show();
                }
                box.setTitle(title);
                boxText.setText(text);
                boxBar.setIndeterminate(total <= 0);
                if (total > 0) {
                    boxBar.setMax(total);
                    boxBar.setProgress(done);
                }
            } catch (RuntimeException e) {
                // the activity going away: the copy goes on without its box
                Log.i(TAG, "memories-pc: disc picker: the copy's box could not be shown (" + e + ")");
            }
        });
    }

    /** Takes the box away. */
    public static void close(Activity activity) {
        activity.runOnUiThread(() -> {
            try {
                if (box != null)
                    box.dismiss();
            } catch (RuntimeException e) {
                // gone with its window already
            }
            box = null;
            boxText = null;
            boxBar = null;
        });
    }
}
