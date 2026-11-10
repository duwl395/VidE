#include "codec/raw/raw_video_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace vide {
namespace {

inline uint8_t clamp8(int value) {
    return static_cast<uint8_t>(value < 0 ? 0 : (value > 255 ? 255 : value));
}

inline void yuvToRgb(int y, int u, int v, uint8_t* rgb) {
    const int c = y - 16;
    const int d = u - 128;
    const int e = v - 128;
    const int scaled = c < 0 ? 0 : c;
    rgb[0] = clamp8((298 * scaled + 409 * e + 128) >> 8);
    rgb[1] = clamp8((298 * scaled - 100 * d - 208 * e + 128) >> 8);
    rgb[2] = clamp8((298 * scaled + 516 * d + 128) >> 8);
}

inline int chromaWidth(int width) { return (width + 1) / 2; }
inline int chromaHeight(int height) { return (height + 1) / 2; }

// Converts one raw frame in `format` to tightly packed RGBA in `out`.
void convertToRgba(PixelFormat format, int width, int height, const uint8_t* src,
                   std::vector<uint8_t>& out) {
    out.resize(static_cast<std::size_t>(width) * height * 4);
    const int cw = chromaWidth(width);
    const int ch = chromaHeight(height);

    switch (format) {
        case PixelFormat::I420:
        case PixelFormat::YUV422P:
        case PixelFormat::YUV444P:
        case PixelFormat::YUV400: {
            const uint8_t* y_plane = src;
            const uint8_t* u_plane = nullptr;
            const uint8_t* v_plane = nullptr;
            int c_stride = 0;
            int sub_x = 1;
            int sub_y = 1;
            if (format == PixelFormat::I420) {
                u_plane = y_plane + static_cast<std::size_t>(width) * height;
                v_plane = u_plane + static_cast<std::size_t>(cw) * ch;
                c_stride = cw;
                sub_x = 2;
                sub_y = 2;
            } else if (format == PixelFormat::YUV422P) {
                u_plane = y_plane + static_cast<std::size_t>(width) * height;
                v_plane = u_plane + static_cast<std::size_t>(cw) * height;
                c_stride = cw;
                sub_x = 2;
                sub_y = 1;
            } else if (format == PixelFormat::YUV444P) {
                u_plane = y_plane + static_cast<std::size_t>(width) * height;
                v_plane = u_plane + static_cast<std::size_t>(width) * height;
                c_stride = width;
                sub_x = 1;
                sub_y = 1;
            }
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int Y = y_plane[static_cast<std::size_t>(y) * width + x];
                    int U = 128;
                    int V = 128;
                    if (u_plane != nullptr) {
                        const std::size_t ci = static_cast<std::size_t>(y / sub_y) * c_stride +
                                               static_cast<std::size_t>(x / sub_x);
                        U = u_plane[ci];
                        V = v_plane[ci];
                    }
                    yuvToRgb(Y, U, V,
                             &out[(static_cast<std::size_t>(y) * width + x) * 4]);
                    out[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = 255;
                }
            }
            break;
        }
        case PixelFormat::NV12:
        case PixelFormat::NV21: {
            const uint8_t* y_plane = src;
            const uint8_t* uv_plane = y_plane + static_cast<std::size_t>(width) * height;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int Y = y_plane[static_cast<std::size_t>(y) * width + x];
                    const std::size_t uv_index =
                        (static_cast<std::size_t>(y / 2) * cw + (x / 2)) * 2;
                    const int a = uv_plane[uv_index];
                    const int b = uv_plane[uv_index + 1];
                    const int U = (format == PixelFormat::NV12) ? a : b;
                    const int V = (format == PixelFormat::NV12) ? b : a;
                    yuvToRgb(Y, U, V, &out[(static_cast<std::size_t>(y) * width + x) * 4]);
                    out[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = 255;
                }
            }
            break;
        }
        case PixelFormat::P010:
        case PixelFormat::P012:
        case PixelFormat::P016: {
            const uint16_t* y16 = reinterpret_cast<const uint16_t*>(src);
            const uint16_t* uv16 = y16 + static_cast<std::size_t>(width) * height;
            // All P0xx samples are MSB-aligned, so `>> 8` maps them to 8-bit.
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const int Y = y16[static_cast<std::size_t>(y) * width + x] >> 8;
                    const std::size_t uv_index =
                        (static_cast<std::size_t>(y / 2) * cw + (x / 2)) * 2;
                    const int U = uv16[uv_index] >> 8;
                    const int V = uv16[uv_index + 1] >> 8;
                    yuvToRgb(Y, U, V, &out[(static_cast<std::size_t>(y) * width + x) * 4]);
                    out[(static_cast<std::size_t>(y) * width + x) * 4 + 3] = 255;
                }
            }
            break;
        }
        case PixelFormat::RGB888: {
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t s = (static_cast<std::size_t>(y) * width + x) * 3;
                    const std::size_t d = (static_cast<std::size_t>(y) * width + x) * 4;
                    out[d + 0] = src[s + 0];
                    out[d + 1] = src[s + 1];
                    out[d + 2] = src[s + 2];
                    out[d + 3] = 255;
                }
            }
            break;
        }
        default:
            break;
    }
}

