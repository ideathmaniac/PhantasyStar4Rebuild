package com.ps4recomp;

import android.content.Intent;
import android.net.Uri;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import org.libsdl.app.SDLActivity;

/* SDL's activity, plus the ROM file picker the game asks for on first start
 * (port/sdl_win32.c, GetOpenFileNameW): the chosen file is copied into the
 * app's folder, so it is asked for only once. */
public class PS4Activity extends SDLActivity {
    private static final int PICK_ROM = 4242;
    private final Object lock = new Object();
    private boolean done;
    private Uri picked;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    /* called from the game's thread; waits for the user's choice */
    public String pickRom(String dest) {
        synchronized (lock) { done = false; picked = null; }
        runOnUiThread(new Runnable() {
            @Override public void run() {
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try { startActivityForResult(i, PICK_ROM); }
                catch (Exception e) { synchronized (lock) { done = true; lock.notifyAll(); } }
            }
        });
        synchronized (lock) {
            while (!done) {
                try { lock.wait(); } catch (InterruptedException e) { return null; }
            }
        }
        if (picked == null) return null;
        try {
            File out = new File(dest);
            if (out.getParentFile() != null) out.getParentFile().mkdirs();
            InputStream in = getContentResolver().openInputStream(picked);
            OutputStream os = new FileOutputStream(out);
            byte[] buf = new byte[65536];
            int n;
            long total = 0;
            while ((n = in.read(buf)) > 0 && total < 16 * 1024 * 1024) { os.write(buf, 0, n); total += n; }
            os.close();
            in.close();
            return dest;
        } catch (Exception e) {
            return null;
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == PICK_ROM) {
            synchronized (lock) {
                picked = (result == RESULT_OK && data != null) ? data.getData() : null;
                done = true;
                lock.notifyAll();
            }
            return;
        }
        super.onActivityResult(request, result, data);
    }
}
