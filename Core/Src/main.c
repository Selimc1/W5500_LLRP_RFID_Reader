#include <stdio.h>
#include <string.h>
#include "stm32f4xx.h"

#define MAX_TAGS 50

// --- AĞ (NETWORK) AYARLARI ---
#define READER_PORT        5084
static const uint8_t MAC_ADDR[]    = {0x00, 0x08, 0xDC, 0x11, 0x22, 0x33};
static const uint8_t GATEWAY_IP[]  = {169, 254, 1, 1};
static const uint8_t SUBNET_MASK[] = {255, 255, 0, 0};
static const uint8_t LOCAL_IP[]    = {169, 254, 1, 100};
static const uint8_t TARGET_IP[]   = {169, 254, 1, 1};

// --- LLRP SABİTLERİ VE TİPLERİ ---
#define LLRP_MSG_KEEPALIVE         0x46 // Type 70
#define LLRP_MSG_RO_ACCESS_REPORT  0x3D // Type 61
#define LLRP_PARAM_TAG_REPORT_DATA 0xF0 // Type 240
#define LLRP_PARAM_EPC_96          0x8D // Type 141
#define LLRP_PARAM_ANTENNA_ID      0x81 // Type 129
#define LLRP_PARAM_PEAK_RSSI       0x86 // Type 134

// --- LLRP PAKET DİZİLERİ ---
// 0. DELETE_ROSPEC (Okuyucuda kalan eski ayarları siler)
uint8_t llrp_delete_rospec[] = {
    0x04, 0x15, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x04,
    0x00, 0x00, 0x00, 0x00 // ROSpecID: 0
};

// 1. ADD_ROSPEC (75 Byte - Hata vermeyen, AntenID ve RSSI aktif paket)
uint8_t llrp_add_rospec[] = {
    0x04, 0x14, 0x00, 0x00, 0x00, 0x4B, 0x00, 0x00, 0x00, 0x01,
    0x00, 0xB1, 0x00, 0x41, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0xB2, 0x00, 0x12, 0x00, 0xB3, 0x00, 0x05, 0x00, 0x00,
    0xB6, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7,
    0x00, 0x18, 0x00, 0x01, 0x00, 0x00, 0x00, 0xB8, 0x00, 0x09,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBA, 0x00, 0x07, 0x00,
    0x01, 0x01, 0x00, 0xED, 0x00, 0x0D, 0x01, 0x00, 0x01, 0x00,
    0xEE, 0x00, 0x06, 0x14, 0x00
};

// 2. ENABLE_ROSPEC (Eklenen konfigürasyonu etkinleştirir)
uint8_t llrp_enable_rospec[] = {
    0x04, 0x18, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x01
};

// 3. START_ROSPEC (Antenleri ateşler, etiketleri okumaya başlar)
uint8_t llrp_start_rospec[] = {
    0x04, 0x16, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x00, 0x01
};

// Okuyucu bağlantıyı koparmasın diye gönderilen Keepalive Cevabı
uint8_t llrp_keepalive_ack[] = {
    0x04, 0x48, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00
};


// --- ZAMANLAYICI (SYSTICK) İLE GÜVENLİ DELAY ---
volatile uint32_t sysTickCounter = 0;

void SysTick_Handler(void) {
    if (sysTickCounter != 0) {
        sysTickCounter--;
    }
}

// Milisaniye bazlı kesin bekleme. SysTick_Config ayarlanmalı.
void delay_ms(uint32_t ms) {
    sysTickCounter = ms;
    while(sysTickCounter != 0) {
        // Bekle
    }
}

// Döngüsel basit delay (eğer Systick kurulu değilse diye yedek)
void delay_cycles(volatile uint32_t count){
    while(count--){
        __asm("NOP"); // Derleyicinin döngüyü silmesini önler
    }
}


// --- USART2 FONKSİYONLARI ---
void USART_Init(void){
    RCC->AHB1ENR |= (1 << 0); // GPIOA Clock
    GPIOA->MODER &= ~(3 << 4);
    GPIOA->MODER |= (2 << 4); // PA2 AF Mode
    GPIOA->AFR[0] &= ~(0xF << 8);
    GPIOA->AFR[0] |= (7 << 8); // AF7 (USART2)

    RCC->APB1ENR |= (1 << 17); // USART2 Clock
    USART2->BRR = 0x008B;      // 115200 Baud (16MHz için)
    USART2->CR1 |= (1 << 3);   // TX Enable
    USART2->CR1 |= (1 << 13);  // USART Enable
}

