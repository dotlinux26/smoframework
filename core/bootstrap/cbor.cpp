#include "cbor.hpp"

#include <cstring>

namespace smo::cbor {

// ── Internal helpers ──────────────────────────────────────────────────

// Encode a CBOR head byte (major type + additional info)
void Encoder::encode_head(uint8_t major, uint64_t val) {
    if (val <= 23) {
        buf_.push_back(static_cast<uint8_t>((major << 5) | val));
    } else if (val <= 0xFF) {
        buf_.push_back(static_cast<uint8_t>((major << 5) | 24));
        buf_.push_back(static_cast<uint8_t>(val));
    } else if (val <= 0xFFFF) {
        buf_.push_back(static_cast<uint8_t>((major << 5) | 25));
        buf_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(val & 0xFF));
    } else if (val <= 0xFFFFFFFFULL) {
        buf_.push_back(static_cast<uint8_t>((major << 5) | 26));
        buf_.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(val & 0xFF));
    } else {
        buf_.push_back(static_cast<uint8_t>((major << 5) | 27));
        buf_.push_back(static_cast<uint8_t>((val >> 56) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 48) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 40) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 32) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(val & 0xFF));
    }
}

// ── Public Encoder ────────────────────────────────────────────────────

void Encoder::encode_uint(uint64_t val) {
    encode_head(0, val);
}

void Encoder::encode_int(int64_t val) {
    if (val >= 0) {
        encode_uint(static_cast<uint64_t>(val));
    } else {
        encode_head(1, static_cast<uint64_t>(-1 - val));
    }
}

void Encoder::encode_bytes(BytesView data) {
    encode_head(2, data.size());
    buf_.insert(buf_.end(), data.begin(), data.end());
}

void Encoder::encode_string(const std::string& s) {
    encode_head(3, s.size());
    buf_.insert(buf_.end(), s.begin(), s.end());
}

void Encoder::encode_array(size_t count) {
    encode_head(4, count);
}

void Encoder::encode_map(size_t count) {
    encode_head(5, count);
}

// ── Key-value helpers ─────────────────────────────────────────────────

void encode_uint_key(Encoder& enc, uint64_t key) {
    enc.encode_uint(key);
}

void encode_string_key(Encoder& enc, const std::string& key) {
    enc.encode_string(key);
}

// ── Decoder ───────────────────────────────────────────────────────────

Result<uint64_t> Decoder::decode_head(uint8_t& major_out) {
    if (pos_ >= data_.size()) {
        return SMO_ERR_PROTOCOL(700, Error, NoRetry, None,
                                "CBOR: unexpected end of data");
    }
    uint8_t ib = data_[pos_++];
    major_out = ib >> 5;
    uint8_t ai = ib & 0x1F;

    if (ai <= 23) {
        return static_cast<uint64_t>(ai);
    }

    uint64_t val = 0;
    size_t extra = 0;
    switch (ai) {
        case 24: extra = 1; break;
        case 25: extra = 2; break;
        case 26: extra = 4; break;
        case 27: extra = 8; break;
        default:
            return SMO_ERR_PROTOCOL(701, Error, NoRetry, None,
                                    "CBOR: reserved additional info");
    }

    if (pos_ + extra > data_.size()) {
        return SMO_ERR_PROTOCOL(700, Error, NoRetry, None,
                                "CBOR: truncated additional info");
    }

    for (size_t i = 0; i < extra; ++i) {
        val = (val << 8) | data_[pos_++];
    }
    return val;
}

uint8_t Decoder::peek_major() const {
    if (pos_ >= data_.size()) return 0xFF;
    return data_[pos_] >> 5;
}

Result<uint64_t> Decoder::decode_uint() {
    uint8_t major = 0;
    auto val = decode_head(major);
    if (!val) return val.error();
    if (major != 0) {
        return SMO_ERR_PROTOCOL(702, Error, NoRetry, None,
                                "CBOR: expected unsigned integer");
    }
    return val;
}

Result<int64_t> Decoder::decode_int() {
    uint8_t major = 0;
    auto val = decode_head(major);
    if (!val) return val.error();
    if (major == 0) {
        return static_cast<int64_t>(val.value());
    } else if (major == 1) {
        return -1 - static_cast<int64_t>(val.value());
    }
    return SMO_ERR_PROTOCOL(703, Error, NoRetry, None,
                            "CBOR: expected integer");
}

Result<BytesView> Decoder::decode_bytes() {
    uint8_t major = 0;
    auto len = decode_head(major);
    if (!len) return len.error();
    if (major != 2) {
        return SMO_ERR_PROTOCOL(704, Error, NoRetry, None,
                                "CBOR: expected byte string");
    }
    size_t n = static_cast<size_t>(len.value());
    if (pos_ + n > data_.size()) {
        return SMO_ERR_PROTOCOL(700, Error, NoRetry, None,
                                "CBOR: truncated byte string");
    }
    BytesView result(data_.data() + pos_, n);
    pos_ += n;
    return result;
}

