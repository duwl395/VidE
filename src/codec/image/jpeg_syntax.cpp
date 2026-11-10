#include "codec/image/jpeg_syntax.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace vide {
namespace {

std::string num(unsigned value) { return std::to_string(value); }

// "0xFFD8" style marker label (the previous code printed decimal, e.g. 0xFF216).
std::string hexMarker(uint8_t marker) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0xFF%02X", marker);
    return buffer;
}

const char* markerName(uint8_t marker) {
    switch (marker) {
        case 0xC0: return "SOF0 (baseline DCT)";
        case 0xC1: return "SOF1 (extended sequential)";
        case 0xC2: return "SOF2 (progressive)";
        case 0xC3: return "SOF3 (lossless)";
        case 0xC4: return "DHT (huffman table)";
        case 0xC9: return "SOF9 (arithmetic)";
        case 0xCA: return "SOF10 (progressive arithmetic)";
        case 0xCC: return "DAC (arithmetic conditioning)";
        case 0xD8: return "SOI (start of image)";
        case 0xD9: return "EOI (end of image)";
        case 0xDA: return "SOS (start of scan)";
        case 0xDB: return "DQT (quantisation table)";
        case 0xDD: return "DRI (restart interval)";
        case 0xE0: return "APP0 (JFIF)";
        case 0xE1: return "APP1 (Exif/XMP)";
        case 0xE2: return "APP2 (ICC)";
        case 0xEE: return "APP14 (Adobe)";
        case 0xFE: return "COM (comment)";
        default: break;
    }
    if (marker >= 0xE0 && marker <= 0xEF) {
        return "APPn";
    }
    return "marker";
}

std::string sofInfo(const uint8_t* p, std::size_t len) {
    if (len < 6) {
        return "";
    }
    const int precision = p[0];
    const int height = (p[1] << 8) | p[2];
    const int width = (p[3] << 8) | p[4];
    const int components = p[5];
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "precision=%d %dx%d components=%d", precision, width,
                  height, components);
    return buffer;
}

// JFIF APP0 payload: "JFIF\0" ver_major ver_minor units Xdensity Ydensity
// Xthumb Ythumb.
void addJfifFields(SyntaxBuilder& builder, uint32_t node, const uint8_t* p, std::size_t len) {
    if (len < 14 || !(p[0] == 'J' && p[1] == 'F' && p[2] == 'I' && p[3] == 'F')) {
        return;
    }
    builder.addField(node, "identifier", "JFIF", -1, -1);
    builder.addField(node, "version", num(p[5]) + "." + num(p[6]), -1, -1);
    const char* units = p[7] == 0 ? "aspect ratio" : (p[7] == 1 ? "dots/inch" : "dots/cm");
    builder.addField(node, "units", std::string(num(p[7])) + " (" + units + ")", -1, -1);
    builder.addField(node, "x_density", num((p[8] << 8) | p[9]), -1, -1);
    builder.addField(node, "y_density", num((p[10] << 8) | p[11]), -1, -1);
    builder.addField(node, "thumbnail", num(p[12]) + "x" + num(p[13]), -1, -1);
}

void addDqtFields(SyntaxBuilder& builder, uint32_t node, const uint8_t* p, std::size_t len) {
    std::size_t off = 0;
    int index = 0;
    while (off < len) {
        const uint8_t pq_tq = p[off];
        const int precision = pq_tq >> 4;  // 0 = 8-bit, 1 = 16-bit
        const int id = pq_tq & 0x0F;
        const std::size_t table_bytes = precision == 0 ? 64u : 128u;
        if (off + 1 + table_bytes > len) {
            break;
        }
        const uint32_t table = builder.addField(
            node, "table", "id=" + num(id) + " precision=" + (precision == 0 ? "8-bit" : "16-bit"),
            -1, -1);
        builder.addField(table, "destination_id", num(id), -1, -1);
        builder.addField(table, "precision", precision == 0 ? "8-bit" : "16-bit", -1, -1);
        // DQT stores the 64 coefficients in zig-zag order.
        const uint32_t values = builder.addStructure(table, "values (zig-zag)");
        for (int i = 0; i < 64; ++i) {
            unsigned v;
            if (precision == 0) {
                v = p[off + 1 + static_cast<std::size_t>(i)];
            } else {
                v = (static_cast<unsigned>(p[off + 1 + 2 * i]) << 8) |
                    p[off + 2 + 2 * i];
            }
            builder.addField(values, "q" + num(static_cast<unsigned>(i)), num(v), -1, -1);
        }
        off += 1 + table_bytes;
        ++index;
    }
    builder.addField(node, "table_count", num(index), -1, -1, SyntaxKind::Derived);
}

