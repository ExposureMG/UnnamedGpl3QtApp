#pragma once

#include "core/Status.hpp"

#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace unnamed::core {

struct FileProbe;

// Single-file operations (XEX decrypt, checksum, ...) describe their
// parameters as data; the UI builds the dialog from the descriptor and the
// core checks the values before the operation runs.

enum class ParameterKind {
    Choice,       // one of `options`; the value is the option's id (string)
    Boolean,      // bool
    Integer,      // std::int64_t within [minimum, maximum]
    Text,         // string
    InputFile,    // string: an existing file to read (see OperationIo::openInput)
    OutputFile,   // string: a file to create (see OperationIo::createOutput)
    OutputFolder, // string: a folder to create files in
};

// A parameter value. Choice, Text and the file kinds hold a string, Boolean a
// bool and Integer an std::int64_t. Spell strings as std::string{"..."}: a
// string literal would otherwise risk converting to bool.
using ParameterValue = std::variant<bool, std::int64_t, std::string>;
// Values by parameter id.
using Parameters = std::map<std::string, ParameterValue>;

struct ChoiceOption {
    std::string id;
    std::string label;
};

// A parameter is shown and used only while another one, declared before it,
// is itself active and holds one of `values`. An empty `parameter` means always.
struct Condition {
    std::string parameter;
    std::vector<ParameterValue> values;
};

struct ParameterDescriptor {
    std::string id;
    ParameterKind kind = ParameterKind::Text;
    std::string label;
    std::string help;
    ParameterValue defaultValue = std::string{};
    std::vector<ChoiceOption> options;                                // Choice
    std::int64_t minimum = std::numeric_limits<std::int64_t>::min(); // Integer
    std::int64_t maximum = std::numeric_limits<std::int64_t>::max(); // Integer
    // Text and file kinds: an empty value is refused while the parameter is active.
    bool required = false;
    // File kinds: picker filters, e.g. "Title updates (*.xexp)".
    std::vector<std::string> nameFilters;
    // OutputFile: name offered in the save dialog; "{name}" and "{stem}" stand
    // for the source's file name with and without its extension.
    std::string suggestedName;
    Condition visibleWhen;
};

struct OperationDescriptor {
    std::string id;
    std::string name;        // "Decrypt", "Checksum", ...
    std::string description; // one or two sentences for the dialog
    std::vector<ParameterDescriptor> parameters;
    // Rewrites the source file in place (needs a writable filesystem with
    // Capability::Replace). The UI asks before running it.
    bool modifiesSource = false;
    // Which files the operation applies to, beyond its handler recognising
    // them; empty means every file the handler recognises.
    std::function<bool(const FileProbe&)> appliesTo;
};

// Checks the descriptor itself: unique non-empty ids, defaults of the right
// type (an option for Choice, within bounds for Integer), options present for
// Choice, conditions naming an earlier parameter with values of its type.
Status validateDescriptor(const OperationDescriptor& operation);

// Checks `values` against the descriptor and fills in defaults for missing
// parameters. Unknown ids and values of the wrong type are refused; bounds,
// options and `required` are checked only for active parameters.
Status validateParameters(const OperationDescriptor& operation, Parameters& values);

// Whether a parameter is in effect for these values (see Condition).
bool isParameterActive(const OperationDescriptor& operation, const Parameters& values,
                       const std::string& id);

// True if the operation has an OutputFile or OutputFolder parameter.
bool writesOutputs(const OperationDescriptor& operation);

const ParameterDescriptor* findParameter(const OperationDescriptor& operation, const std::string& id);

// `pattern` with "{name}" and "{stem}" replaced from `sourceName`.
std::string expandSuggestedName(const std::string& pattern, const std::string& sourceName);

} // namespace unnamed::core
