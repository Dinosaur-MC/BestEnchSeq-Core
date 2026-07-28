#include "IAlgorithmSerializer.h"
#include "domain/algorithm/IAlgorithm.h"
#include "domain/algorithm/types/AlgorithmTypes.h"

namespace algorithm {

std::vector<uint8_t> IAlgorithmSerializer::serialize(
    const IAlgorithm& algo, const AlgorithmInput& input) const
{
    checkpoint::Checkpoint cp(std::string(algo.name()), checkpoint::FILE_VERSION);

    // Write input as a section
    cp.add_section(checkpoint::SECTION_TYPE_INPUT, 0, input);

    // Write algorithm-specific state
    auto state_sections = _serialize_state(algo);
    for (auto& sec : state_sections) {
        sec.header.type |= checkpoint::SECTION_TYPE_ALGO;
        cp.sections.push_back(std::move(sec));
        cp.meta.num_sections = static_cast<uint32_t>(cp.sections.size());
    }

    cp.finalize();

    ByteStreamWriter w;
    w << cp;
    return std::move(w).take();
}

bool IAlgorithmSerializer::deserialize(
    IAlgorithm& algo, AlgorithmInput& out_input,
    std::span<const uint8_t> data) const
{
    // Quick header peek for magic + version check — reads only the first 6 bytes.
    constexpr size_t MIN_CHECKPOINT_BYTES = 37; // fixed header (29) + empty tag overhead (8)
    if (data.size() < MIN_CHECKPOINT_BYTES)
        return false;
    if (data.size() < sizeof(checkpoint::MetaHeader::magic) +
                      sizeof(checkpoint::MetaHeader::version))
        return false;

    {
        ByteStreamReader peek_r(data.data(), data.size());
        uint32_t peek_magic = peek_r.u32();
        uint16_t peek_ver   = peek_r.u16();
        if (!peek_r.ok() || peek_magic != checkpoint::FILE_MAGIC ||
            peek_ver != checkpoint::FILE_VERSION)
            return false;
    }

    // Full parse
    checkpoint::Checkpoint cp;
    ByteStreamReader r(data.data(), data.size());
    r >> cp;
    if (!r.ok()) return false;
    if (!cp.verify()) return false;

    // Extract input section and collect algorithm-specific sections
    std::vector<checkpoint::Section> algo_sections;
    for (auto& sec : cp.sections) {
        if (sec.header.type == checkpoint::SECTION_TYPE_INPUT) {
            ByteStreamReader pr(sec.payload.data(), sec.payload.size());
            pr >> out_input;
            if (!pr.ok()) return false;
        } else if (sec.header.type & checkpoint::SECTION_TYPE_ALGO) {
            algo_sections.push_back(std::move(sec));
        }
    }

    return _deserialize_state(algo, algo_sections);
}

} // namespace algorithm
