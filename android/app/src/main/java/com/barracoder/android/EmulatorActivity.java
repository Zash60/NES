package com.barracoder.android;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.util.Pair;
import android.view.WindowMetrics;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.util.Arrays;

public class EmulatorActivity extends SDLActivity {
    private static final String TAG = "ANDRONES_EMULATOR";
    private String mEmulatorFile;
    private boolean mGenie, mIsTV;
    private int mWidth, mHeight;

    @Override
    protected String[] getLibraries() {
        return new String[]{
                "SDL3",
                "SDL3_ttf",
                "nes" // Nome da lib corrigido conforme CMakeLists.txt
        };
    }

    @Override
    protected String[] getArguments() {
        String[] args = new String[]{
                mEmulatorFile,
                String.valueOf(mWidth),
                String.valueOf(mHeight),
                String.valueOf(mIsTV? 1 : 0),
        };

        if(mGenie) {
            args = Arrays.copyOf(args, args.length + 1);
            args[args.length - 1] = "roms/GENIE.nes";
        }
        return args;
    }

    public static Pair<Integer, Integer> getScreenDimensions(Activity activity) {
        int width;
        int height;

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowMetrics metrics = activity.getWindowManager().getCurrentWindowMetrics();
            Rect bounds = metrics.getBounds();
            width = bounds.width();
            height = bounds.height();
        } else {
            DisplayMetrics metrics = new DisplayMetrics();
            activity.getWindowManager().getDefaultDisplay().getMetrics(metrics);
            width = metrics.widthPixels;
            height = metrics.heightPixels;
        }

        return new Pair<>(width, height);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState){
        super.onCreate(savedInstanceState);
        
        // 1. Cria o script Lua padrão para forçar a criação da pasta 'data'
        createDefaultLuaScript();

        mEmulatorFile = getIntent().getStringExtra("rom");
        mGenie = getIntent().getBooleanExtra("genie", false);
        mIsTV = getIntent().getBooleanExtra("tv", false);

        Pair<Integer, Integer> dimensions = getScreenDimensions(this);
        mWidth = dimensions.first;
        mHeight = dimensions.second;
        Log.i(TAG, "size: " + mWidth + "x" + mHeight);
        Log.i(TAG, "IS TV: " + mIsTV);
    }

    // Função para criar o arquivo Lua e a pasta
    private void createDefaultLuaScript() {
        try {
            // Isso acessa /Android/data/com.barracoder.android/files/
            File dir = getExternalFilesDir(null); 
            if (dir == null) return;

            File scriptFile = new File(dir, "hitbox.lua");
            
            // Só cria se não existir
            if (!scriptFile.exists()) {
                Log.i(TAG, "Criando script padrao em: " + scriptFile.getAbsolutePath());
                
                FileOutputStream fos = new FileOutputStream(scriptFile);
                OutputStreamWriter writer = new OutputStreamWriter(fos);
                
                writer.write("-- Exemplo de Script Lua para AndroNES\n");
                writer.write("-- Desenha uma caixa ao redor do Mario (Super Mario Bros)\n");
                writer.write("while true do\n");
                writer.write("    -- 0x0086 = Posicao X, 0x00CE = Posicao Y (na memoria RAM)\n");
                writer.write("    local x = memory.readbyte(0x0086)\n");
                writer.write("    local y = memory.readbyte(0x00CE)\n");
                writer.write("    \n");
                writer.write("    if x > 0 and y > 0 then\n");
                writer.write("        gui.drawbox(x, y, x+16, y+24, \"green\")\n");
                writer.write("        gui.text(x, y-10, \"Mario\")\n");
                writer.write("    end\n");
                writer.write("    \n");
                writer.write("    FCEU.frameadvance()\n");
                writer.write("end\n");
                
                writer.close();
                fos.close();
            }
        } catch (IOException e) {
            Log.e(TAG, "Erro ao criar script Lua padrao", e);
        }
    }

    @Override
    public void setOrientationBis(int w, int h, boolean resizable, String hint){
        // Force sensorLandscape
        mSingleton.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
    }

    public static void launchROM(Activity activity, NESItemModel item, boolean genie, boolean isTV) {
        Log.i(TAG, "Launching ROM: " + item.getRom());
        Intent intent = new Intent(activity, EmulatorActivity.class);
        intent.putExtra("rom", item.getRom());
        intent.putExtra("genie", genie);
        intent.putExtra("tv", isTV);
        ROMList.addToCategory(activity, item, "recent");
        activity.startActivity(intent);
    }
}