void addDhtFields(SyntaxBuilder& builder, uint32_t node, const uint8_t* p, std::size_t len) {
    std::size_t off = 0;
    int index = 0;
    while (off + 17 <= len) {
        const uint8_t tc_th = p[off];
        const int cls = tc_th >> 4;  // 0 = DC, 1 = AC
        const int id = tc_th & 0x0F;
        unsigned symbols = 0;
        for (int i = 0; i < 16; ++i) {
            symbols += p[off + 1 + i];
        }
        if (off + 17 + symbols > len) {
            break;
        }
        const uint32_t table = builder.addField(
            node, "table",
            std::string(cls == 0 ? "DC" : "AC") + " id=" + num(id) + " symbols=" + num(symbols),
            -1, -1);
        builder.addField(table, "class", cls == 0 ? "DC" : "AC", -1, -1);
        builder.addField(table, "destination_id", num(id), -1, -1);
        builder.addField(table, "symbol_count", num(symbols), -1, -1);
        // 16 code-length counts (BITS[1..16]) followed by the symbol list.
        const uint32_t lengths = builder.addStructure(table, "code_lengths");
        for (int i = 0; i < 16; ++i) {
            builder.addField(lengths, "bits[" + num(static_cast<unsigned>(i + 1)) + "]",
                             num(p[off + 1 + static_cast<std::size_t>(i)]), -1, -1);
        }
        std::string symbols_text;
        const std::size_t shown = std::min<std::size_t>(symbols, 256);
        for (std::size_t k = 0; k < shown; ++k) {
            char byte_text[4];
            std::snprintf(byte_text, sizeof(byte_text), "%02X", p[off + 17 + k]);
            if (!symbols_text.empty()) {
                symbols_text += ' ';
            }
            symbols_text += byte_text;
        }
        if (symbols > shown) {
            symbols_text += " ...";
        }
        builder.addField(table, "symbols", symbols_text, -1, -1);
        off += 17 + symbols;
        ++index;
    }
    builder.addField(node, "table_count", num(index), -1, -1, SyntaxKind::Derived);
}

void addSosFields(SyntaxBuilder& builder, uint32_t node, const uint8_t* p, std::size_t len) {
    if (len < 1) {
        return;
    }
    const int components = p[0];
    builder.addField(node, "components", num(components), -1, -1);
    const std::size_t need = 1 + static_cast<std::size_t>(components) * 2 + 3;
    if (len < need) {
        return;
    }
    const std::size_t tail = 1 + static_cast<std::size_t>(components) * 2;
    builder.addField(node, "Ss", num(p[tail + 0]), -1, -1);
    builder.addField(node, "Se", num(p[tail + 1]), -1, -1);
    builder.addField(node, "Ah", num(p[tail + 2] >> 4), -1, -1);
    builder.addField(node, "Al", num(p[tail + 2] & 0x0F), -1, -1);
}

}  // namespace

void parseJpegSyntax(const uint8_t* data, std::size_t size, SyntaxBuilder& builder,
                     uint32_t parent) {
    if (data == nullptr || size < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        return;
    }

    std::size_t offset = 0;
    while (offset + 1 < size) {
        if (data[offset] != 0xFF) {
            ++offset;
            continue;
        }
        const uint8_t marker = data[offset + 1];
        if (marker == 0xFF) {  // fill byte
            ++offset;
            continue;
        }

        // Standalone markers (no length field): SOI, EOI, TEM, RSTn.
        if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 ||
            (marker >= 0xD0 && marker <= 0xD7)) {
            builder.addField(parent, "marker",
                             std::string(markerName(marker)) + " (" + hexMarker(marker) + ")",
                             static_cast<int64_t>(offset) * 8, 16);
            offset += 2;
            continue;
        }

        if (offset + 3 >= size) {
            break;
        }
        const std::size_t seg_len = (static_cast<std::size_t>(data[offset + 2]) << 8) |
                                    data[offset + 3];
        const int64_t seg_bit_offset = static_cast<int64_t>(offset) * 8;
        const int64_t seg_bit_length = (2 + seg_len) * 8;

        const uint32_t node = builder.addField(
            parent, "marker",
            std::string(markerName(marker)) + " (" + hexMarker(marker) + ")", seg_bit_offset,
            seg_bit_length);
        builder.addField(node, "length", num(static_cast<unsigned>(seg_len)), -1, -1);

        const std::size_t payload = offset + 4;
        const std::size_t payload_len = seg_len >= 2 ? seg_len - 2 : 0;

        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
            marker != 0xCC) {
            builder.addField(node, "frame_info", sofInfo(data + payload, payload_len), -1, -1);
        } else if (marker == 0xDB) {
            addDqtFields(builder, node, data + payload, payload_len);
        } else if (marker == 0xC4) {
            addDhtFields(builder, node, data + payload, payload_len);
        } else if (marker == 0xE0) {
            addJfifFields(builder, node, data + payload, payload_len);
        } else if (marker == 0xFE) {
            std::string comment(reinterpret_cast<const char*>(data + payload),
                                payload_len < 64 ? payload_len : 64);
            builder.addField(node, "comment", comment, -1, -1);
        } else if (marker == 0xDD && payload_len >= 2) {
            builder.addField(node, "restart_interval",
                             num((data[payload] << 8) | data[payload + 1]), -1, -1);
        } else if (marker == 0xDA) {
            addSosFields(builder, node, data + payload, payload_len);
            builder.addField(node, "scan_data", "entropy-coded (not parsed)", -1, -1,
                             SyntaxKind::Reserved);
            break;
        }

        offset += 2 + seg_len;
    }

    builder.addField(parent, "byte_size", num(static_cast<unsigned>(size)), 0, -1,
                     SyntaxKind::Derived);
}

}  // namespace vide
