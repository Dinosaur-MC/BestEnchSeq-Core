#include "idastar/IDAStarDiagnostics.h"

void IDAStarDiagnostics::flush(std::vector<DiagnosticsWriter::Entry>& out) const {
    PoolSearchDiagnostics::flush(out);
    out.push_back({"tt_lookups",        static_cast<int64_t>(tt_lookups)});
    out.push_back({"tt_stores",         static_cast<int64_t>(tt_stores)});
    out.push_back({"solution_path_len", static_cast<int64_t>(solution_path_len)});
}
