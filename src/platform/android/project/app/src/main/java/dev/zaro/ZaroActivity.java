package dev.zaro;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

import org.libsdl.app.SDLActivity;

/**
 * Runs the zaro command line from libmain.so (D-021).
 *
 * The arguments come from the launch intent's "args" extra, or else from assets/zaro_args.txt,
 * which `zaro export android` writes (by default "run game/scene.toml"). For example:
 *
 *   adb shell am start -S -n PACKAGE/dev.zaro.ZaroActivity --es args "'run game/scene.toml --headless --perf perf.json'"
 *
 * The native side unpacks the assets into the app's external files directory, runs there, and
 * sends stdout and stderr to logcat with the tag "zaro".
 */
public class ZaroActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        // libmain.so links libwgpu_native.so, which the dynamic linker finds next to it.
        return new String[] {"SDL3", "main"};
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected String[] getArguments() {
        String args = getIntent().getStringExtra("args");
        if (args == null) {
            args = readAsset("zaro_args.txt");
        }
        args = args.trim();
        return args.isEmpty() ? new String[0] : args.split("\s+");
    }

    private String readAsset(String name) {
        try (InputStream in = getAssets().open(name)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            for (int n; (n = in.read(buffer)) > 0; ) {
                out.write(buffer, 0, n);
            }
            return out.toString(StandardCharsets.UTF_8.name());
        } catch (IOException e) {
            return "";
        }
    }
}
