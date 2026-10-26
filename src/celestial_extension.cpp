#define DUCKDB_EXTENSION_MAIN

// Windows...
// Switch to std::numbers::pi rather than M_PI in C++20.
#define _USE_MATH_DEFINES

#include <cmath>
#include <algorithm>

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

#include "celestial_extension.hpp"

namespace duckdb {
static constexpr double kDeg2Rad = M_PI / 180.0;
static constexpr double kRad2Deg = 180.0 / M_PI;

// https://www.movable-type.co.uk/scripts/latlong.html
static inline double haversine(double ra1, double dec1, double ra2, double dec2) {
	double d_ra = ra2 - ra1;
	double d_dec = dec2 - dec1;

	double sin_d_dec = std::sin(d_dec / 2.0);
	double sin_d_ra = std::sin(d_ra / 2.0);

	double a = sin_d_dec * sin_d_dec + std::cos(dec1) * std::cos(dec2) * sin_d_ra * sin_d_ra;

	// Clamp `a` to [0, 1] to prevent NaN from minor floating-point inaccuracies
	if (std::isnan(a)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	a = std::clamp(a, 0.0, 1.0);

	return 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

template <bool Degrees = false>
inline void SphericalAngleFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	UnifiedVectorFormat ra1_data, dec1_data, ra2_data, dec2_data;
	args.data[0].ToUnifiedFormat(ra1_data);
	args.data[1].ToUnifiedFormat(dec1_data);
	args.data[2].ToUnifiedFormat(ra2_data);
	args.data[3].ToUnifiedFormat(dec2_data);

	const auto ra1_ptr = reinterpret_cast<const double *>(ra1_data.data);
	const auto dec1_ptr = reinterpret_cast<const double *>(dec1_data.data);
	const auto ra2_ptr = reinterpret_cast<const double *>(ra2_data.data);
	const auto dec2_ptr = reinterpret_cast<const double *>(dec2_data.data);

	// TODO next DuckDB version: use FlatVector::Writer API
	// See https://github.com/duckdb/duckdb/pull/21808
	auto result_data = FlatVector::Writer<double>(result, args.size());
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); ++i) {
		auto ra1_idx = ra1_data.sel->get_index(i);
		auto dec1_idx = dec1_data.sel->get_index(i);
		auto ra2_idx = ra2_data.sel->get_index(i);
		auto dec2_idx = dec2_data.sel->get_index(i);

		// Handle NULLs
		if (!ra1_data.validity.RowIsValid(ra1_idx) || !dec1_data.validity.RowIsValid(dec1_idx) ||
		    !ra2_data.validity.RowIsValid(ra2_idx) || !dec2_data.validity.RowIsValid(dec2_idx)) {
			result_data.WriteNull();
			continue;
		}

		double ra1 = ra1_ptr[ra1_idx];
		double dec1 = dec1_ptr[dec1_idx];
		double ra2 = ra2_ptr[ra2_idx];
		double dec2 = dec2_ptr[dec2_idx];

		if constexpr (Degrees) {
			ra1 *= kDeg2Rad;
			dec1 *= kDeg2Rad;
			ra2 *= kDeg2Rad;
			dec2 *= kDeg2Rad;
		}

		double val = haversine(ra1, dec1, ra2, dec2);
		result_data.WriteValue(Degrees ? val * kRad2Deg : val);
	}
}

static void LoadInternal(ExtensionLoader &loader) {
	FunctionDescription desc;

	auto fn =
	    ScalarFunction("angular_separation_rad",
	                   {FunctionParameter("ra1", LogicalType::DOUBLE), FunctionParameter("dec1", LogicalType::DOUBLE),
	                    FunctionParameter("ra2", LogicalType::DOUBLE), FunctionParameter("dec2", LogicalType::DOUBLE)},
	                   LogicalType::DOUBLE, SphericalAngleFunction<false>);
	auto info = CreateScalarFunctionInfo(fn);
	desc = {.parameter_names = {"ra1", "dec1", "ra2", "dec2"},
	        .description = "Returns the angular separation in radians between two points on a sphere given in radians.",
	        .examples = {"angular_separation_rad(0, 0, 1, 0)"},
	        .categories = {"astronomy", "geometry"}};
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	info.descriptions.push_back(desc);
	loader.RegisterFunction(std::move(info));

	// Deprecated alias for angular_separation_rad.
	fn.name = "spherical_angle";
	info = CreateScalarFunctionInfo(fn);
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	info.descriptions.push_back(desc);
	loader.RegisterFunction(std::move(info));

	fn = ScalarFunction("angular_separation_deg",
	                    {FunctionParameter("ra1", LogicalType::DOUBLE), FunctionParameter("dec1", LogicalType::DOUBLE),
	                     FunctionParameter("ra2", LogicalType::DOUBLE), FunctionParameter("dec2", LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, SphericalAngleFunction<true>);
	info = CreateScalarFunctionInfo(fn);
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	desc = {.parameter_names = {"ra1", "dec1", "ra2", "dec2"},
	        .description = "Returns the angular separation in degrees between two points on a sphere given in degrees.",
	        .examples = {"angular_separation_deg(0, 0, 0, 30)"},
	        .categories = {"astronomy", "geometry"}};
	info.descriptions.push_back(desc);
	loader.RegisterFunction(std::move(info));
}

void CelestialExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string CelestialExtension::Name() {
	return "celestial";
}

std::string CelestialExtension::Version() const {
#ifdef EXT_VERSION_CELESTIAL
	return EXT_VERSION_CELESTIAL;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(celestial, loader) {
	duckdb::LoadInternal(loader);
}
}
