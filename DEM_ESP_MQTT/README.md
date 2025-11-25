# ESP-IDF Akıllı Wi-Fi ve MQTT Kurulumu (NVS Destekli) - Algoritma Akışı
# ESP-IDF Smart Wi-Fi and MQTT Setup (NVS Supported) - Algorithm Flow

---

## 🇹🇷 Türkçe

Bu ESP-IDF uygulaması, NVS (Kalıcı Depolama) destekli bir akıllı Wi-Fi ve MQTT bağlantı yöneticisidir.
Sistem açılışta NVS flash belleği, ağ arayüzlerini ve UART sürücüsünü başlatır.
Kullanıcıya seri terminal üzerinden "Otomatik Bağlan" (O) ve "Yeni Kurulum" (N) seçeneklerini sunan bir menü döngüsü görüntülenir.
"Otomatik Bağlan" seçilirse, sistem NVS hafızasından SSID, şifre, broker IP'si ve konu başlığını okumaya çalışır.
Veriler başarıyla okunursa, bu kayıtlı bilgiler kullanılarak Wi-Fi bağlantısı denenir.
Bağlantı denemesi 8 saniyelik bir zaman aşımı süresince sonucu bekler.
"Yeni Kurulum" seçilirse, sistem interaktif bir sihirbaz moduna geçer.
Kullanıcıdan SSID ve şifre (maskelenmiş olarak) girmesi istenir.
Girilen bilgilerle Wi-Fi bağlantısı hemen denenir; başarısız olursa giriş adımı tekrarlanır.
Bağlantı başarılı olursa, Wi-Fi bilgileri NVS'ye kaydedilir ve kullanıcıdan MQTT Broker IP'si ile Konu başlığı istenir.
Bu yeni MQTT bilgileri de NVS'ye kaydedildikten sonra kurulum modu tamamlanır.
Kurulum veya otomatik yükleme sonrası MQTT istemcisi başlatılır (varsayılan 1883 portu ile).
Sistem sonsuz bir ana döngüye girer.
Döngüde, hem Wi-Fi hem de MQTT bağlantısının aktif olduğu doğrulanır.
Her iki bağlantı da mevcutsa, 0 ile 99 arasında rastgele bir sayı üretilir ve belirlenen konuya yayınlanır.
Eğer Wi-Fi bağlantısı koparsa, sistem durumu algılar ve yeniden bağlanma fonksiyonunu tetikler.
Bu döngü her 10 saniyede bir tekrarlanır.

---

## 🇬🇧 English

This ESP-IDF application is a smart Wi-Fi and MQTT connection manager supported by NVS (Non-Volatile Storage).
Upon startup, the system initializes NVS flash memory, network interfaces, and the UART driver.
A menu loop is presented to the user via the serial terminal, offering "Auto Connect" (O) and "New Setup" (N) options.
If "Auto Connect" is selected, the system attempts to read the SSID, password, broker IP, and topic from NVS memory.
If the data is successfully read, a Wi-Fi connection is attempted using these saved credentials.
The connection attempt waits for a result within an 8-second timeout period.
If "New Setup" is selected, the system enters an interactive wizard mode.
The user is prompted to enter the SSID and password (masked).
A Wi-Fi connection is immediately attempted with the entered details; if it fails, the input step is repeated.
If the connection is successful, Wi-Fi credentials are saved to NVS, and the user is prompted for the MQTT Broker IP and Topic.
After these new MQTT details are also saved to NVS, the setup mode is completed.
Following setup or auto-loading, the MQTT client is started (defaulting to port 1883).
The system enters an infinite main loop.
In the loop, it verifies that both Wi-Fi and MQTT connections are active.
If both connections are present, a random number between 0 and 99 is generated and published to the specified topic.
If the Wi-Fi connection is lost, the system detects the status and triggers the reconnection function.
This loop repeats every 10 seconds.