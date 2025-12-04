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
    View emptyState; 
    private static final String TAG = "MainActivity";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        
        recyclerView = findViewById(R.id.NESRecyclerView);
        
        // Tenta achar o emptyStateText (do XML novo) ou emptyStateLayout (do antigo)
        emptyState = findViewById(R.id.emptyStateText); 
        if (emptyState == null) {
            // Fallback via ID numérico se necessário, mas melhor confiar no XML correto
            // Apenas verifique se o seu activity_main.xml tem o ID correto
        }

        // MUDANÇA IMPORTANTE: Lista Vertical
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        
        loadGames();
        
        findViewById(R.id.openROMBtn).setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, 42);
        });
    }

    // Função para limpar nomes de ROMs (Ex: "Super Mario (USA).nes" -> "Super Mario")
    private String cleanRomName(String fileName) {
        String name = fileName;
        int dotIndex = name.lastIndexOf('.');
        if (dotIndex > 0) {
            name = name.substring(0, dotIndex);
        }
        // Remove (USA), [!], etc
        name = name.replaceAll("\\(.*?\\)", "").replaceAll("\\[.*?\\]", "");
        name = name.replace("_", " ").trim();
        return name;
    }

    private void loadGames() {
        list = new ArrayList<>();
        File internalDir = getFilesDir();
        File[] files = internalDir.listFiles();
        if (files != null) {
            for (File file : files) {
                if (file.getName().toLowerCase().endsWith(".nes") || file.getName().toLowerCase().endsWith(".nsf")) {
                    // Usa o cleanRomName para o título, mas mantém o path original
                    String displayName = cleanRomName(file.getName());
                    list.add(new NESItemModel(null, displayName, file.getAbsolutePath()));
                }
            }
        }
        
        // Carregar Assets (Jogos embutidos)
        try {
            String[] assets = getAssets().list("roms");
            if(assets != null) {
                for(String r : assets) {
                     String displayName = cleanRomName(r);
                     list.add(new NESItemModel(null, displayName, "roms/"+r));
                }
            }
        } catch (IOException e) {}

        if (list.isEmpty()) {
            if(emptyState != null) emptyState.setVisibility(View.VISIBLE);
            recyclerView.setVisibility(View.GONE);
        } else {
            if(emptyState != null) emptyState.setVisibility(View.GONE);
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
            // Tenta pegar nome real
            android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null);
            if (cursor != null && cursor.moveToFirst()) {
                int nameIndex = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (nameIndex != -1) fileName = cursor.getString(nameIndex);
                cursor.close();
            }

            InputStream is = getContentResolver().openInputStream(uri);
            File dest = new File(getFilesDir(), fileName);
            OutputStream os = new FileOutputStream(dest);
            byte[] buf = new byte[4096]; int len;
            while ((len = is.read(buf)) > 0) os.write(buf, 0, len);
            os.close(); is.close();
            Toast.makeText(this, "Importado: " + cleanRomName(fileName), Toast.LENGTH_SHORT).show();
            loadGames();
        } catch (Exception e) {
            Log.e(TAG, "Import error", e);
            Toast.makeText(this, "Erro ao importar", Toast.LENGTH_SHORT).show();
        }
    }
}
