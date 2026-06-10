#ifndef STEAMWORKS_BINDINGS_H
#define STEAMWORKS_BINDINGS_H
#ifdef _WIN32
#pragma once
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <algorithm>

//#include "../public/steam/steam_api.h"
#include "../public/steam/steam_api_flat.h"
//#include "../public/steam/steam_gameserver.h" // EServerMode enum
#include "../public/steam/steamnetworkingfakeip.h" // SteamNetworkingFakeIPResult
//#include "../public/steam/isteamnetworkingutils.h"
//#include "../public/steam/matchmakingtypes.h"
//#include "../public/steam/isteammatchmaking.h"


namespace py = pybind11;


// accessors & conversions

template <size_t N>
std::string get_char_array(const char (&buf)[N]) {
	return std::string(buf);
}
template <size_t N>
void set_char_array(char (&buf)[N], const std::string& s) {
	std::strncpy(buf, s.c_str(), N - 1);
	buf[N - 1] = '\0';
}

/*
template <typename T, size_t N>
std::vector<T> get_fixed_array(const T (&arr)[N]) {
	return std::vector<T>(arr, arr + N);
}
template <typename T, size_t N>
void set_fixed_array(T (&arr)[N], const std::vector<T>& values) {
	if (values.size() != N)
		throw py::value_error("Expected array of length " + std::to_string(N) + ", got " + std::to_string(values.size()) );
	std::copy(values.begin(), values.end(), arr);
}
*/

template <typename T, size_t N>
struct FixedArrayView {
	T (&data)[N];

	T get(int32 i) const {
		if (i < 0) i += N; 
		if ( (size_t)i >= N) throw py::index_error();
		return data[i];
	}

	void set(int32 i, const T& value) {
		if (i < 0) i += N;
		if ( (size_t)i >= N) throw py::index_error();
		data[i] = value;
	}

	size_t size() const {
		return N;
	}

	std::vector<T> to_list() const {
		return std::vector<T>(data, data + N);
	}
};

template <typename T, size_t N>
void bind_fixed_array_view(py::module_& m, const char* name) {
	py::class_<FixedArrayView<T, N>>(m, name)
		.def("__len__", &FixedArrayView<T, N>::size)
		.def("__getitem__", &FixedArrayView<T, N>::get)
		.def("__setitem__", &FixedArrayView<T, N>::set)
		.def("__iter__", [](FixedArrayView<T, N>& self) {
			return py::make_iterator(self.data, self.data + N);
		}, py::keep_alive<0, 1>())
		.def("to_list", &FixedArrayView<T, N>::to_list);
}

namespace pybind11 {
namespace detail {
	template <>
	struct type_caster<SteamParamStringArray_t> {
	public:
		PYBIND11_TYPE_CASTER(SteamParamStringArray_t, _("Sequence[str]"));

		std::vector<std::string> storage;
		std::vector<const char*> ptrs;

		bool load(handle src, bool) {
			if (!py::isinstance<py::sequence>(src)) {
				return false;
			}

			py::sequence seq = py::reinterpret_borrow<py::sequence>(src);

			storage.clear();
			storage.reserve(py::len(seq));

			for (py::handle item : seq) {
				storage.emplace_back(py::cast<std::string>(item));
			}

			ptrs.clear();
			ptrs.reserve(storage.size());

			for (const std::string& s : storage) {
				ptrs.push_back(s.c_str());
			}

			value.m_ppStrings = ptrs.empty() ? nullptr : ptrs.data();
			value.m_nNumStrings = static_cast<int32>(ptrs.size());

			return true;
		}

		static handle cast(
			const SteamParamStringArray_t& src,
			return_value_policy,
			handle
		) {
			py::list out;

			if (!src.m_ppStrings || src.m_nNumStrings <= 0) {
				return out.release();
			}

			for (int32 i = 0; i < src.m_nNumStrings; ++i) {
				out.append(src.m_ppStrings[i] ? src.m_ppStrings[i] : "");
			}

			return out.release();
		}
	};
}}


// for SteamNetworkingMessage_t
template <typename T>
struct ReleaseDeleter {
	void operator()(T* ptr) const noexcept {
		if (ptr) {
			ptr->Release();
		}
	}
};

template <typename T>
using ReleasePtr = std::unique_ptr<T, ReleaseDeleter<T>>;

#endif