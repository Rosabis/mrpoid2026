package com.mrpoid.mrplist.app;

import com.mrpoid.MrpoidMain;
import com.mrpoid.mrplist.R;
import com.edroid.common.utils.UmengUtils;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.view.WindowManager;
import android.widget.Toast;

/**
 * 启动页：已有存储权限则直接运行；仅在检测到未授权时再说明并请求。
 */
public class WelcomeActivity extends AppCompatActivity {

    private static final int REQUEST_MANAGE_STORAGE = 100;
    private static final int REQ_LEGACY_STORAGE = 1;

    private void launchGame() {
        MrpoidMain.runMrp(this, "dsm_gm.mrp");
        finish();
    }

    /** Android 11+：全部文件访问；Android 6～10：读写外部存储；更低版本由安装时权限决定 */
    private boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            return isGranted(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
            });
        }
        return true;
    }

    private void showStorageIntroThenRequest() {
        new AlertDialog.Builder(this)
                .setTitle(R.string.storage_permission_title)
                .setMessage(R.string.storage_permission_message)
                .setCancelable(false)
                .setPositiveButton(R.string.storage_permission_ok, (d, w) -> requestFileAccess())
                .setNegativeButton(android.R.string.cancel, (d, w) -> finish())
                .show();
    }

    private void requestFileAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.setData(Uri.parse("package:" + getPackageName()));
                    startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                } catch (Throwable t) {
                    Intent fallback = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                    fallback.setData(Uri.parse("package:" + getPackageName()));
                    startActivityForResult(fallback, REQUEST_MANAGE_STORAGE);
                }
                return;
            }
            launchGame();
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            String[] perms = {
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
            };
            if (!isGranted(perms)) {
                ActivityCompat.requestPermissions(this, perms, REQ_LEGACY_STORAGE);
                return;
            }
        }
        launchGame();
    }

    private boolean isGranted(String[] perms) {
        for (String p : perms) {
            if (ContextCompat.checkSelfPermission(this, p) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_STORAGE) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && Environment.isExternalStorageManager()) {
                launchGame();
            } else {
                Toast.makeText(this, R.string.storage_permission_denied, Toast.LENGTH_LONG).show();
                if (!hasStorageAccess()) {
                    showStorageIntroThenRequest();
                }
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQ_LEGACY_STORAGE) {
            return;
        }
        for (int r : grantResults) {
            if (r != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, R.string.storage_permission_denied, Toast.LENGTH_LONG).show();
                if (!hasStorageAccess()) {
                    showStorageIntroThenRequest();
                }
                return;
            }
        }
        launchGame();
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS);

        if (hasStorageAccess()) {
            launchGame();
            return;
        }
        setContentView(R.layout.activity_welcome);
        showStorageIntroThenRequest();
    }

    @Override
    protected void onPause() {
        UmengUtils.onPause(this);
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        UmengUtils.onResume(this);
    }
}
