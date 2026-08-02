#pragma once
// DataSchema umbrella — schema-driven JSON/CSV serialization engine.
//   Declare a logical schema once (Type + fields tuple), then use
//   ds::json::Schema<S> and ds::csv::Schema<S> for automatic
//   serialize/parse with collective validation.  Engine knows no
//   domain types — NSID/AID/... plug in via user Converters.
#include "ds/Error.h"   // IWYU pragma: export
#include "ds/Field.h"   // IWYU pragma: export
#include "ds/codec/Codecs.h"   // IWYU pragma: export
#include "ds/codec/Converter.h"   // IWYU pragma: export
#include "ds/json/JsonBinder.h"   // IWYU pragma: export
#include "ds/csv/CsvBinder.h"   // IWYU pragma: export