void downscaleRgba(const DecodedImage& src, int dst_w, int dst_h, DecodedImage& out) {
    out.width = dst_w;
    out.height = dst_h;
    out.format = PixelFormat::RGBA8888;
    out.bit_depth = BitDepth::B8;
    out.data.resize(static_cast<std::size_t>(dst_w) * dst_h * 4);

    for (int y = 0; y < dst_h; ++y) {
        const int sy = std::min(src.height - 1, y * src.height / dst_h);
        for (int x = 0; x < dst_w; ++x) {
            const int sx = std::min(src.width - 1, x * src.width / dst_w);
            const uint8_t* s = &src.data[(static_cast<std::size_t>(sy) * src.width + sx) * 4];
            uint8_t* d = &out.data[(static_cast<std::size_t>(y) * dst_w + x) * 4];
            std::memcpy(d, s, 4);
        }
    }
}

}  // namespace

int64_t rawFrameSize(PixelFormat format, int width, int height) {
    const int cw = chromaWidth(width);
    const int ch = chromaHeight(height);
    const int64_t luma = static_cast<int64_t>(width) * height;
    switch (format) {
        case PixelFormat::I420:
        case PixelFormat::NV12:
        case PixelFormat::NV21:
            return luma + 2LL * cw * ch;
        case PixelFormat::YUV422P:
            return luma + 2LL * cw * height;
        case PixelFormat::YUV444P:
            return luma * 3;
        case PixelFormat::YUV400:
            return luma;
        case PixelFormat::P010:
        case PixelFormat::P012:
        case PixelFormat::P016:
            return (luma + 2LL * cw * ch) * 2;
        case PixelFormat::RGB888:
            return luma * 3;
        default:
            return -1;
    }
}

namespace {

std::string toLowerCopy(const std::string& text) {
    std::string out = text;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool readUIntAt(const std::string& text, std::size_t& pos, int& value) {
    if (pos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[pos]))) {
        return false;
    }
    long v = 0;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        v = v * 10 + (text[pos] - '0');
        if (v > 1000000) {
            v = 1000000;
        }
        ++pos;
    }
    value = static_cast<int>(v);
    return true;
}

}  // namespace

RawNameInfo parseRawFileName(const std::string& path) {
    RawNameInfo info;
    const std::size_t slash = path.find_last_of("/\\");
    const std::string name =
        toLowerCopy(slash == std::string::npos ? path : path.substr(slash + 1));

    // Pixel format tokens (longest first so e.g. yuv420p wins over i420).
    struct Token {
        const char* text;
        PixelFormat format;
    };
    static const Token kTokens[] = {
        {"yuv420p", PixelFormat::I420},   {"i420", PixelFormat::I420},
        {"nv12", PixelFormat::NV12},      {"nv21", PixelFormat::NV21},
        {"yuv422p", PixelFormat::YUV422P}, {"yuv400", PixelFormat::YUV400},
        {"gray", PixelFormat::YUV400},    {"grey", PixelFormat::YUV400},
        {"yuv444p", PixelFormat::YUV444P}, {"p010", PixelFormat::P010},
        {"p012", PixelFormat::P012},      {"p016", PixelFormat::P016},
        {"rgb888", PixelFormat::RGB888},  {"rgb24", PixelFormat::RGB888},
    };
    for (const Token& token : kTokens) {
        if (name.find(token.text) != std::string::npos) {
            info.format = token.format;
            break;
        }
    }

    // Dimensions: "<width>x<height>" (also uppercase X).
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
            continue;
        }
        std::size_t j = i;
        int w = 0;
        int h = 0;
        if (!readUIntAt(name, j, w)) {
            continue;
        }
        if (j < name.size() && (name[j] == 'x' || name[j] == 'X') &&
            readUIntAt(name, ++j, h) && w >= 16 && h >= 16) {
            info.width = w;
            info.height = h;
            break;
        }
        i = j;
    }

    // Frame rate: "<n>fps", "<n>f" (word boundary), or "@<n>".
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '@') {
            std::size_t j = i + 1;
            int v = 0;
            if (readUIntAt(name, j, v) && v > 0) {
                info.fps_num = v;
                info.fps_den = 1;
                break;
            }
        }
        if (std::isdigit(static_cast<unsigned char>(name[i])) &&
            (i == 0 || !std::isdigit(static_cast<unsigned char>(name[i - 1])))) {
            std::size_t j = i;
            int v = 0;
            if (!readUIntAt(name, j, v) || v <= 0) {
                continue;
            }
            if (name.compare(j, 3, "fps") == 0) {
                info.fps_num = v;
                info.fps_den = 1;
                break;
            }
            if (j < name.size() && name[j] == 'f' &&
                (j + 1 >= name.size() ||
                 !std::isalpha(static_cast<unsigned char>(name[j + 1])))) {
                info.fps_num = v;
                info.fps_den = 1;
                break;
            }
        }
    }

    return info;
}