Result<std::string> Decoder::decode_string() {
    uint8_t major = 0;
    auto len = decode_head(major);
    if (!len) return len.error();
    if (major != 3) {
        return SMO_ERR_PROTOCOL(705, Error, NoRetry, None,
                                "CBOR: expected text string");
    }
    size_t n = static_cast<size_t>(len.value());
    if (pos_ + n > data_.size()) {
        return SMO_ERR_PROTOCOL(700, Error, NoRetry, None,
                                "CBOR: truncated text string");
    }
    std::string result(reinterpret_cast<const char*>(data_.data() + pos_), n);
    pos_ += n;
    return result;
}

Result<size_t> Decoder::decode_array_size() {
    uint8_t major = 0;
    auto len = decode_head(major);
    if (!len) return len.error();
    if (major != 4) {
        return SMO_ERR_PROTOCOL(706, Error, NoRetry, None,
                                "CBOR: expected array");
    }
    return static_cast<size_t>(len.value());
}

Result<size_t> Decoder::decode_map_size() {
    uint8_t major = 0;
    auto len = decode_head(major);
    if (!len) return len.error();
    if (major != 5) {
        return SMO_ERR_PROTOCOL(707, Error, NoRetry, None,
                                "CBOR: expected map");
    }
    return static_cast<size_t>(len.value());
}

Result<void> Decoder::skip() {
    if (pos_ >= data_.size()) {
        return SMO_ERR_PROTOCOL(700, Error, NoRetry, None,
                                "CBOR: unexpected end during skip");
    }
    uint8_t major = data_[pos_] >> 5;
    switch (major) {
        case 0: case 1: { // uint / negint
            uint8_t m;
            return decode_head(m), Result<void>{};
        }
        case 2: { // bytes
            auto r = decode_bytes();
            if (!r) return r.error();
            return {};
        }
        case 3: { // string
            auto r = decode_string();
            if (!r) return r.error();
            return {};
        }
        case 4: { // array
            auto size_r = decode_array_size();
            if (!size_r) return size_r.error();
            for (size_t i = 0; i < size_r.value(); ++i) {
                auto r = skip();
                if (!r) return r;
            }
            return {};
        }
        case 5: { // map
            auto size_r = decode_map_size();
            if (!size_r) return size_r.error();
            for (size_t i = 0; i < size_r.value() * 2; ++i) {
                auto r = skip();
                if (!r) return r;
            }
            return {};
        }
        case 7: { // simple/float
            uint8_t m;
            decode_head(m);
            return {};
        }
        default:
            return SMO_ERR_PROTOCOL(708, Error, NoRetry, None,
                                    "CBOR: cannot skip major type " + std::to_string((int)major));
    }
}

// ── Field decoders (find by uint key in a map) ────────────────────────

static Result<uint64_t> decode_uint_field_at_offset(Decoder& dec, uint64_t target_key) {
    while (!dec.done()) {
        uint8_t major = dec.peek_major();
        if (major != 0) break; // not a uint key
        auto key = dec.decode_uint();
        if (!key) return key.error();
        if (key.value() == target_key) {
            return dec.decode_uint();
        }
        auto r = dec.skip();
        if (!r) return r.error();
    }
    return SMO_ERR_PROTOCOL(709, Error, NoRetry, None,
                            "CBOR: field " + std::to_string(target_key) + " not found");
}

Result<uint64_t> decode_uint_field(Decoder& dec, uint64_t key) {
    return decode_uint_field_at_offset(dec, key);
}

Result<std::string> decode_string_field(Decoder& dec, uint64_t target_key) {
    while (!dec.done()) {
        uint8_t major = dec.peek_major();
        if (major != 0) break;
        auto key = dec.decode_uint();
        if (!key) return key.error();
        if (key.value() == target_key) {
            return dec.decode_string();
        }
        auto r = dec.skip();
        if (!r) return r.error();
    }
    return SMO_ERR_PROTOCOL(709, Error, NoRetry, None,
                            "CBOR: field not found");
}

Result<BytesView> decode_bytes_field(Decoder& dec, uint64_t target_key) {
    while (!dec.done()) {
        uint8_t major = dec.peek_major();
        if (major != 0) break;
        auto key = dec.decode_uint();
        if (!key) return key.error();
        if (key.value() == target_key) {
            return dec.decode_bytes();
        }
        auto r = dec.skip();
        if (!r) return r.error();
    }
    return SMO_ERR_PROTOCOL(709, Error, NoRetry, None,
                            "CBOR: field not found");
}

} // namespace smo::cbor
