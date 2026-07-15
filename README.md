# 🛠️ Program Monitoring System

**Sistem monitoring remote berbasis C++ dengan teknik process injection dan command polling.**

---

## 📝 Deskripsi

Proyek ini merupakan **program monitoring sistem** yang memungkinkan eksekusi perintah jarak jauh melalui HTTP polling. Terdiri dari dua komponen utama:

- **`sysdiag.exe`** → Engine utama (remote diagnostics)
- **`loader.exe`** → Lightweight injector + self-delete

> ⚠️ **Disclaimer**: Program ini dibuat **murni untuk tujuan edukasi** dan pembelajaran teknik low-level Windows programming, process injection, AMSI bypass, dan anti-analysis. **Tidak boleh digunakan untuk kegiatan ilegal.**

---

## ✨ Fitur Utama

- **Remote Command Execution** via HTTP polling
- **AMSI Bypass** (dual layer + unhook)
- **Process Injection** ke `explorer.exe` (menggunakan `NtCreateThreadEx`)
- **Self-Delete** setelah eksekusi
- **Hidden Console** + Pipe communication
- **Encrypted C2 URL** (XOR)
- **PowerShell Support** dengan encoded command
- **Output Queue** dengan retry mechanism

---

## 📁 Struktur Proyek

| File                    | Deskripsi                                      | Keterangan                          |
|------------------------|------------------------------------------------|-------------------------------------|
| `sysdiag.exe`          | Remote diagnostic engine                       | Compiled from `remote_diagnostics.cpp` |
| `loader.exe`           | Payload injector + self delete                 | Compiled from `loader.cpp`          |
| `payload.bin`          | Shellcode / payload yang di-inject             | Convert shell code dari `sysdiag.exe` |
| `payload.h`            | Header berisi payload array                    | Convert array dari versi `payload.bin`|
| `remote_diagnostics.cpp` | Core logic (polling, cmd pipe, http)         | -                                   |
| `loader.cpp`           | Process injection ke explorer.exe              | -                                   |

---