void USART_SendChar(char c){
    while(!(USART2->SR & (1 << 7)));
    USART2->DR = c;
}

void USART_SendString(char *str){
    while(*str) {
        USART_SendChar(*str++);
    }
}

// --- SPI1 FONKSİYONLARI ---
void SPI1_Init(void){
    RCC->AHB1ENR |= (1 << 0); // GPIOA Clock
    RCC->APB2ENR |= (1 << 12); // SPI1 Clock

    // PA4 (CS) Çıkış Modu
    GPIOA->MODER &= ~(3 << 8);
    GPIOA->MODER |= (1 << 8);
    GPIOA->ODR |= (1 << 4); // CS HIGH başlasın

    // PA5 (SCK), PA6 (MISO), PA7 (MOSI) AF Modu
    GPIOA->MODER &= ~((3 << 10) | (3 << 12) | (3 << 14));
    GPIOA->MODER |=  ((2 << 10) | (2 << 12) | (2 << 14));
    GPIOA->AFR[0] &= ~((0xF << 20) | (0xF << 24) | (0xF << 28));
    GPIOA->AFR[0] |=  ((5 << 20) | (5 << 24) | (5 << 28));

    // SPI Kurulumu (Master, fPCLK/4, Software Slave Management)
    SPI1->CR1 = 0;
    SPI1->CR1 |= (1 << 2) | (1 << 3) | (1 << 9) | (1 << 8);
    SPI1->CR1 |= (1 << 6); // SPI Enable
}

uint8_t SPI1_WriteRead(uint8_t data){
    while(!(SPI1->SR & (1 << 1))); // TXE bekle
    SPI1->DR = data;
    while(!(SPI1->SR & (1 << 0))); // RXNE bekle
    return (uint8_t)SPI1->DR;
}

// --- W5500 KAYITCI (REGISTER) OKUMA / YAZMA ---
uint8_t W5500_ReadReg(uint16_t address, uint8_t block){
    uint8_t data;
    GPIOA->ODR &= ~(1 << 4);
    SPI1_WriteRead((address >> 8) & 0xFF);
    SPI1_WriteRead(address & 0xFF);
    SPI1_WriteRead(block | 0x00);
    data = SPI1_WriteRead(0xFF);
    GPIOA->ODR |= (1 << 4);
    return data;
}

void W5500_WriteReg(uint16_t address, uint8_t block, uint8_t data){
    GPIOA->ODR &= ~(1 << 4);
    SPI1_WriteRead((address >> 8) & 0xFF);
    SPI1_WriteRead(address & 0xFF);
    SPI1_WriteRead(block | 0x04);
    SPI1_WriteRead(data);
    GPIOA->ODR |= (1 << 4);
}

// --- W5500 VERİ GÖNDERME VE ALMA ---
void W5500_SendBytes(uint8_t *buf, uint16_t len) {
    uint16_t ptr = (W5500_ReadReg(0x0024, 0x08) << 8) | W5500_ReadReg(0x0025, 0x08);
    for(uint16_t i = 0; i < len; i++) {
        W5500_WriteReg(ptr, 0x10, buf[i]);
        ptr++;
    }
    W5500_WriteReg(0x0024, 0x08, (ptr >> 8));
    W5500_WriteReg(0x0025, 0x08, (ptr & 0xFF));
    W5500_WriteReg(0x0001, 0x08, 0x20); // SEND komutu
}

void W5500_ReadBytes(uint8_t *buf, uint16_t len) {
    uint16_t ptr = (W5500_ReadReg(0x0028, 0x08) << 8) | W5500_ReadReg(0x0029, 0x08);
    for(uint16_t i = 0; i < len; i++) {
        buf[i] = W5500_ReadReg(ptr, 0x18);
        ptr++;
    }
    W5500_WriteReg(0x0028, 0x08, (ptr >> 8));
    W5500_WriteReg(0x0029, 0x08, (ptr & 0xFF));
    W5500_WriteReg(0x0001, 0x08, 0x40); // RECV komutu
}

