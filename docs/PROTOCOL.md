# TETHER protokolü (v1)

Eklenti ile norns'taki `tether` mod'u arasındaki OSC. UDP, norns tarafında matron'un portu **10111**.
Eklenti kendi portunu işletim sistemine seçtirir ve her mesajda (ya da `hello`'da) bildirir;
mod cevabı o adrese (`from` host'u + bildirilen port) yollar. Bir norns'a aynı anda birden çok
eklenti bağlanabilir: her biri ayrı bir istemci.

Mod `/tether/` ile başlayan her yolu kendisi tüketir; geri kalan OSC (`/param/...`, `/remote/...`,
script'in kendi yolları) olduğu gibi norns'a geçer. Mod'u kurmadan da `/remote/key` ve
`/remote/enc` çalışır: eklenti mod'dan cevap alamazsa tuşları ve enkoderleri onlarla gönderir.

Sayılar Lua'dan gelir: tamsayı `i`, ondalık `f`; eklenti iki yönde de esnek okur
(`d`, `h`, `T`, `F`, bundle da kabul).

## Eklenti → norns

| yol | argümanlar | ne |
|---|---|---|
| `/tether/hello` | `port:i name:s` | kayıt (name: kanal adı, mod menüsünde görünür). Cevap: `welcome`, sonra liste |
| `/tether/ping` | `seq:i port:i` | saniyede bir. Cevap: `pong` |
| `/tether/bye` | `port:i` | ayrıl (eklenti kapanınca, LINK kapanınca) |
| `/tether/dump` | `port:i` | parametre listesini baştan iste |
| `/tether/range` | `from:i to:i port:i` | listenin bu satırlarını yeniden iste (kaybolan paketler) |
| `/tether/set` | `id:s raw:f` | parametreyi 0..1 ile ayarla (aşağıda tiplere göre) |
| `/tether/key` | `n:i z:i` | K1–K3 basıldı (1) / bırakıldı (0) |
| `/tether/enc` | `n:i d:i` | E1–E3 döndü |
| `/tether/screen` | `fps:i port:i` | ekran aynası, 0 = kapalı (editör kapanınca). 2 s'de bir tekrarlanır |
| `/tether/clock` | `bpm:f playing:i mode:i` | mode 1 tempo, 2 tempo + başlat/durdur. Yalnız norns'un kaynağı `internal` iken uygulanır |
| `/tether/audio/start` | | tether-audio'yu başlat (ses: `AUDIO.md`) |
| `/tether/pset/load` | `n:i` | `params:read(n)` |
| `/tether/pset/save` | `n:i name:s` | `params:write(n, name)` |

## norns → eklenti

| yol | argümanlar | ne |
|---|---|---|
| `/tether/welcome` | `version:i script:s` | `hello`'ya cevap |
| `/tether/pong` | `seq:i epoch:i` | gecikme ölçümü; epoch eklentininkiyle tutmazsa eklenti listeyi yeniden ister |
| `/tether/params/begin` | `count:i script:s epoch:i` | liste başlıyor |
| `/tether/p` | `index:i id:s name:s type:i raw:f text:s steps:i hidden:i` | bir satır (1'den başlar). Tick başına 24 satır |
| `/tether/params/end` | `count:i epoch:i` | liste bitti; eksik satırları eklenti `range` ile ister |
| `/tether/v` | `index:i raw:f text:s` | bir değer değişti (kim değiştirirse: script, menü, eklenti) |
| `/tether/script` | `name:s epoch:i` | başka bir script yüklendi; ardından yeni liste |
| `/tether/scr` | `band:i hex:s` | ekranın bir bandı: 128 × 8 piksel, piksel başına bir onaltılık hane (0–f) |
| `/tether/hw` | `kind:s n:i v:i` | norns'un kendi tuşu (`key`, v = z) ya da enkoderi (`enc`, v = delta) |

## Parametreler

Mod yalnız eklentinin sürebileceği tipleri listeler (grup, ayraç, dosya, metin listelenmez):

| tip | `type` | raw 0..1 | `steps` |
|---|---|---|---|
| number | 1 | `(value - min) / (max - min)`, ayarlarken en yakın tamsayı | max - min + 1 |
| option | 2 | `(selected - 1) / (count - 1)` | count |
| control | 3 | `get_raw()` / `set_raw()` (controlspec'in kendi eğrisi) | 0 |
| taper | 5 | `get_raw()` / `set_raw()` | 0 |
| trigger | 6 | 0; `raw ≥ 0.5` tetikler | 2 |
| binary | 9 | 0 / 1 | 2 |

Değişiklikler 15 Hz'de taranır (tick başına en çok 48 mesaj).

## Ekran

`screen.peek(0, y, 128, 8)` ile sekiz bant. Bir bant yalnız değişince gider; her karede bir bant
sırayla yine de gider (kaybolan paket en geç 8 karede iyileşir). İlk kare ve her `screen` mesajından
sonraki kare tamdır. Ayna istemcilerin istediği en yüksek fps'te çalışır (en çok 30).

## Zamanlama

Mod'un yoklaması bir `metro`dur (30 Hz). norns her script yüklemede `metro.free_all()` çağırdığı için
metro `script_pre_init` ve `script_post_init`'te yeniden kurulur. 6 s ping gelmeyen istemci düşer.
