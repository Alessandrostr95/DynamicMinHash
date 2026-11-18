#include <buffered_minhash/buffered_minhash.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace py = pybind11;
namespace bmh = buffered_minhash;

namespace {

// Own a handle to the immutable native configuration, not a mutable family.
template<class Element>
struct PythonHashFamily {
    using Family = bmh::HashFamily<bmh::TabulationHash<Element>>;
    std::shared_ptr<const Family> value;
};

template<class Element>
Element checked_element(py::handle value) {
    if (PyBool_Check(value.ptr()) || !PyIndex_Check(value.ptr())) {
        throw py::type_error("elements must be integers (not bool, float, or str)");
    }
    auto integer = py::reinterpret_steal<py::object>(PyNumber_Index(value.ptr()));
    if (!integer) {
        throw py::error_already_set();
    }
    const auto result = PyLong_AsUnsignedLongLong(integer.ptr());
    if (PyErr_Occurred()) {
        throw py::error_already_set();
    }
    if (result > std::numeric_limits<Element>::max()) {
        PyErr_SetString(PyExc_OverflowError, "element exceeds the sketch's unsigned integer domain");
        throw py::error_already_set();
    }
    return static_cast<Element>(result);
}

// Adapt a Python iterable to the native one-pass restore API. The GIL stays held
// because advancing or converting an element may execute Python code.
template<class Element>
class ElementIterator {
public:
    using value_type = Element;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::input_iterator_tag;