// --- W5500 AĞ AYARLARI ---
void W5500_InitNetwork(void){
    W5500_WriteReg(0x0000, 0x00, 0x80); // MR = Software Reset
    delay_cycles(100000); // 100ms kadar bekle (SysTick de kullanılabilir)

    // MAC Adresi
    for(int i = 0; i < 6; i++) {
        W5500_WriteReg(0x0009 + i, 0x00, MAC_ADDR[i]);
    }

    // Gateway Adresi
    for(int i = 0; i < 4; i++) {
        W5500_WriteReg(0x0001 + i, 0x00, GATEWAY_IP[i]);
    }

    // Subnet Maskesi
    for(int i = 0; i < 4; i++) {
        W5500_WriteReg(0x0005 + i, 0x00, SUBNET_MASK[i]);
    }

    // Yerel (W5500) IP Adresi
    for(int i = 0; i < 4; i++) {
        W5500_WriteReg(0x000F + i, 0x00, LOCAL_IP[i]);
    }

    // Bellek Boyutları (Socket 0 için 2KB)
    W5500_WriteReg(0x001E, 0x08, 2); // RX Buf Size
    W5500_WriteReg(0x001F, 0x08, 2); // TX Buf Size

    USART_SendString("Network Configured. \r\n");
}

void W5500_TCP_Connect(void){
    // Fiziksel bağlantı (Ethernet kablosu) takılı mı kontrol et
    uint8_t phy_status = W5500_ReadReg(0x002E, 0x00);
    if ((phy_status & 0x01) == 0) {
        USART_SendString("HATA: Ethernet Kablosu Takili Degil!\r\n");
        return;
    }

    // Soket Ayarları
    W5500_WriteReg(0x0000, 0x08, 0x01); // Sn_MR = TCP (0x01)

    // Her bağlantıda farklı bir Source Port (Bağlantı çakışmasını önler)
    static uint16_t src_port = 50000;
    src_port++;
    W5500_WriteReg(0x0004, 0x08, (src_port >> 8));
    W5500_WriteReg(0x0005, 0x08, (src_port & 0xFF));

    // Hedef RFID Okuyucu IP
    for(int i = 0; i < 4; i++) {
        W5500_WriteReg(0x000C + i, 0x08, TARGET_IP[i]);
    }

    // Hedef Port (LLRP Portu)
    W5500_WriteReg(0x0010, 0x08, (READER_PORT >> 8));
    W5500_WriteReg(0x0011, 0x08, (READER_PORT & 0xFF));

    // Soketi Aç
    W5500_WriteReg(0x0001, 0x08, 0x01); // Sn_CR = OPEN
    uint32_t timeout = 500000;
    while(W5500_ReadReg(0x0001, 0x08) != 0x00) {
        if(--timeout == 0) {
            USART_SendString("HATA: Soket Acilamadi (Timeout)!\r\n");
            return;
        }
    }
}

// --- LLRP PROFESYONEL AYRIŞTIRICI (PARSER) ---
void Parse_LLRP_Buffer(uint8_t *buf, uint16_t len) {
    uint16_t idx = 0;

    while (idx + 10 <= len) { // En az bir header (10 byte) olmalı
        if (buf[idx] != 0x04) {
            idx++; // LLRP başlangıç byte'ı değilse atla (Senkronizasyon)
            continue;
        }

        uint8_t msg_type = buf[idx + 1];
        uint32_t msg_len = (buf[idx + 2] << 24) | (buf[idx + 3] << 16) | (buf[idx + 4] << 8) | buf[idx + 5];

        if (msg_len == 0 || idx + msg_len > len) {
            break; // Güvenlik çıkışı (Parçalı paket geldiğinde vb.)
        }

        // 1. Durum: KEEPALIVE Geldi -> Hemen ACK Cevabı Dön
        if (msg_type == LLRP_MSG_KEEPALIVE) {
            W5500_SendBytes(llrp_keepalive_ack, sizeof(llrp_keepalive_ack));
        }
        // 2. Durum: ETİKET OKUNDU -> Verileri ayrıştır ve ekrana bas
        else if (msg_type == LLRP_MSG_RO_ACCESS_REPORT) {
            uint16_t offset = idx + 10; // Header'ı atla

            while (offset < idx + msg_len) {
                // TagReportData tespiti (0x00 0xF0)
                if (buf[offset] == 0x00 && buf[offset+1] == LLRP_PARAM_TAG_REPORT_DATA) {
                    uint16_t tlv_len = (buf[offset+2] << 8) | buf[offset+3];
                    uint16_t sub_offset = offset + 4;

                    uint8_t *epc = NULL;
                    uint8_t ant = 0;
                    int8_t rssi = 0;

                    // TLV içindeki parametreleri dolaş (EPC, Anten, RSSI)
                    while (sub_offset < offset + tlv_len && sub_offset < idx + msg_len) {
                        uint8_t p_type = buf[sub_offset];

                        if (p_type == LLRP_PARAM_EPC_96) {
                            epc = &buf[sub_offset + 1];
                            sub_offset += 13;
                        }
                        else if (p_type == LLRP_PARAM_ANTENNA_ID) {
                            ant = buf[sub_offset + 2];
                            sub_offset += 3;
                        }
                        else if (p_type == LLRP_PARAM_PEAK_RSSI) {
                            rssi = (int8_t)buf[sub_offset + 1];
                            sub_offset += 2;
                        } else {
                            break; // Desteklenmeyen parametre, döngüden çık
                        }
                    }

                    // EPC Bulunduysa buffer'a taşırmadan yazdır (sprintf yerine snprintf kullanımı)
                    if (epc != NULL) {
                        char msg[120];
                        snprintf(msg, sizeof(msg), "[ETIKET] EPC: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X | Ant: %d | RSSI: %d dBm\r\n",
                                epc[0], epc[1], epc[2], epc[3], epc[4], epc[5],
                                epc[6], epc[7], epc[8], epc[9], epc[10], epc[11],
                                ant, rssi);
                        USART_SendString(msg);
                    }
                    offset += tlv_len;
                } else {
                    break;
                }
            }
        }

        // Bir sonraki LLRP paketine atla
        idx += msg_len;
    }
}

