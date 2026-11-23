#include <buffered_minhash/minhash.hpp>
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

template<bool Vanilla = false, class S>
void bind_sketch(py::class_<S>& cls) {
    using Element = typename S::element_type;
    using Family = PythonHashFamily<Element>;
    constexpr bool legal = S::stream_kind == bmh::StreamKind::Legal;
    if constexpr (Vanilla) {
        cls.def(py::init([](std::size_t k, std::optional<std::uint64_t> seed) {
            return seed ? S(k, *seed) : S(k);
        }), py::arg("k"), py::arg("seed") = py::none(),
        "Create an empty vanilla sketch with k > 0 hashes. An optional seed makes construction reproducible.")
        .def(py::init([](const Family& hashes) { return S(hashes.value); }), py::arg("hashes"),
        "Create an empty vanilla sketch sharing hashes; no buffer capacity is needed.");
    } else {
        cls.def(py::init([](std::size_t k, std::size_t capacity, std::optional<std::uint64_t> seed) {
            return seed ? S(k, capacity, *seed) : S(k, capacity);
        }), py::arg("k"), py::arg("capacity"), py::arg("seed") = py::none(),
        "Create an empty sketch with k hashes and capacity entries per buffer. Both must be positive.")
       .def(py::init([](std::size_t capacity, const Family& hashes) {
            return S(capacity, hashes.value);
        }), py::arg("capacity"), py::arg("hashes"),
        "Create an empty sketch sharing hashes; k is derived from that configuration.")
        .def("buffer_capacity", &S::buffer_capacity, "Return the maximum entries per buffer (ell).");
    }
    cls.def("insert", [](S& self, py::handle element) {
            return self.insert(checked_element<Element>(element));
        }, py::arg("element"),
        legal ? "Insert an element that was absent before updating the external set. Unchecked precondition; returns UpdateResult.Ok."
              : "Insert after updating the external set. Duplicates are harmless. Returns UpdateResult.Ok.")
       .def("erase", [](S& self, py::handle element) {
            return self.erase(checked_element<Element>(element));
        }, py::arg("element"),
        Vanilla ? "Delete after updating the external set; absent elements are harmless. Invalidate all coordinates won by this element, returning NeedsRecovery if any are lost. Reads and updates then require restore."
        : legal ? "Delete an element that was present before updating the external set (unchecked). Returns NeedsRecovery if a buffer empties; then call restore."
              : "Delete after updating the external set; absent elements are harmless. Returns NeedsRecovery if a buffer empties; then call restore.")
       .def("restore", [](S& self, py::iterable elements) {
            self.restore(ElementIterator<Element>{py::iter(elements)}, std::default_sentinel);
        }, py::arg("elements"),
        Vanilla ? "After a fault, scan the unchanged current set after the triggering deletion and recompute only lost coordinates, preserving valid entries. When Ready, rebuild all coordinates to initialize or replace the set. Accepts generators, duplicates and empty iterables; retains hashes."
        : legal ? "Rebuild from the current set in one pass. Requires distinct elements (unchecked); accepts generators and empty iterables; retains hashes."
              : "Rebuild from the current set in one pass. Accepts generators, duplicates and empty iterables; retains hashes.")
       .def("signature", &S::copy_signature,
        "Return an independent list of hash values, or None for a known empty set. Raises RuntimeError on fault.")
       .def("copy_signature", &S::copy_signature,
        "Return an independent signature list, or None. Equivalent to signature() in Python.")
       .def("state", &S::state, "Return SketchState.Ready or SketchState.NeedsRecovery.")
       .def("signature_length", &S::signature_length, "Return the number of hash functions k.")
       .def("hash_family", [](const S& self) { return Family{self.hash_family()}; },
        "Return a shared immutable hash configuration; no hash tables are copied.")
       .def("copy", [](const S& self) { return S(self); },
        "Copy mutable state independently, sharing only the immutable hashes.")
       .def("__copy__", [](const S& self) { return S(self); });
}

