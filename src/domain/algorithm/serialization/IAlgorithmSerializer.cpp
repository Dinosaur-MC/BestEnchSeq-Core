#include "IAlgorithmSerializer.h"
#include "domain/algorithm/types/AlgorithmTypes.h"

namespace algorithm {

std::vector<uint8_t> IAlgorithmSerializer::serialize(
    const IAlgorithm& algo, const AlgorithmInput& input) const
{
    checkpoint::Checkpoint cp(algorithm_name(), /* version */ 1);

    // Write input as a section
    cp.add_section(checkpoint::SECTION_TYPE_INPUT, 0, input);

    // Write algorithm-specific state
    auto state_sections = _serialize_state(algo);
    for (auto& sec : state_sections) {
        sec.header.type |= checkpoint::SECTION_TYPE_ALGO;
        cp.sections.push_back(std::move(sec));
    }

    ByteStreamWriter w;
    w << cp;
    return std::move(w).take();
}

bool IAlgorithmSerializer::deserialize(
    IAlgorithm& algo, AlgorithmInput& out_input,
    std::span<const uint8_t> data) const
{
    // Quick header peek for magic + version check
    if (data.size() < sizeof(checkpoint::MetaHeader::magic) +
                      sizeof(checkpoint::MetaHeader::version))
        return false;

    checkpoint::MetaHeader peek_hdr;
    {
        ByteStreamReader peek_r(data.data(), sizeof(peek_hdr));
        peek_hdr.deserialize(peek_r);
        if (peek_hdr.magic != checkpoint::FILE_MAGIC ||
            peek_hdr.version != checkpoint::FILE_VERSION)
            return false;
    }

    // Full parse
    checkpoint::Checkpoint cp;
    ByteStreamReader r(data.data(), data.size());
    r >> cp;
    if (!r.ok()) return false;

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
