#include "Config.h"
#include <CRC32.h>
#include "NetworkManager.h"


WebServer server(80);
static File fotaFile;



bool waitForAck(uint32_t timeout_ms = 1000) {
    Serial2.flush();
    uint32_t timeout = millis();
    while (millis() - timeout < timeout_ms) {
        if (Serial2.available()) {
            uint8_t response = Serial2.read();
            if (response == 0x79) {
                return true;  // ACK
            }
           else if (response == 0x1F) {
                    // STM32 isyan bayrağını çekti! Anında işlemi iptal et.
                    Serial.println("[FOTA][HATA] STM32 NACK (0x1F) dönderdi! Veri eksik veya bozuk.");
                    return false; 
                } 
                else {
                    // Hataya düşmese bile hattan serseri bir byte gelirse görelim (Debug için efsanedir)
                    Serial.printf("[FOTA][UYARI] Beklenmeyen byte alındı: 0x%02X\n", response);
                }
                yield();
    }
}
    Serial.println("[FOTA] STM32 Timeout: Cevap gelmedi.");
    return false;
}

//0xA1: Delete code
bool stm32GlobalErase() {
    Serial.println("[FOTA] Flash siliniyor...");

    Serial2.write(0xA1);

    if(!waitForAck(1000)) return false; 
    return waitForAck(15000); 
}


uint8_t calculateChecksum(uint8_t* data, uint32_t len) {
    uint8_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
    }
    return crc;
}


bool WriteChunk(uint8_t* pData , uint16_t len)
{
    Serial2.write(0xA2);
    if(!waitForAck(1000)) { Serial.println("[FOTA] HATA: 0xA2 komut ACK'si gelmedi!"); return false; }

    uint8_t nMinusOne = (uint8_t)(len - 1);
    Serial2.write(nMinusOne);
    if(!waitForAck(1000)) { Serial.println("[FOTA] HATA: Uzunluk bilgisi ACK'si gelmedi!"); return false; }
    
    Serial.println("\n[ANALİZ] Gönderim Öncesi:");
    Serial.printf("   > RX Buffer'da bekleyen: %d byte\n", Serial2.available());
    
    delay(10); 
    Serial.println("[FOTA] 256 Byte Paket Basılıyor...");
    Serial2.write(pData , len);

    Serial2.flush(); 
    Serial.println("[FOTA] Donanımsal Flush Tamamlandı.");
    Serial.printf("   > RX Buffer Durumu: %d byte\n", Serial2.available());
    if(Serial2.available() > 0) {
        Serial.printf("   > Hat boş değil! Gelen ilk byte: 0x%02X\n", Serial2.peek());
    }
    if(!waitForAck(3000)) { 
        Serial.println("[FOTA] HATA: Flash'a yazma bitiş ACK'si gelmedi!"); 
        Serial.printf("[ANALİZ] Kritik Hata Anı RX Buffer: %d\n", Serial2.available());
        return false; 
    }
    return true;
}




bool JumpToApp()
{
    Serial.println("[FOTA] completed , main code");
    Serial2.write(0xA3);
    return waitForAck(1000);
}




bool enterSTM32Bootloader()
{
    pinMode(NRST_PIN  , OUTPUT);
    digitalWrite(NRST_PIN , LOW);
    vTaskDelay(pdMS_TO_TICKS(100));

    while(Serial2.available()) Serial2.read(); // Clean bus
    
    digitalWrite(NRST_PIN , HIGH);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint32_t tStart = millis();
    while(millis() - tStart < 600) {
        Serial2.write(0x7F); 
        
        vTaskDelay(pdMS_TO_TICKS(15)); 
        
        while (Serial2.available()) {
            uint8_t res = Serial2.read();
            if (res == 0x79) { // ACK yakalandı!
                return true;
            }
        }
    }
    return false;
    
}


void startFotaTransfer()
{
    File file =LittleFS.open("/stm32_fw.bin" , "r");
    uint8_t chunk[256];

    if (commTaskHandle != NULL)
    {
        vTaskSuspend(commTaskHandle);
        Serial.println("[FOTA] Comm Task askıya alındı. Queue'dan veri çekimi durduruldu.");
    }

    vTaskDelay(pdMS_TO_TICKS(50)); 
    while(Serial2.available()) Serial2.read(); //Empty the line


    if (!enterSTM32Bootloader()) { 
        if (commTaskHandle) vTaskResume(commTaskHandle);
        file.close();
        return;
    }

    if (!stm32GlobalErase()) {
        Serial.println("[FOTA] Flash silme başarısız!");
        if(commTaskHandle) vTaskResume(commTaskHandle);
        file.close();
        return;
    }

    size_t totalWritten = 0;
    size_t fileSize = file.size();
    while(file.available())
    {
        size_t bytesRead = file.read(chunk, sizeof(chunk));

        if(!WriteChunk(chunk , bytesRead))
        {
            Serial.printf("[FOTA][HATA] Yazma işlemi %d. byte'da koptu!\n", totalWritten);
            break;
        }

        totalWritten += bytesRead;

        if (totalWritten % 1024 == 0 || totalWritten == fileSize) {
            Serial.printf("[FOTA] İlerleme: %d%%\n", (totalWritten * 100) / fileSize);
        }
    }

    if(totalWritten == fileSize)
    {
        JumpToApp();
    }
    else {
         Serial.println("[FOTA][HATA] Eksik yazım yapıldı.");
    }
    if (commTaskHandle) vTaskResume(commTaskHandle);
    file.close();
}



