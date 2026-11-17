# TTGO WiFi Repeater

Repeater WiFi (AP+STA dengan NAT) untuk LilyGo T-Display (ESP32 biasa).

## Build dan flash

1. Upload isi folder ini ke repo GitHub, lalu buka tab **Actions** dan tunggu build selesai.
2. Unduh artifact `ttgo-wifi-repeater` dan ambil `ttgo-wifi-repeater.bin` (file gabungan siap flash).
3. Flash di alamat **0x0**, misalnya:
   `esptool.py --chip esp32 write_flash 0x0 ttgo-wifi-repeater.bin`
   atau lewat flasher web (alamat 0x0).

Memberi tag `v1.0` akan membuat rilis otomatis berisi file .bin.

## Tombol

| Tombol | Aksi |
|---|---|
| Atas (GPIO35) | pilih / ketik karakter. **Dobel klik = power off** (tidak aktif di layar keyboard). Menyalakan lagi: tekan sekali. |
| Bawah (GPIO0) | klik = lanjut. Tahan = buka menu, atau kembali/tutup. |

Halaman utama: Status, Klien, Sistem (klik bawah untuk pindah). Atas di halaman Klien menggeser daftar.

## Keyboard dua tombol

Pilih **baris** dulu (bawah = baris berikutnya, atas = pilih), lalu **kolom** (bawah = kolom berikutnya, atas = ketik).
Tahan bawah: di mode kolom kembali ke pilih baris, di mode baris menghapus satu karakter.
Baris terakhir berisi: ganti set (abc, ABC, #+=), Spasi, Hapus, Lihat/Tutup, OK, Batal.

## Web UI

Sambungkan perangkat ke hotspot repeater (default `TTGO-EXT`, sandi `ttgo-repeater`), lalu buka `http://192.168.4.1`.
Web UI sengaja hanya bisa dibuka lewat hotspot repeater, bukan dari jaringan sumber.
Alamat repeater bisa berubah ke `192.168.5.1` kalau router sumber memakai subnet `192.168.4.x`; alamat aktif ada di halaman Sistem.

## Catatan

- Kanal hotspot selalu mengikuti kanal jaringan sumber (satu radio dipakai bersama), jadi tidak bisa diatur sendiri.
- Redup otomatis dan kecerahan ada di menu (tahan tombol bawah) dan di web UI.
- Set ulang orientasi: menu "Putar layar", atau ubah `ROT_A`/`ROT_B` di `platformio.ini`.
- `src/fonts.h` dibuat oleh `tools/genfont.py` dari font Inter (SIL OFL 1.1).
