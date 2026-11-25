package com.shootout26.soccer;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.zip.CRC32;

public class RomPickerActivity extends Activity {
    private static final int REQ_PICK_ROM = 1001;
    private static final int ROM_SIZE = 0x180000;
    private static final long ROM_CRC = 0xee314ae5L;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        File romFile = new File(getFilesDir(), "rom.sfc");
        if (romFile.exists() && romFile.length() == ROM_SIZE) {
            launchGame();
            return;
        }
        promptForRom();
    }

    private void launchGame() {
        startActivity(new Intent(this, SoccerActivity.class));
        finish();
    }

    private void promptForRom() {
        new AlertDialog.Builder(this)
            .setTitle("Capcom's Soccer Shootout 2026")
            .setMessage("Selecciona tu ROM dumpeada legalmente de Capcom's Soccer Shootout (USA) (.sfc / .smc).\n\nSelect your legally dumped Capcom's Soccer Shootout (USA) ROM (.sfc / .smc).")
            .setCancelable(false)
            .setPositiveButton("Seleccionar ROM", (d, w) -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                startActivityForResult(intent, REQ_PICK_ROM);
            })
            .setNegativeButton("Salir", (d, w) -> finish())
            .show();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_PICK_ROM) return;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            promptForRom();
            return;
        }
        try {
            Uri uri = data.getData();
            InputStream in = getContentResolver().openInputStream(uri);
            if (in == null) { promptForRom(); return; }
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            byte[] tmp = new byte[16384];
            int n;
            while ((n = in.read(tmp)) > 0) buf.write(tmp, 0, n);
            in.close();
            byte[] raw = buf.toByteArray();
            int off = (raw.length == ROM_SIZE + 512) ? 512 : 0;
            if (raw.length - off != ROM_SIZE) {
                Toast.makeText(this, "ROM invalida: debe ser Capcom's Soccer Shootout (USA) de 1.5 MB.", Toast.LENGTH_LONG).show();
                promptForRom();
                return;
            }
            CRC32 crc = new CRC32();
            crc.update(raw, off, ROM_SIZE);
            if (crc.getValue() != ROM_CRC) {
                Toast.makeText(this, "CRC32 no coincide con Capcom's Soccer Shootout (USA).", Toast.LENGTH_LONG).show();
                promptForRom();
                return;
            }
            File romFile = new File(getFilesDir(), "rom.sfc");
            try (FileOutputStream fos = new FileOutputStream(romFile)) {
                fos.write(raw, off, ROM_SIZE);
            }
            launchGame();
        } catch (Exception e) {
            Toast.makeText(this, "Error leyendo la ROM: " + e.getMessage(), Toast.LENGTH_LONG).show();
            promptForRom();
        }
    }
}
