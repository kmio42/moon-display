#include "png_stream.h"

#include <string.h>

namespace {

// CRC-32 (Polynom 0xEDB88320) mit 16er-Tabelle, halbbyteweise
const uint32_t CRC_TABLE[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4, 0x4DB26158, 0x5005713C,
    0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C, 0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C,
};

uint32_t crcUpdate(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        crc = (crc >> 4) ^ CRC_TABLE[crc & 0x0F];
        crc = (crc >> 4) ^ CRC_TABLE[crc & 0x0F];
    }
    return crc;
}

// Adler-32 über die unkomprimierten Daten (Prüfsumme des zlib-Datenstroms)
void adlerUpdate(uint32_t& s1, uint32_t& s2, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        s1 = (s1 + data[i]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
}

void putBE32(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}

constexpr size_t SIGNATURE_LEN   = 8;
constexpr size_t IHDR_CHUNK_LEN  = 4 + 4 + 13 + 4;  // Länge, Typ, Daten, CRC
constexpr size_t CHUNK_HEAD_LEN  = 4 + 4;           // Länge, Typ
constexpr size_t ZLIB_HEADER_LEN = 2;
constexpr size_t STORED_HEAD_LEN = 5;               // BFINAL/BTYPE, LEN, NLEN
constexpr size_t ADLER_LEN       = 4;
constexpr size_t CRC_LEN         = 4;
constexpr size_t IEND_CHUNK_LEN  = 12;

// Größter Block: eine Zeile mit Deflate-Kopf
uint8_t buffer[STORED_HEAD_LEN + 1 + PNG_STREAM_MAX_WIDTH * 3];

int width, height;
uint16_t (*pixelFn)(int x, int y);
int nextPiece;          // 0 = Kopf, 1..height = Zeilen, height+1 = Abschluss
uint32_t idatCrc;
uint32_t adler1, adler2;

size_t rowLen()   { return 1 + (size_t) width * 3; }  // Filterbyte + RGB
size_t idatLen()  { return ZLIB_HEADER_LEN + (size_t) height * (STORED_HEAD_LEN + rowLen()) + ADLER_LEN; }

} // namespace

void pngStreamBegin(int w, int h, uint16_t (*pixel)(int x, int y)) {
    width = (w > PNG_STREAM_MAX_WIDTH) ? PNG_STREAM_MAX_WIDTH : w;
    height = h;
    pixelFn = pixel;
    nextPiece = 0;
    adler1 = 1;
    adler2 = 0;
}

size_t pngStreamSize() {
    return SIGNATURE_LEN + IHDR_CHUNK_LEN + CHUNK_HEAD_LEN + idatLen() + CRC_LEN + IEND_CHUNK_LEN;
}

bool pngStreamMore() {
    return nextPiece <= height + 1;
}

const uint8_t* pngStreamNext(size_t& len) {
    uint8_t* p = buffer;

    if (nextPiece == 0) {
        // PNG-Signatur
        static const uint8_t SIGNATURE[SIGNATURE_LEN] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        memcpy(p, SIGNATURE, SIGNATURE_LEN);
        p += SIGNATURE_LEN;

        // IHDR: Breite, Höhe, 8 Bit, Farbtyp 2 (RGB), Kompression 0, Filter 0, kein Interlace
        putBE32(p, 13);
        uint8_t* chunk = p + 4;
        memcpy(chunk, "IHDR", 4);
        putBE32(chunk + 4, width);
        putBE32(chunk + 8, height);
        chunk[12] = 8;
        chunk[13] = 2;
        chunk[14] = 0;
        chunk[15] = 0;
        chunk[16] = 0;
        putBE32(chunk + 17, crcUpdate(0xFFFFFFFF, chunk, 4 + 13) ^ 0xFFFFFFFF);
        p += IHDR_CHUNK_LEN;

        // IDAT-Kopf und zlib-Header (Deflate, 32K-Fenster, ohne Kompression)
        putBE32(p, idatLen());
        memcpy(p + 4, "IDAT", 4);
        idatCrc = crcUpdate(0xFFFFFFFF, p + 4, 4);
        p += CHUNK_HEAD_LEN;
        p[0] = 0x78;
        p[1] = 0x01;
        idatCrc = crcUpdate(idatCrc, p, ZLIB_HEADER_LEN);
        p += ZLIB_HEADER_LEN;
    } else if (nextPiece <= height) {
        // Eine Bildzeile als unkomprimierter Deflate-Block
        const int y = nextPiece - 1;
        const uint16_t n = rowLen();
        p[0] = (y == height - 1) ? 1 : 0;  // BFINAL beim letzten Block, BTYPE 00
        p[1] = n & 0xFF;
        p[2] = n >> 8;
        p[3] = ~n & 0xFF;
        p[4] = (~n >> 8) & 0xFF;
        uint8_t* row = p + STORED_HEAD_LEN;
        row[0] = 0;  // Filter: keiner
        for (int x = 0; x < width; x++) {
            const uint16_t c = pixelFn(x, y);
            const uint8_t r5 = (c >> 11) & 0x1F;
            const uint8_t g6 = (c >> 5) & 0x3F;
            const uint8_t b5 = c & 0x1F;
            row[1 + x * 3]     = (r5 << 3) | (r5 >> 2);
            row[1 + x * 3 + 1] = (g6 << 2) | (g6 >> 4);
            row[1 + x * 3 + 2] = (b5 << 3) | (b5 >> 2);
        }
        adlerUpdate(adler1, adler2, row, n);
        p += STORED_HEAD_LEN + n;
        idatCrc = crcUpdate(idatCrc, buffer, p - buffer);
    } else {
        // Adler-32, IDAT-CRC und IEND
        putBE32(p, (adler2 << 16) | adler1);
        idatCrc = crcUpdate(idatCrc, p, ADLER_LEN);
        p += ADLER_LEN;
        putBE32(p, idatCrc ^ 0xFFFFFFFF);
        p += CRC_LEN;
        static const uint8_t IEND[IEND_CHUNK_LEN] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82 };
        memcpy(p, IEND, IEND_CHUNK_LEN);
        p += IEND_CHUNK_LEN;
    }

    nextPiece++;
    len = p - buffer;
    return buffer;
}
