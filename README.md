pada kesempatan kali ini saya ingin membuat suatu program yg dimana dapat melakukan monitoring system computer yg dipasanginya 
sebelumnya saya himbau bahwa kode ini dibuat bukan serta merta untuk kejahatan melainkan untuk praktek pembelajaran baik pembelajaran cara kerja malware ataupun antivirus,compiler,obfuskasi dll penyalahgunaan tidak ditanggung.

struktur utama dari program ini yaitu sysdiag.exe ygg merupakan hasil compile remote_diagnostic.cpp dan loader.exe hasil compile dari loader.cpp

remote_diagnostic.cpp sendiri dasarnya dirancang untuk membuka proses cmd dan akan dilanjut dengan menciptakan sub procces sebuah powershell jika dibutuhkan dengan mekanisme pemanggilan http alias polling atau apaitu disebutnya, bukan via raw socket
kemudian si cpp ini akan melakukan http get untuk mengecek ke backend apakah ada command terbaru yg dikirim dari server berdasarkan waktu polling thread yg sudah ditentukan lalu data data dari cpp ini akan di simpan di database di server

loader.cpp sendiri versi lebih ringannya dari remote_diagnostic.cpp karena dia hanya akan menjalankan dirinya sendiri dan membawa payload.h hasil dari conc=ver dari payload.bin dari hasil conver sysdiag.exe dari remote diagnostic.cpp lalu setelah selesai dia akan self delate program ini sendiri jadi sysdiag.exe hanya jalan di memory
