#include "core/OperationDescriptor.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace unnamed::core {

namespace {

// Index of the variant alternative a kind's values use.
std::size_t valueIndex(ParameterKind kind) {
    switch (kind) {
    case ParameterKind::Boolean: return 0;
    case ParameterKind::Integer: return 1;
    default: return 2;
    }
}

const char* typeName(ParameterKind kind) {
    switch (kind) {
    case ParameterKind::Boolean: return "true or false";
    case ParameterKind::Integer: return "a whole number";
    default: return "text";
    }
}

bool isFileKind(ParameterKind kind) {
    return kind == ParameterKind::InputFile || kind == ParameterKind::OutputFile ||
           kind == ParameterKind::OutputFolder;
}

// "Output file" -> "output file", but "IDC script" stays.
std::string lowerFirst(std::string text) {
    if (text.size() > 1 && std::islower(static_cast<unsigned char>(text[1])))
        text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    return text;
}

bool hasOption(const ParameterDescriptor& p, const std::string& id) {
    return std::any_of(p.options.begin(), p.options.end(), [&](const ChoiceOption& o) { return o.id == id; });
}

// Checks a value of the right type against options, bounds and `required`.
Status checkValue(const ParameterDescriptor& p, const ParameterValue& value) {
    const std::string label = p.label.empty() ? p.id : p.label;
    switch (p.kind) {
    case ParameterKind::Choice:
        if (!hasOption(p, std::get<std::string>(value)))
            return Status::failure(label + ": “" + std::get<std::string>(value) + "” is not one of the choices");
        break;
    case ParameterKind::Integer: {
        const std::int64_t v = std::get<std::int64_t>(value);
        if (v < p.minimum || v > p.maximum)
            return Status::failure(label + " must be between " + std::to_string(p.minimum) + " and " +
                                   std::to_string(p.maximum));
        break;
    }
    case ParameterKind::Boolean:
        break;
    default:
        if (p.required && std::get<std::string>(value).empty())
            return Status::failure(isFileKind(p.kind) ? "No " + lowerFirst(label) + " chosen" : label + " is required");
        break;
    }
    return Status::success();
}

// A condition's values must be values its parameter can hold: of its type,
// and options of a Choice (a condition no value meets hides a parameter for
// good, `required` included).
Status checkCondition(const Condition& when, const ParameterDescriptor& other, const std::string& where) {
    if (when.values.empty())
        return Status::failure(where + " without values");
    for (const ParameterValue& v : when.values) {
        if (v.index() != valueIndex(other.kind))
            return Status::failure(where + ": value must be " + typeName(other.kind));
        if (other.kind == ParameterKind::Choice && !hasOption(other, std::get<std::string>(v)))
            return Status::failure(where + ": “" + std::get<std::string>(v) + "” is not an option of " + other.id);
    }
    return Status::success();
}

bool active(const OperationDescriptor& op, const Parameters& values, const ParameterDescriptor& p, int depth) {
    if (p.visibleWhen.parameter.empty())
        return true;
    const ParameterDescriptor* other = findParameter(op, p.visibleWhen.parameter);
    if (!other || depth > static_cast<int>(op.parameters.size()) || !active(op, values, *other, depth + 1))
        return false;
    const auto it = values.find(other->id);
    const ParameterValue& current = it != values.end() ? it->second : other->defaultValue;
    return std::find(p.visibleWhen.values.begin(), p.visibleWhen.values.end(), current) != p.visibleWhen.values.end();
}

} // namespace