uint32_t calculateIEEECRC32(File &f) {
    uint32_t crc = 0xFFFFFFFF;
    f.seek(0); // Dosya başına dön
    
    while (f.available()) {
        uint8_t b = f.read();
        crc ^= b;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return (crc ^ 0xFFFFFFFF);
}



// ─────────────────────────────────────────────
void verifyAndCompleteFOTA() {
    Serial.println("[FOTA] verifyAndCompleteFOTA() başladı.");

    String expectedCrcStr = server.header("X-File-CRC");
    Serial.printf("[FOTA] Beklenen CRC string: '%s'\n", expectedCrcStr.c_str());

    if (expectedCrcStr.isEmpty()) {
        Serial.println("[FOTA][HATA] X-File-CRC header'ı boş! İstek header'ı eksik.");
        return;
    }

    uint32_t expectedCrc = strtoul(expectedCrcStr.c_str(), NULL, 16);
    Serial.printf("[FOTA] Beklenen CRC (hex): %08X\n", expectedCrc);

    File f = LittleFS.open("/stm32_fw.bin", FILE_READ);
    if (!f) {
        Serial.println("[FOTA][HATA] /stm32_fw.bin açılamadı! Dosya yok mu?");
        return;
    }

    Serial.printf("[FOTA] Dosya boyutu: %d byte\n", f.size());
    Serial.println("[FOTA] CRC hesaplanıyor...");

    CRC32 crc;
    uint8_t buffer[1024];
    size_t byteCount = 0;
    f.seek(0);
    while (f.available()) {
        size_t n = f.read(buffer , sizeof(buffer));
        for (size_t i = 0; i < n; i++) {
            crc.update(buffer[i]);
        }
    }
    f.close();

    Serial.printf("[FOTA] Toplam okunan byte: %d\n", byteCount);

    uint32_t calculatedCrc = crc.finalize();
    Serial.printf("[FOTA] Hesaplanan CRC: %08X\n", calculatedCrc);

    if (calculatedCrc == expectedCrc) {
        Serial.println("[FOTA] BAŞARILI! CRC eşleşti. STM32'ye aktarım başlayacak.");
        startFotaTransfer();
    } else {
        Serial.printf("[FOTA][HATA] CRC UYUŞMUYOR! Beklenen: %08X | Hesaplanan: %08X\n", expectedCrc, calculatedCrc);
        LittleFS.remove("/stm32_fw.bin");
    }
}

// ─────────────────────────────────────────────
void initFOTA() {
    const char * headerkeys[] = {"X-File-CRC"} ;
    size_t headerkeyssize = sizeof(headerkeys)/sizeof(char*);
    server.collectHeaders(headerkeys, headerkeyssize);

    if (!LittleFS.begin(true)) {
        Serial.println("[FOTA][ERR] LittleFS mount");
        return;
    }

    if (LittleFS.exists("/stm32_fw.bin")) {
        File existing = LittleFS.open("/stm32_fw.bin", FILE_READ);
        Serial.printf("[FOTA]: %d byte\n", existing.size());
        existing.close();
    }
    server.on("/upload_firmware", HTTP_OPTIONS, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        server.send(200);
    });


    server.on("/upload_firmware", HTTP_POST, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST,GET,OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "*");
        //Serial.println("[FOTA][HTTP] POST tamamlandı, response gönderiliyor.");
        server.send(200, "text/plain", "OK: File at ESP32. STM32 transfer part.");
    }, []() {
        HTTPUpload& upload = server.upload();

        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("[FOTA][UPLOAD] Dosya adı: %s\n", upload.filename.c_str());
            LittleFS.remove("/stm32_fw.bin");
            fotaFile = LittleFS.open("/stm32_fw.bin", FILE_WRITE);
            if (fotaFile) {
                Serial.println("[FOTA][UPLOAD] Yeni dosya açıldı, yazılmaya hazır.");
            } else {
                Serial.println("[FOTA][UPLOAD][HATA] Dosya açılamadı!");
            }

        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (fotaFile) {
                size_t written = fotaFile.write(upload.buf, upload.currentSize);
                Serial.printf("[FOTA][UPLOAD] Chunk yazıldı: %d byte (toplam: %d byte)\n", written, upload.totalSize);
            } else {
                Serial.println("[FOTA][UPLOAD][HATA] Dosya handle geçersiz! Yazma atlandı.");
            }

        } else if (upload.status == UPLOAD_FILE_END) {
            if (fotaFile) {
                fotaFile.close();
                Serial.println("[FOTA][UPLOAD] Dosya kapatıldı.");
            }
            Serial.printf("[FOTA][UPLOAD] ─── UPLOAD TAMAMLANDI: %u byte ───\n", upload.totalSize);
            verifyAndCompleteFOTA();

        } else if (upload.status == UPLOAD_FILE_ABORTED) {
            Serial.println("[FOTA][UPLOAD][HATA] Upload IPTAL EDİLDİ!");
            if (fotaFile) fotaFile.close();
            LittleFS.remove("/stm32_fw.bin");
        }
    });
    Serial.println("[FOTA] Route tanımlandı. Server henüz başlatılmadı.");

}
