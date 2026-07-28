#include "Checkpoint.h"
#include <chrono>
#include <cstring>

namespace checkpoint {

MetaHeader::MetaHeader(std::string_view tag, uint16_t algo_ver, uint32_t sect_count)
    : num_sections(sect_count), algo_version(algo_ver), algorithm_tag(tag)
{
    timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void MetaHeader::serialize(ByteStreamWriter& w) const noexcept {
    w << magic << version << flags << num_sections << timestamp;
    w.bytes(crc_code, sizeof(crc_code));
    w << algo_version << algorithm_tag;
}

void MetaHeader::deserialize(ByteStreamReader& r) noexcept {
    r >> magic >> version >> flags >> num_sections >> timestamp;
    if (r.ok()) {
        auto data = r.read_bytes(sizeof(crc_code));
        std::memcpy(crc_code, data.data(), sizeof(crc_code));
    }
    r >> algo_version >> algorithm_tag;
}

SectionHeader::SectionHeader(uint32_t t, uint32_t id, uint64_t len)
    : type(t), section_id(id), payload_len(len) {}

void SectionHeader::serialize(ByteStreamWriter& w) const noexcept {
    w << type << section_id << payload_len;
}

void SectionHeader::deserialize(ByteStreamReader& r) noexcept {
    r >> type >> section_id >> payload_len;
}

Section::Section(uint32_t type, uint32_t id, const IBinarySerializable& body)
    : header(type, id, 0)
{
    ByteStreamWriter w;
    w << body;
    payload = std::move(w).take();
    header.payload_len = payload.size();
}

void Section::serialize(ByteStreamWriter& w) const noexcept {
    w << header;
    w.bytes(payload.data(), payload.size());
}

void Section::deserialize(ByteStreamReader& r) noexcept {
    r >> header;
    payload = r.read_bytes(static_cast<size_t>(header.payload_len));
}

Checkpoint::Checkpoint(std::string_view tag, uint16_t algo_ver)
    : meta(tag, algo_ver, 0) {}

void Checkpoint::add_section(uint32_t type, uint32_t id, const IBinarySerializable& body) {
    sections.emplace_back(type, id, body);
    meta.num_sections = static_cast<uint32_t>(sections.size());
}

void Checkpoint::serialize(ByteStreamWriter& w) const noexcept {
    // num_sections is maintained by add_section() during construction
    w << meta;
    for (const auto& sec : sections)
        w << sec;
}

void Checkpoint::deserialize(ByteStreamReader& r) noexcept {
    r >> meta;
    if (!r.ok()) return;
    if (meta.num_sections > MAX_CHECKPOINT_SECTIONS) {
        r.set_fail();
        return;
    }
    sections.resize(meta.num_sections);
    for (auto& sec : sections)
        r >> sec;
}

void Checkpoint::finalize() noexcept {
    ByteStreamWriter w;
    w << meta.algorithm_tag << meta.algo_version;
    for (const auto& sec : sections)
        w.bytes(sec.payload.data(), sec.payload.size());
    auto buf = std::move(w).take();
    compute_crc56(buf.data(), buf.size(), meta.crc_code);
}

bool Checkpoint::verify() const noexcept {
    // Legacy checkpoints have all-zero crc_code — skip verification.
    bool all_zero = true;
    for (int i = 0; i < 7; ++i) {
        if (meta.crc_code[i] != 0) { all_zero = false; break; }
    }
    if (all_zero)
        return true;

    uint8_t expected[7] = {};
    ByteStreamWriter w;
    w << meta.algorithm_tag << meta.algo_version;
    for (const auto& sec : sections)
        w.bytes(sec.payload.data(), sec.payload.size());
    auto buf = std::move(w).take();
    compute_crc56(buf.data(), buf.size(), expected);
    return std::memcmp(expected, meta.crc_code, 7) == 0;
}

void compute_crc56(const uint8_t* data, size_t len, uint8_t crc[7]) {
    std::memset(crc, 0, 7);
    for (size_t i = 0; i < len; ++i) {
        size_t lane = i % 7;
        crc[lane]       = static_cast<uint8_t>(crc[lane] ^ data[i]);
        crc[(lane+1)%7] = static_cast<uint8_t>(crc[(lane+1)%7] + data[i]);
    }
}

} // namespace checkpoint