int main(void){
    SystemInit();

    // İşlemci frekansınıza göre ms bazlı gecikme sağlamak için Systick ayarlanmalıdır.
    // Örnek: SystemCoreClock / 1000 = 1ms interrupt sağlar
    // SysTick_Config(SystemCoreClock / 1000);

    USART_Init();
    SPI1_Init();

    USART_SendString("\r\nSystem Booting...\r\n");

    W5500_InitNetwork();

    uint8_t rx_buf[2048];
    uint8_t is_rospec_sent = 0;

    while(1) {
        uint8_t socket_status = W5500_ReadReg(0x0003, 0x08); // Sn_SR

        if (socket_status == 0x00) { // Soket Kapalı
            USART_SendString("Socket Closed. Retrying Connection...\r\n");
            W5500_TCP_Connect();
            delay_cycles(1000000);
            is_rospec_sent = 0; // Yeni bağlantıda komutlar tekrar yollanmalı
        }
        else if (socket_status == 0x13) { // Soket Açıldı, Bağlanıyor (INIT)
            W5500_WriteReg(0x0001, 0x08, 0x04); // CONNECT Komutu
            delay_cycles(1000000);
        }
        else if (socket_status == 0x17) { // BAĞLANTI KURULDU (ESTABLISHED)

            // Konfigürasyonlar (ROSpec) Sadece 1 Kere Gönderilmeli
            if (!is_rospec_sent) {
                USART_SendString("TCP ESTABLISHED. Configuring Reader...\r\n");

                // 0. Temizlik yap
                W5500_SendBytes(llrp_delete_rospec, sizeof(llrp_delete_rospec));
                delay_cycles(2000000);

                // 1. Ayarları Gönder
                W5500_SendBytes(llrp_add_rospec, sizeof(llrp_add_rospec));
                delay_cycles(2000000);

                // 2. Ayarları Aktifleştir
                W5500_SendBytes(llrp_enable_rospec, sizeof(llrp_enable_rospec));
                delay_cycles(2000000);

                // 3. Okumayı Başlat
                USART_SendString("Starting Inventory...\r\n");
                W5500_SendBytes(llrp_start_rospec, sizeof(llrp_start_rospec));

                is_rospec_sent = 1;
            }

            // Gelen veri var mı kontrol et (Sn_RX_RSR okuma)
            uint16_t rx_size = (W5500_ReadReg(0x0026, 0x08) << 8) | W5500_ReadReg(0x0027, 0x08);
            if (rx_size > 0) {
                if (rx_size > 2048) rx_size = 2048; // Buffer aşımını önle

                // Gelen ham datayı W5500 donanımından çek
                W5500_ReadBytes(rx_buf, rx_size);

                // Ham verileri çözümle ve ekrana yazdır
                Parse_LLRP_Buffer(rx_buf, rx_size);
            }
        }
    }
}