const ParameterDescriptor* findParameter(const OperationDescriptor& operation, const std::string& id) {
    for (const ParameterDescriptor& p : operation.parameters) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

Status validateDescriptor(const OperationDescriptor& operation) {
    if (operation.id.empty() || operation.name.empty())
        return Status::failure("Operation without an id or name");
    std::set<std::string> seen;
    for (const ParameterDescriptor& p : operation.parameters) {
        const std::string where = operation.id + "." + p.id;
        if (p.id.empty())
            return Status::failure(operation.id + ": parameter without an id");
        if (seen.count(p.id))
            return Status::failure(where + ": duplicate parameter id");
        if (p.defaultValue.index() != valueIndex(p.kind))
            return Status::failure(where + ": default must be " + typeName(p.kind));
        if (p.kind == ParameterKind::Choice && p.options.empty())
            return Status::failure(where + ": a choice needs options");
        std::set<std::string> optionIds;
        for (const ChoiceOption& o : p.options) {
            if (o.id.empty() || !optionIds.insert(o.id).second)
                return Status::failure(where + ": option ids must be unique and not empty");
        }
        if (p.kind == ParameterKind::Integer && p.minimum > p.maximum)
            return Status::failure(where + ": minimum is above maximum");
        if (p.kind == ParameterKind::Choice || p.kind == ParameterKind::Integer) {
            if (const Status st = checkValue(p, p.defaultValue); !st)
                return Status::failure(where + ": default: " + st.message);
        }
        if (!p.visibleWhen.parameter.empty()) {
            const ParameterDescriptor* other = findParameter(operation, p.visibleWhen.parameter);
            if (!other || !seen.count(other->id))
                return Status::failure(where + ": condition must name an earlier parameter");
            if (const Status st = checkCondition(p.visibleWhen, *other, where + ": condition"); !st)
                return st;
        }
        seen.insert(p.id);
    }
    if (const Condition& when = operation.modifiesSourceWhen; !when.parameter.empty()) {
        const std::string where = operation.id + ": modifiesSourceWhen";
        if (!operation.modifiesSource)
            return Status::failure(where + " without modifiesSource");
        const ParameterDescriptor* other = findParameter(operation, when.parameter);
        if (!other)
            return Status::failure(where + " must name a parameter");
        if (const Status st = checkCondition(when, *other, where); !st)
            return st;
    }
    return Status::success();
}

Status validateParameters(const OperationDescriptor& operation, Parameters& values) {
    for (const auto& [id, value] : values) {
        const ParameterDescriptor* p = findParameter(operation, id);
        if (!p)
            return Status::failure(operation.name + " has no parameter “" + id + "”");
        if (value.index() != valueIndex(p->kind))
            return Status::failure((p->label.empty() ? p->id : p->label) + " must be " + typeName(p->kind));
    }
    for (const ParameterDescriptor& p : operation.parameters)
        values.emplace(p.id, p.defaultValue);
    for (const ParameterDescriptor& p : operation.parameters) {
        if (!isParameterActive(operation, values, p.id))
            continue;
        if (const Status st = checkValue(p, values.at(p.id)); !st)
            return st;
    }
    return operation.checkValues ? operation.checkValues(values) : Status::success();
}

bool isParameterActive(const OperationDescriptor& operation, const Parameters& values, const std::string& id) {
    const ParameterDescriptor* p = findParameter(operation, id);
    return p && active(operation, values, *p, 0);
}

bool rewritesSource(const OperationDescriptor& operation, const Parameters& values) {
    if (!operation.modifiesSource)
        return false;
    const Condition& when = operation.modifiesSourceWhen;
    if (when.parameter.empty())
        return true;
    if (!isParameterActive(operation, values, when.parameter))
        return false;
    const auto it = values.find(when.parameter);
    const ParameterValue& current =
        it != values.end() ? it->second : findParameter(operation, when.parameter)->defaultValue;
    return std::find(when.values.begin(), when.values.end(), current) != when.values.end();
}

bool writesOutputs(const OperationDescriptor& operation) {
    return std::any_of(operation.parameters.begin(), operation.parameters.end(), [](const ParameterDescriptor& p) {
        return p.kind == ParameterKind::OutputFile || p.kind == ParameterKind::OutputFolder;
    });
}

std::string expandSuggestedName(const std::string& pattern, const std::string& sourceName) {
    const auto dot = sourceName.find_last_of('.');
    const std::string stem = dot == std::string::npos || dot == 0 ? sourceName : sourceName.substr(0, dot);
    std::string out;
    for (std::size_t i = 0; i < pattern.size();) {
        if (pattern.compare(i, 6, "{name}") == 0) {
            out += sourceName;
            i += 6;
        } else if (pattern.compare(i, 6, "{stem}") == 0) {
            out += stem;
            i += 6;
        } else {
            out += pattern[i++];
        }
    }
    return out;
}

} // namespace unnamed::core
