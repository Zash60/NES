package com.barracoder.android;

import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;
import androidx.appcompat.app.AppCompatActivity;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;

public class MainActivity extends AppCompatActivity {
    ArrayList<NESItemModel> list;
    RecyclerView recyclerView;
    NESItemAdapter adapter;
    TextView emptyState;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        recyclerView = findViewById(R.id.NESRecyclerView);
        emptyState = findViewById(R.id.emptyStateText);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        loadGames();
        findViewById(R.id.openROMBtn).setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, 42);
        });
    }

    private void loadGames() {
        list = new ArrayList<>();
        File internalDir = getFilesDir();
        File[] files = internalDir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.getName().toLowerCase().endsWith(".nes")) {
                    list.add(new NESItemModel(null, file.getName().replace(".nes",""), file.getAbsolutePath()));
                }
            }
        }
        // Assets (Opcional)
        try {
            String[] assets = getAssets().list("roms");
            if(assets != null) for(String r : assets) list.add(new NESItemModel(null, r.replace(".nes",""), "roms/"+r));
        } catch (IOException e) {}

        if (list.isEmpty()) {
            emptyState.setVisibility(View.VISIBLE);
            recyclerView.setVisibility(View.GONE);
        } else {
            emptyState.setVisibility(View.GONE);
            recyclerView.setVisibility(View.VISIBLE);
            adapter = new NESItemAdapter(MainActivity.this, list);
            recyclerView.setAdapter(adapter);
        }
    }

    @Override
    public void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == 42 && resultCode == RESULT_OK && data != null) {
            Uri uri = data.getData();
            if (uri != null) importRom(uri);
        }
    }

    private void importRom(Uri uri) {
        try {
            String fileName = "game_" + System.currentTimeMillis() + ".nes";
            // Tentar pegar nome real via cursor... (Simplificado para brevidade)
            InputStream is = getContentResolver().openInputStream(uri);
            File dest = new File(getFilesDir(), fileName);
            OutputStream os = new FileOutputStream(dest);
            byte[] buf = new byte[4096]; int len;
            while ((len = is.read(buf)) > 0) os.write(buf, 0, len);
            os.close(); is.close();
            Toast.makeText(this, "Jogo Importado!", Toast.LENGTH_SHORT).show();
            loadGames();
        } catch (Exception e) {
            Toast.makeText(this, "Erro ao importar", Toast.LENGTH_SHORT).show();
        }
    }
}