    explicit ElementIterator(py::iterator iterator) : iterator_(std::move(iterator)) {}
    Element operator*() const { return checked_element<Element>(*iterator_); }
    ElementIterator& operator++() { ++iterator_; return *this; }
    void operator++(int) { ++*this; }
    bool operator==(std::default_sentinel_t) const {
        return iterator_ == py::iterator::sentinel();
    }

private:
    py::iterator iterator_;
};

template<class Element>
void bind_family(py::module_& module, const char* name) {
    using Family = PythonHashFamily<Element>;
    py::class_<Family>(module, name,
        "Immutable shared tabulation hash configuration. Use the same family for comparable sketches.")
        .def(py::init([](std::size_t k, std::optional<std::uint64_t> seed) {
            return Family{seed ? bmh::make_hash_family<Element>(k, *seed)
                               : bmh::make_hash_family<Element>(k)};
        }), py::arg("k"), py::arg("seed") = py::none(),
        "Create k hash functions (k > 0). An integer seed makes construction reproducible.")
        .def("__len__", [](const Family& self) { return self.value->size(); },
             "Return the signature length k.")
        .def("__eq__", [](const Family& self, const Family& other) {
            return self.value == other.value || *self.value == *other.value;
        }, py::is_operator(), "Compare all hash parameters in coordinate order.");
}

template<class Element, bmh::BufferKind Kind>
using Sketch = bmh::BufferedMinhash<Element, Kind>;

template<class Element, bmh::BufferKind Kind>
void bind_sketch(py::class_<Sketch<Element, Kind>>& cls) {
    using S = Sketch<Element, Kind>;
    using Family = PythonHashFamily<Element>;
    cls.def(py::init([](std::size_t k, std::size_t capacity, std::optional<std::uint64_t> seed) {
            return seed ? S(k, capacity, *seed) : S(k, capacity);
        }), py::arg("k"), py::arg("capacity"), py::arg("seed") = py::none(),
        "Create an empty sketch with k hashes and capacity pairs per buffer. Both must be positive.")
       .def(py::init([](std::size_t capacity, const Family& hashes) {
            return S(capacity, hashes.value);
        }), py::arg("capacity"), py::arg("hashes"),
        "Create an empty sketch sharing hashes; k is derived from that configuration.")
       .def("insert", [](S& self, py::handle element) {
            return self.insert(checked_element<Element>(element));
        }, py::arg("element"),
        "Insert after updating the external set. Duplicates are harmless. Returns UpdateResult.Ok.")
       .def("erase", [](S& self, py::handle element) {
            return self.erase(checked_element<Element>(element));
        }, py::arg("element"),
        "Delete after updating the external set. Returns NeedsRecovery if a buffer empties; then call restore.")
       .def("restore", [](S& self, py::iterable elements) {
            self.restore(ElementIterator<Element>{py::iter(elements)}, std::default_sentinel);
        }, py::arg("elements"),
        "Rebuild from the current set in one pass. Accepts generators, duplicates and empty iterables; retains hashes.")
       .def("signature", &S::copy_signature,
        "Return an independent list of hash values, or None for a known empty set. Raises RuntimeError on fault.")
       .def("copy_signature", &S::copy_signature,
        "Return an independent signature list, or None. Equivalent to signature() in Python.")
       .def("state", &S::state, "Return SketchState.Ready or SketchState.NeedsRecovery.")
       .def("signature_length", &S::signature_length, "Return the number of hash functions k.")
       .def("buffer_capacity", &S::buffer_capacity, "Return the maximum pairs per buffer (ell).")
       .def("hash_family", [](const S& self) { return Family{self.hash_family()}; },
        "Return a shared immutable hash configuration; no hash tables are copied.")
       .def("copy", [](const S& self) { return S(self); },
        "Copy buffers and state independently, sharing only the immutable hashes.")
       .def("__copy__", [](const S& self) { return S(self); });
}

template<class Element, bmh::BufferKind Left, bmh::BufferKind Right>
void bind_comparison(py::module_& module, py::class_<Sketch<Element, Left>>& cls) {
    cls.def("compatible_with", [](const Sketch<Element, Left>& self, const Sketch<Element, Right>& other) {
        return self.compatible_with(other);
    }, py::arg("other"), "Check ordered hash configuration equality, independently of readiness or buffer capacity.");
    module.def("similarity", [](const Sketch<Element, Left>& left, const Sketch<Element, Right>& right) {
        return bmh::similarity(left, right);
    }, py::arg("left"), py::arg("right"),
    "Compare compatible ready signatures. Two empty sets give 1; one empty gives 0. Incompatibility raises ValueError.");
}

template<class Element>
void bind_width(py::module_& module, const char* family_name, const char* array_name, const char* tree_name) {
    bind_family<Element>(module, family_name);
    py::class_<Sketch<Element, bmh::BufferKind::Array>> array(module, array_name,
        "General-stream buffered MinHash with array buffers. The application owns the set.");
    py::class_<Sketch<Element, bmh::BufferKind::Tree>> tree(module, tree_name,
        "General-stream buffered MinHash with tree buffers. The application owns the set.");
    bind_sketch(array);
    bind_sketch(tree);
    bind_comparison<Element, bmh::BufferKind::Array, bmh::BufferKind::Array>(module, array);
    bind_comparison<Element, bmh::BufferKind::Array, bmh::BufferKind::Tree>(module, array);
    bind_comparison<Element, bmh::BufferKind::Tree, bmh::BufferKind::Array>(module, tree);
    bind_comparison<Element, bmh::BufferKind::Tree, bmh::BufferKind::Tree>(module, tree);
}

} // namespace

PYBIND11_MODULE(buffered_minhash, module) {
    module.doc() = "Buffered MinHash for dynamic sets with explicit recovery and shared immutable hashes.";
    module.attr("__version__") = "0.1.0";
    py::enum_<bmh::UpdateResult>(module, "UpdateResult", "Validity after an update, not a membership result.")
        .value("Ok", bmh::UpdateResult::Ok)
        .value("NeedsRecovery", bmh::UpdateResult::NeedsRecovery);
    py::enum_<bmh::SketchState>(module, "SketchState", "Whether updates and signature access are allowed.")
        .value("Ready", bmh::SketchState::Ready)
        .value("NeedsRecovery", bmh::SketchState::NeedsRecovery);
    bind_width<std::uint32_t>(module, "HashFamily", "BufferedMinhash", "TreeBufferedMinhash");
    bind_width<std::uint64_t>(module, "HashFamily64", "BufferedMinhash64", "TreeBufferedMinhash64");
}
