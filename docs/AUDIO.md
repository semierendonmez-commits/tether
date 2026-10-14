# TETHER ses akışı (v1)

norns'un sesi DAW'a, DAW'ın sesi norns'a: UDP üzerinden stereo ses. İki uç:

| uç | nerede | ne |
|---|---|---|
| **tether-audio** | norns, `~/dust/code/tether/bin/` | JACK istemcisi. `crone:output_1/2` (norns'un çıkışı: kulaklığa / hat çıkışına giden karışım) → eklentiye; eklentiden gelen → `crone:input_1/2` (ADC'nin yanına, softcut ve motor duyar) |
| **AudioLink** | eklenti (`Source/engine/AudioLink.h`) | kendi thread'i ve soketi; ses thread'inde kilitsiz |

```
 norns                                              DAW
 crone:output_1/2 ─► tether-audio ──UDP 10112──► AudioLink ─► TETHER / TETHER FX çıkışı
 crone:input_1/2  ◄─ tether-audio ◄──────────── AudioLink ◄─ TETHER FX girişi
```

## Başlatma

tether-audio'yu mod başlatır. Eklenti abone olur (`SUB`), 1.5 s cevap gelmezse OSC ile mod'a
`/tether/audio/start` der; mod makinenin mimarisine göre ikiliyi seçer (`uname -m`: `armv7l` →
`tether-audio-armhf`, `aarch64` → `tether-audio-arm64`) ve arka planda çalıştırır (log
`/tmp/tether-audio.log`). İkili UDP 10112'yi tutar: ikinci kopya portu dolu bulup çıkar, yani
istemek zararsızdır. İki dakika abonesiz kalınca kendisi çıkar.

İkililer glibc 2.17 ve üstüne bağlanmıştır (norns buster 2.28, bullseye 2.31), libc++ içine gömülü,
JACK'i çalışma anında `libjack.so.0`'dan yükler: norns'a hiçbir şey kurmak gerekmez.
`scripts/build-norns-audio.sh` (zig ile) yeniden üretir.

## Paketler

UDP, tether-audio'nun portu 10112. Bütün sayılar little-endian.

**Ses** (24 bayt başlık + örnekler, iç içe stereo):

| ofset | alan |
|---|---|
| 0 | `"TAU1"` |
| 4 | u32 akış kimliği (gönderen her başladığında rastgele) |
| 8 | u32 sıra numarası |
| 12 | u32 örnekleme hızı (gönderenin) |
| 16 | u16 kare sayısı |
| 18 | u8 kanal (2) |
| 19 | u8 biçim: 1 = 16-bit, 2 = 24-bit (işaretli, packed) |
| 20 | u32 ayrılmış |

norns 128 karelik paketler gönderir (JACK periyodu, 48 kHz'de 2.7 ms), eklenti de 128.
24-bit stereo 48 kHz: yön başına ~2.3 Mbit/s; 16-bit ~1.5 Mbit/s.

**Kontrol** (`"TCT1"`, 4: tür):

| tür | yön | alanlar |
|---|---|---|
| `SUB` 1 | eklenti → norns, saniyede iki | 5 norns'tan istiyor · 6 norns'a gönderiyor · 7 biçim · 8 u16 tampon ms |
| `STAT` 2 | norns → eklenti, saniyede iki | 5 sürüm · 8 JACK hızı · 12 periyot · 16 xrun · 20 boşalma · 24 kayıp · 28 f32 tampon ms · 32 yakalama bağlantısı · 33 çalma bağlantısı · 36 gönderilen paket |
| `BYE` 3 | eklenti → norns | dur |

norns aynı anda tek eklentiye hizmet eder: son abone olan. 3 s abonelik gelmezse düşer.

## Alıcı: titreşim tamponu ve saat kayması

İki ucun saati hiçbir zaman tam aynı değildir (kristaller milyonda onlarca fark eder) ve ağ paketleri
düzensiz, kimi zaman sırası karışık getirir. Alıcı (`audio::Receiver`, iki uçta aynı kod):

- **Yerine yazma:** paket, sıra numarasından hesaplanan yerine yazılır. Sırası karışık gelen paket,
  okuyucu oraya varmadan geldiyse kendi boşluğunu doldurur; hiç gelmeyen sessizlik olarak çalar.
- **Tampon:** okuyucu, tampon BUFFER kadar dolunca çalmaya başlar (eklentide 10–250 ms, varsayılan 40).
- **Kayma:** okuyucu 4 noktalı Hermite ile yeniden örnekler; oran = gönderenin hızı / alıcının hızı
  × (1 + düzeltme). Düzeltme tamponun ortalamasını hedefte tutan bir PI denetleyicisidir (en çok %1,
  birkaç saniyede oturur, ağ titreşimi perdeyi oynatmaz). 44.1 kHz bir oturum norns'un 48 kHz'ini
  böylece doğrudan alır.
- **Boşalma:** tampon biterse sessizlik, sonra yeniden dolana kadar bekleme.
- **Fazla birikme:** bir duraklamadan sonra tampon hedefin iki katını aşarsa okuyucu hedefe atlar.

## Gecikme

Uçtan uca: tampon (BUFFER) + ağ (kabloda ~1 ms, Wi-Fi'de 2–20 ms ve dalgalı) + JACK periyodu
(norns'ta 3 × 2.7 ms). FROM NORNS açıkken eklenti BUFFER'ı hosta gecikme olarak bildirir: host
diğer kanalları o kadar geciktirir, kayıtta hizalanır. TETHER FX'te THRU açıkken kuru sinyal de
aynı süre geciktirilir, norns'un dönüşüyle hizalı kalır.

Canlı çalarken (norns'u çalıp DAW'dan dinlemek) bu gecikme hissedilir: kabloda BUFFER 15–20 ms
rahat çalışır, Wi-Fi'de 40 ms ve üstü gerekir.

## Testler

| test | ne |
|---|---|
| `engine` (simülasyon) | 3 dakikalık sanal zaman: norns saati 150 ppm hızlı, 2–32 ms titreşim (sık sık sırası karışık), 44.1 kHz host; 120 s'den sonra %1 kayıp ve 60 ms sıçramalar. Frekans, tıksızlık (örnekten örneğe adım kusursuz sinüsün eğimi), boşalma yok, tampon hedefte, kayma doğru tahmin edilmiş |
| `audio` (uçtan uca) | gerçek bir JACK sunucusu (dummy sürücü, senkron, 48 kHz / 128 — norns'taki gibi), crone yerine bir JACK istemcisi (440 / 550 Hz çalar, girişini ölçer), gerçek tether-audio, AudioLink 44.1 kHz'de: iki yön frekansla, yönleri kapatınca sessizlik, 16-bit, daemon isteği |
| `audio_Synth`, `audio_Fx` | aynı düzenekte işlemcinin kendisi: gecikme bildirimi, seviye, THRU, kuru sinyal, LINK kapalı, enstrümanın göndermemesi |

Dummy sürücü gerçek bir kristal değildir (meşgul bir makinede binde birkaç sapar): testler saatini
ölçer ve beklenen frekansları ona göre hesaplar; tıksızlık kontrolü simülasyondadır.

norns'ta denenmedi. armhf ikilisi qemu altında gerçek bir armhf libjack ile yüklendi ve 16 JACK
fonksiyonunun hepsini buldu.