template<class Left, class Right>
void bind_comparison(py::module_& module, py::class_<Left>& cls) {
    cls.def("compatible_with", [](const Left& self, const Right& other) {
        return self.compatible_with(other);
    }, py::arg("other"), "Check ordered hash configuration equality, independently of readiness or buffer capacity.");
    module.def("similarity", [](const Left& left, const Right& right) {
        return bmh::similarity(left, right);
    }, py::arg("left"), py::arg("right"),
    "Compare compatible ready signatures. Two empty sets give 1; one empty gives 0. Incompatibility raises ValueError.");
}

template<class Left, class... Right>
void bind_comparisons(py::module_& module, py::class_<Left>& cls, const py::class_<Right>&...) {
    (bind_comparison<Left, Right>(module, cls), ...);
}

template<class Element>
void bind_width(py::module_& module, const char* family_name, const char* array_name, const char* tree_name,
                const char* legal_array_name, const char* legal_tree_name, const char* minhash_name) {
    bind_family<Element>(module, family_name);
    py::class_<Sketch<Element, bmh::BufferKind::Array>> array(module, array_name,
        "General-stream buffered MinHash with array buffers. The application owns the set.");
    py::class_<Sketch<Element, bmh::BufferKind::Tree>> tree(module, tree_name,
        "General-stream buffered MinHash with tree buffers. The application owns the set.");
    py::class_<bmh::LegalBufferedMinhash<Element>> legal_array(module, legal_array_name,
        "Legal-stream buffered MinHash storing only hashes in array buffers. Insert only absent elements, erase only present elements, and restore distinct elements. Preconditions are unchecked; violations can silently corrupt signatures.");
    py::class_<bmh::LegalBufferedMinhash<Element, bmh::BufferKind::Tree>> legal_tree(module, legal_tree_name,
        "Legal-stream buffered MinHash storing hash multiplicities in tree buffers. Insert only absent elements, erase only present elements, and restore distinct elements. Preconditions are unchecked; violations can silently corrupt signatures.");
    py::class_<bmh::Minhash<Element>> vanilla(module, minhash_name,
        "Vanilla MinHash without buffers. Supports redundant set updates and shared hashes. On deletion, all lost minima require recovery; restore recomputes only those coordinates and preserves valid ones. The application owns the set.");
    bind_sketch(array);
    bind_sketch(tree);
    bind_sketch(legal_array);
    bind_sketch(legal_tree);
    bind_sketch<true>(vanilla);
    bind_comparisons(module, array, array, tree, legal_array, legal_tree, vanilla);
    bind_comparisons(module, tree, array, tree, legal_array, legal_tree, vanilla);
    bind_comparisons(module, legal_array, array, tree, legal_array, legal_tree, vanilla);
    bind_comparisons(module, legal_tree, array, tree, legal_array, legal_tree, vanilla);
    bind_comparisons(module, vanilla, array, tree, legal_array, legal_tree, vanilla);
}

} // namespace

PYBIND11_MODULE(buffered_minhash, module) {
    module.doc() = "Buffered and vanilla MinHash for dynamic sets with explicit recovery and shared immutable hashes.";
    module.attr("__version__") = "0.1.0";
    py::enum_<bmh::UpdateResult>(module, "UpdateResult", "Validity after an update, not a membership result.")
        .value("Ok", bmh::UpdateResult::Ok)
        .value("NeedsRecovery", bmh::UpdateResult::NeedsRecovery);
    py::enum_<bmh::SketchState>(module, "SketchState", "Whether updates and signature access are allowed.")
        .value("Ready", bmh::SketchState::Ready)
        .value("NeedsRecovery", bmh::SketchState::NeedsRecovery);
    bind_width<std::uint32_t>(module, "HashFamily", "BufferedMinhash", "TreeBufferedMinhash",
                              "LegalBufferedMinhash", "LegalTreeBufferedMinhash", "Minhash");
    bind_width<std::uint64_t>(module, "HashFamily64", "BufferedMinhash64", "TreeBufferedMinhash64",
                              "LegalBufferedMinhash64", "LegalTreeBufferedMinhash64", "Minhash64");
}