bool isRawFileExtension(const std::string& path) {
    const std::string lower = toLowerCopy(path);
    static const char* kExts[] = {".yuv", ".raw", ".rgb", ".gray", ".grey"};
    for (const char* ext : kExts) {
        const std::size_t n = std::strlen(ext);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0) {
            return true;
        }
    }
    return false;
}

bool RawVideoAdapter::probe(const std::string& path) const {
    (void)path;
    // Raw video is never auto-detected; it is selected explicitly through
    // OpenOptions::raw_format.
    return false;
}

Result<StreamInfo> RawVideoAdapter::open(const OpenOptions& options) {
    close();

    if (options.raw_width <= 0 || options.raw_height <= 0) {
        return Status::error(StatusCode::InvalidArgument,
                             "raw video requires --width and --height");
    }
    if (options.raw_format == PixelFormat::Unknown) {
        return Status::error(StatusCode::InvalidArgument,
                             "raw video requires --raw-format");
    }

    frame_size_ = rawFrameSize(options.raw_format, options.raw_width, options.raw_height);
    if (frame_size_ <= 0) {
        return Status::error(StatusCode::Unsupported, "unsupported raw pixel format");
    }

    file_.open(options.path, std::ios::binary | std::ios::ate);
    if (!file_.is_open()) {
        return Status::error(StatusCode::IoError, "cannot open '" + options.path + "'");
    }

    const std::streamoff total = file_.tellg();
    frame_count_ = total > 0 ? static_cast<int64_t>(total) / frame_size_ : 0;

    options_ = options;
    info_ = StreamInfo{};
    info_.codec = CodecId::RawVideo;
    info_.width = options.raw_width;
    info_.height = options.raw_height;
    info_.format = options.raw_format;
    info_.bit_depth = bytesPerSample(options.raw_format) == 2 ? BitDepth::B10 : BitDepth::B8;
    info_.chroma = ChromaFormat::C420;
    info_.fps_num = options.raw_fps_num > 0 ? options.raw_fps_num : 25;
    info_.fps_den = options.raw_fps_den > 0 ? options.raw_fps_den : 1;
    info_.frame_count = frame_count_;
    info_.container = "raw";

    return info_;
}

void RawVideoAdapter::close() {
    if (file_.is_open()) {
        file_.close();
    }
    info_ = StreamInfo{};
    frame_size_ = 0;
    frame_count_ = -1;
    options_ = OpenOptions{};
}

CodecCapabilities RawVideoAdapter::capabilities() const {
    CodecCapabilities caps;
    caps.syntax_tree = false;
    caps.block_info = false;
    caps.thumbnail = true;
    return caps;
}

Result<FrameMeta> RawVideoAdapter::frameMeta(uint32_t index) {
    if (!file_.is_open()) {
        return Status::error(StatusCode::InvalidArgument, "no raw stream open");
    }
    FrameMeta meta;
    meta.index = index;
    meta.width = info_.width;
    meta.height = info_.height;
    meta.format = info_.format;
    meta.bit_depth = info_.bit_depth;
    meta.codec = CodecId::RawVideo;
    meta.file_offset = static_cast<int64_t>(index) * frame_size_;
    meta.size_bytes = frame_size_;
    return meta;
}

Result<DecodedImage> RawVideoAdapter::decode(uint32_t index) {
    if (!file_.is_open()) {
        return Status::error(StatusCode::InvalidArgument, "no raw stream open");
    }
    if (frame_count_ >= 0 && static_cast<int64_t>(index) >= frame_count_) {
        return Status::error(StatusCode::InvalidArgument, "frame index out of range");
    }

    std::vector<uint8_t> buffer(static_cast<std::size_t>(frame_size_));
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(index) * frame_size_, std::ios::beg);
    file_.read(reinterpret_cast<char*>(buffer.data()), frame_size_);
    if (file_.gcount() != frame_size_) {
        return Status::error(StatusCode::IoError, "short read for raw frame");
    }

    DecodedImage image;
    image.width = info_.width;
    image.height = info_.height;
    image.format = PixelFormat::RGBA8888;
    image.bit_depth = BitDepth::B8;
    convertToRgba(info_.format, info_.width, info_.height, buffer.data(), image.data);
    return image;
}

Result<DecodedImage> RawVideoAdapter::thumbnail(uint32_t index, int max_width, int max_height) {
    auto full = decode(index);
    if (!full.ok()) {
        return full.status();
    }

    const DecodedImage& src = full.value();
    const double scale = std::min({static_cast<double>(max_width) / src.width,
                                   static_cast<double>(max_height) / src.height, 1.0});
    const int tw = std::max(1, static_cast<int>(src.width * scale));
    const int th = std::max(1, static_cast<int>(src.height * scale));
    if (tw == src.width && th == src.height) {
        return full;
    }

    DecodedImage out;
    downscaleRgba(src, tw, th, out);
    return out;
}

Result<SyntaxTree> RawVideoAdapter::syntax() {
    return Status::error(StatusCode::Unsupported, "raw video has no syntax tree");
}

Result<BlockGrid> RawVideoAdapter::blocks(uint32_t index) {
    (void)index;
    return Status::error(StatusCode::Unsupported, "raw video has no block info");
}

}  // namespace vide
