// Minimal dependency-free tests for the GUI-free core library.
#include "core/DemoFileSystem.hpp"
#include "core/FileSystemRegistry.hpp"
#include "core/FormatHandlerRegistry.hpp"
#include "core/GenericFileHandler.hpp"
#include "core/LocalFileSystem.hpp"
#include "core/PathUtil.hpp"
#include "core/Transfer.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;
using namespace unnamed::core;

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

namespace {

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

ParameterDescriptor parameter(std::string id, ParameterKind kind, ParameterValue defaultValue) {
    ParameterDescriptor p;
    p.id = std::move(id);
    p.kind = kind;
    p.label = p.id;
    p.defaultValue = std::move(defaultValue);
    return p;
}

// Exercises the parameter kinds and effects the generic handler does not use:
// an input file, text, an output folder and rewriting the source.
class TestHandler final : public FormatHandler {
public:
    std::string id() const override { return "test"; }
    std::string name() const override { return "Test format"; }
    int probe(const FileProbe& file) const override {
        if (file.startsWith("TST1"))
            return kMatchContent;
        return file.extension() == "tst" ? kMatchName : kMatchNone;
    }
    Status describe(ByteSource&, const FileProbe& file, std::vector<PropertyGroup>& out) const override {
        out = {{"Test", {{"Magic", file.startsWith("TST1") ? "TST1" : "none"}}}};
        return Status::success();
    }
    std::vector<OperationDescriptor> operations() const override {
        OperationDescriptor concat;
        concat.id = "concat";
        concat.name = "Concatenate";
        auto input = parameter("input", ParameterKind::InputFile, std::string{});
        input.required = true;
        auto prefix = parameter("prefix", ParameterKind::Text, std::string{"joined"});
        prefix.required = true;
        auto folder = parameter("folder", ParameterKind::OutputFolder, std::string{});
        folder.required = true;
        concat.parameters = {input, prefix, folder};

        OperationDescriptor upper;
        upper.id = "upper";
        upper.name = "Uppercase";
        upper.modifiesSource = true;
        upper.appliesTo = [](const FileProbe& f) { return f.startsWith("TST1"); };

        // Uppercase to a new file, or in place on request.
        OperationDescriptor shout;
        shout.id = "shout";
        shout.name = "Uppercase to";
        auto to = parameter("to", ParameterKind::Choice, std::string{"copy"});
        to.options = {{"copy", "New file"}, {"inplace", "In place"}};
        auto output = parameter("output", ParameterKind::OutputFile, std::string{});
        output.required = true;
        output.visibleWhen = {"to", {std::string{"copy"}}};
        shout.parameters = {to, output};
        shout.modifiesSource = true;
        shout.modifiesSourceWhen = {"to", {std::string{"inplace"}}};
        return {concat, upper, shout};
    }
    Result<std::string> run(const std::string& operationId, OperationContext& ctx) const override {
        auto data = readAll(ctx.source(), 1 << 20, &ctx);
        if (!data)
            return data.status();
        if (operationId == "upper" || operationId == "shout") {
            for (auto& c : data.value())
                c = static_cast<std::uint8_t>(std::toupper(c));
            auto sink = ctx.active("output") ? ctx.createOutputFile("output", data->size())
                                             : ctx.io().replaceSource(data->size());
            if (!sink)
                return sink.status();
            if (const Status st = sink.value()->write(data->data(), data->size()); !st)
                return st;
            if (const Status st = sink.value()->finish(); !st)
                return st;
            return std::string("uppercased");
        }
        auto in = ctx.openInput("input");
        if (!in)
            return in.status();
        auto extra = readAll(*in.value(), 1 << 20);
        if (!extra)
            return extra.status();
        auto out = ctx.createInFolder("folder", ctx.text("prefix") + ".bin", std::nullopt);
        if (!out)
            return out.status();
        data->insert(data->end(), extra->begin(), extra->end());
        if (const Status st = out.value()->write(data->data(), data->size()); !st)
            return st;
        if (const Status st = out.value()->finish(); !st)
            return st;
        return std::string{};
    }
};

// A view of a host folder that can be read but not changed.
class ReadOnlyView final : public FileSystem {
public:
    explicit ReadOnlyView(const LocalFileSystem& inner) : m_inner(inner) {}
    std::string name() const override { return "Read-only view"; }
    Capability capabilities() const override { return Capability::Browse | Capability::Extract; }
    Status list(const std::string& path, std::vector<Entry>& out) const override { return m_inner.list(path, out); }
    Status stat(const std::string& path, Entry& out) const override { return m_inner.stat(path, out); }
    Result<std::unique_ptr<ByteSource>> openRead(const std::string& path) const override {
        return m_inner.openRead(path);
    }

private:
    const LocalFileSystem& m_inner;
};

void testDescriptors() {
    // Every built-in operation is well formed.
    const auto builtins = FormatHandlerRegistry::withBuiltins();
    for (const auto& handler : builtins.handlers()) {
        for (const auto& op : handler->operations())
            CHECK(validateDescriptor(op));
    }
    for (const auto& op : TestHandler().operations())
        CHECK(validateDescriptor(op));

    OperationDescriptor op;
    op.id = "op";
    op.name = "Op";
    auto mode = parameter("mode", ParameterKind::Choice, std::string{"a"});
    mode.options = {{"a", "A"}, {"b", "B"}};
    auto count = parameter("count", ParameterKind::Integer, std::int64_t{2});
    count.minimum = 1;
    count.maximum = 9;
    count.visibleWhen = {"mode", {std::string{"b"}}};
    auto out = parameter("out", ParameterKind::OutputFile, std::string{});
    out.required = true;
    out.visibleWhen = {"count", {std::int64_t{3}}};
    op.parameters = {mode, count, out};
    CHECK(validateDescriptor(op));

    auto broken = [&](auto change) {
        OperationDescriptor copy = op;
        change(copy);
        return !validateDescriptor(copy);
    };
    CHECK(broken([](OperationDescriptor& o) { o.id.clear(); }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].id = "mode"; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].defaultValue = std::string{"2"}; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].defaultValue = std::int64_t{10}; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].minimum = 10; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[0].defaultValue = std::string{"c"}; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[0].options.clear(); }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[2].defaultValue = false; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].visibleWhen.parameter = "out"; })); // later one
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].visibleWhen.parameter = "nope"; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].visibleWhen.values = {true}; }));
    CHECK(broken([](OperationDescriptor& o) { o.parameters[1].visibleWhen.values.clear(); }));

    // Defaults are filled in; hidden parameters are not checked.
    Parameters values;
    CHECK(validateParameters(op, values));
    CHECK(values.size() == 3 && std::get<std::string>(values["mode"]) == "a" && std::get<std::int64_t>(values["count"]) == 2);
    CHECK(!isParameterActive(op, values, "count"));
    CHECK(!isParameterActive(op, values, "out"));
    values = {{"count", std::int64_t{100}}};
    CHECK(validateParameters(op, values)); // out of bounds but hidden

    // Bounds, options and types once active.
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{100}}};
    CHECK(!validateParameters(op, values));
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{0}}};
    CHECK(!validateParameters(op, values));
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{9}}};
    CHECK(validateParameters(op, values));
    values = {{"mode", std::string{"c"}}};
    CHECK(!validateParameters(op, values));
    values = {{"mode", true}};
    CHECK(!validateParameters(op, values));
    values = {{"count", std::string{"3"}}};
    CHECK(!validateParameters(op, values)); // wrong type even while hidden
    values = {{"nope", true}};
    CHECK(!validateParameters(op, values));

    // A chain of conditions: out is required only when mode = b and count = 3.
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{3}}};
    CHECK(isParameterActive(op, values, "out"));
    const Status noOut = validateParameters(op, values);
    CHECK(!noOut && noOut.message == "No out chosen");
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{3}}, {"out", std::string{"/tmp/x"}}};
    CHECK(validateParameters(op, values));
    values = {{"mode", std::string{"a"}}, {"count", std::int64_t{3}}};
    CHECK(!isParameterActive(op, values, "out")); // count itself is hidden
    CHECK(validateParameters(op, values));
    CHECK(writesOutputs(op));
    CHECK(!isParameterActive(op, values, "nope"));

    // Rewriting the source, always or on request.
    CHECK(!rewritesSource(op, {{"mode", std::string{"b"}}}));
    OperationDescriptor edit = op;
    edit.modifiesSource = true;
    CHECK(rewritesSource(edit, {}));
    edit.modifiesSourceWhen = {"mode", {std::string{"b"}}};
    CHECK(validateDescriptor(edit));
    CHECK(!rewritesSource(edit, {}));
    CHECK(rewritesSource(edit, {{"mode", std::string{"b"}}}));
    edit.modifiesSourceWhen = {"count", {std::int64_t{3}}};
    CHECK(validateDescriptor(edit));
    CHECK(!rewritesSource(edit, {{"count", std::int64_t{3}}})); // count is hidden
    CHECK(rewritesSource(edit, {{"mode", std::string{"b"}}, {"count", std::int64_t{3}}}));
    auto brokenEdit = [&](auto change) {
        OperationDescriptor copy = edit;
        change(copy);
        return !validateDescriptor(copy);
    };
    CHECK(brokenEdit([](OperationDescriptor& o) { o.modifiesSource = false; }));
    CHECK(brokenEdit([](OperationDescriptor& o) { o.modifiesSourceWhen.parameter = "nope"; }));
    CHECK(brokenEdit([](OperationDescriptor& o) { o.modifiesSourceWhen.values.clear(); }));
    CHECK(brokenEdit([](OperationDescriptor& o) { o.modifiesSourceWhen.values = {std::string{"3"}}; }));

    // A check across parameters runs after the others.
    OperationDescriptor both = op;
    both.checkValues = [](const Parameters& v) {
        return std::get<std::string>(v.at("mode")) == "a" ? Status::failure("not a") : Status::success();
    };
    values = {{"mode", std::string{"a"}}};
    const Status notA = validateParameters(both, values);
    CHECK(!notA && notA.message == "not a");
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{0}}};
    CHECK(validateParameters(both, values).message != "not a"); // count is checked first
    values = {{"mode", std::string{"b"}}, {"count", std::int64_t{9}}};
    CHECK(validateParameters(both, values));

    CHECK(expandSuggestedName("{stem}.bin", "default.xex") == "default.bin");
    CHECK(expandSuggestedName("{name}.txt", "default.xex") == "default.xex.txt");
    CHECK(expandSuggestedName("{stem}-x", ".hidden") == ".hidden-x");
    CHECK(expandSuggestedName("{stem}", "noext") == "noext");
}

void testChecksums() {
    Crc32 crc;
    crc.update("123456789", 9);
    CHECK(crc.value() == 0xCBF43926u);
    Sha1 empty;
    CHECK(empty.hexDigest() == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    Sha1 abc;
    abc.update("abc", 3);
    CHECK(abc.hexDigest() == "a9993e364706816aba3e25717850c26c9cd0d89d");
    Sha1 twoBlocks;
    const std::string msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    twoBlocks.update(msg.data(), msg.size());
    CHECK(twoBlocks.hexDigest() == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    Sha1 million;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) { // uneven pieces cross the 64-byte blocks
        million.update(chunk.data(), 7);
        million.update(chunk.data() + 7, chunk.size() - 7);
    }
    CHECK(million.hexDigest() == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

void testFormatHandlers(const fs::path& root) {
    const fs::path dir = root / "handlers";
    fs::create_directories(dir / "src");
    fs::create_directories(dir / "out");
    std::ofstream(dir / "src" / "data.bin", std::ios::binary) << "123456789";
    std::ofstream(dir / "src" / "item.tst", std::ios::binary) << "TST1 body";
    std::ofstream(dir / "src" / "plain.tst", std::ios::binary) << "no magic";
    std::ofstream(dir / "extra.bin", std::ios::binary) << "+extra";
    {
        std::ofstream big(dir / "src" / "big.bin", std::ios::binary);
        const std::string block(1 << 16, 'x');
        for (int i = 0; i < 48; ++i) // 3 MiB: several read chunks
            big << block;
    }
    LocalFileSystem local(dir / "src");

    // Probing and the registry.
    auto probe = probeFile(local, "/item.tst");
    CHECK(probe && probe->name == "item.tst" && probe->size == 9 && probe->head.size() == 9);
    CHECK(probe->extension() == "tst" && probe->startsWith("TST1") && !probe->startsWith("TST1 body and more"));
    CHECK(probeFile(local, "/big.bin") && probeFile(local, "/big.bin")->head.size() == kProbeSize);
    CHECK(!probeFile(local, "/missing"));
    DemoFileSystem demo;
    CHECK(!probeFile(demo, "/readme.txt")); // the demo cannot be read
    fs::create_directories(dir / "src" / "folder");
    CHECK(!probeFile(local, "/folder"));

    auto registry = FormatHandlerRegistry::withBuiltins();
    const std::size_t builtins = registry.handlers().size(); // more with optional formats built in
    CHECK(registry.find("file") != nullptr);
    CHECK(registry.find("test") == nullptr);
    registry.add(std::make_shared<TestHandler>());
    registry.add(std::make_shared<TestHandler>()); // same id replaces
    CHECK(registry.handlers().size() == builtins + 1);
    auto matches = registry.match(probe.value());
    CHECK(matches.size() == 2 && matches[0].handler->id() == "test" && matches[0].score == kMatchContent);
    CHECK(matches[1].handler->id() == "file" && matches[1].score == kMatchAnyFile);
    matches = registry.match(probeFile(local, "/data.bin").value());
    CHECK(matches.size() == 1 && matches[0].handler->id() == "file");

    const auto test = registry.find("test");
    CHECK(applicableOperations(*test, probe.value()).size() == 3);
    CHECK(applicableOperations(*test, probeFile(local, "/plain.tst").value()).size() == 2); // no "upper"
    CHECK(applicableOperations(*test, probeFile(local, "/data.bin").value()).empty());
    std::vector<PropertyGroup> groups;
    {
        auto in = local.openRead("/item.tst");
        CHECK(test->describe(*in.value(), probe.value(), groups) && groups.size() == 1);
    }

    const auto generic = registry.find("file");
    HostOperationIo io(&local, "/data.bin");
    const std::string out = pathToUtf8(dir / "out");

    // Checksum, shown and saved.
    auto report = runOperation(*generic, "checksum", local, "/data.bin",
                               {{"algorithm", std::string{"both"}}, {"save", true},
                                {"output", out + "/sums.txt"}},
                               io);
    CHECK(report);
    CHECK(report.value() == "CRC32 (data.bin) = cbf43926\nSHA1 (data.bin) = "
                            "f7c3bc1d808e04732adf679965ccc34ca7ae3441\n");
    CHECK(readFile(dir / "out" / "sums.txt") == report.value());
    // Not saved: the hidden output needs no value.
    report = runOperation(*generic, "checksum", local, "/data.bin", {{"algorithm", std::string{"crc32"}}}, io);
    CHECK(report && report.value() == "CRC32 (data.bin) = cbf43926\n");
    // Saving without a file, an unknown operation, a wrong type.
    CHECK(!runOperation(*generic, "checksum", local, "/data.bin", {{"save", true}}, io));
    CHECK(!runOperation(*generic, "nope", local, "/data.bin", {}, io));
    CHECK(!runOperation(*generic, "checksum", local, "/data.bin", {{"save", std::string{"yes"}}}, io));
    CHECK(!runOperation(*generic, "checksum", local, "/missing", {}, io));

    // Hex dump of part of a file, in hexdump -C layout.
    std::ofstream(dir / "src" / "abc.bin", std::ios::binary) << "ABCDEFGHIJKLMNOPQRSTUVWXYZ\n";
    report = runOperation(*generic, "hexdump", local, "/abc.bin",
                          {{"offset", std::int64_t{2}}, {"length", std::int64_t{20}}, {"output", out + "/abc.hex"}},
                          io);
    CHECK(report && report.value().empty());
    CHECK(readFile(dir / "out" / "abc.hex") ==
          "00000002  43 44 45 46 47 48 49 4a  4b 4c 4d 4e 4f 50 51 52  |CDEFGHIJKLMNOPQR|\n"
          "00000012  53 54 55 56                                       |STUV|\n"
          "00000016\n");
    report = runOperation(*generic, "hexdump", local, "/abc.bin",
                          {{"range", std::string{"whole"}}, {"ascii", false}, {"output", out + "/abc.hex"}}, io);
    CHECK(report);
    CHECK(readFile(dir / "out" / "abc.hex") ==
          "00000000  41 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50\n"
          "00000010  51 52 53 54 55 56 57 58  59 5a 0a\n"
          "0000001b\n");
    CHECK(!runOperation(*generic, "hexdump", local, "/abc.bin",
                        {{"offset", std::int64_t{27}}, {"output", out + "/past.hex"}}, io));
    CHECK(!fs::exists(dir / "out" / "past.hex"));
    CHECK(!runOperation(*generic, "hexdump", local, "/abc.bin",
                        {{"length", std::int64_t{0}}, {"output", out + "/zero.hex"}}, io));

    // Cancelling leaves no output.
    int calls = 0;
    const ProgressFn cancelLater = [&](const TransferProgress& p) {
        ++calls;
        return p.bytesDone == 0;
    };
    report = runOperation(*generic, "hexdump", local, "/big.bin",
                          {{"range", std::string{"whole"}}, {"output", out + "/big.hex"}}, io, cancelLater);
    CHECK(!report && report.status().cancelled && calls > 1);
    CHECK(!fs::exists(dir / "out" / "big.hex"));
    CHECK(!fs::exists(dir / "out" / "big.hex.part"));
    report = runOperation(*generic, "checksum", local, "/big.bin", {}, io, [](const TransferProgress&) { return false; });
    CHECK(!report && report.status().cancelled);
    std::uint64_t lastDone = 0, total = 0;
    report = runOperation(*generic, "checksum", local, "/big.bin", {}, io, [&](const TransferProgress& p) {
        lastDone = p.bytesDone;
        total = p.bytesTotal;
        return true;
    });
    CHECK(report && total == (3u << 20) && lastDone == total);

    // Input file, text and output folder; names inside the folder stay inside it.
    const std::string extra = pathToUtf8(dir / "extra.bin");
    HostOperationIo itemIo(&local, "/item.tst");
    CHECK(runOperation(*test, "concat", local, "/item.tst",
                       {{"input", extra}, {"folder", out}}, itemIo));
    CHECK(readFile(dir / "out" / "joined.bin") == "TST1 body+extra");
    CHECK(!runOperation(*test, "concat", local, "/item.tst", {{"input", extra}, {"folder", out}}, itemIo)); // exists
    CHECK(!runOperation(*test, "concat", local, "/item.tst",
                        {{"input", extra}, {"folder", out}, {"prefix", std::string{"../escaped"}}}, itemIo));
    CHECK(!fs::exists(dir / "escaped.bin"));
    CHECK(!runOperation(*test, "concat", local, "/item.tst", {{"folder", out}}, itemIo)); // no input
    CHECK(!runOperation(*test, "concat", local, "/item.tst",
                        {{"input", pathToUtf8(dir / "nope")}, {"prefix", std::string{"x"}}, {"folder", out}}, itemIo));
    CHECK(!fs::exists(dir / "out" / "x.bin"));
    // A missing output folder is created; a file in its place is refused.
    CHECK(runOperation(*test, "concat", local, "/item.tst", {{"input", extra}, {"folder", out + "/new/deeper"}}, itemIo));
    CHECK(readFile(dir / "out" / "new" / "deeper" / "joined.bin") == "TST1 body+extra");
    CHECK(!runOperation(*test, "concat", local, "/item.tst",
                        {{"input", extra}, {"folder", out + "/joined.bin"}, {"prefix", std::string{"y"}}}, itemIo));

    // Rewriting the source needs a filesystem that allows it.
    CHECK(runOperation(*test, "upper", local, "/item.tst", {}, itemIo));
    CHECK(readFile(dir / "src" / "item.tst") == "TST1 BODY");
    CHECK(!runOperation(*test, "upper", local, "/plain.tst", {}, itemIo)); // does not apply
    ReadOnlyView view(local);
    HostOperationIo viewIo(&view, "/item.tst");
    CHECK(!runOperation(*test, "upper", view, "/item.tst", {}, viewIo));
    HostOperationIo noSource(nullptr, {});
    CHECK(!noSource.replaceSource(1));
    CHECK(runOperation(*generic, "checksum", view, "/item.tst", {}, viewIo)); // reading is enough

    // Rewriting on request: a new file works on a read-only place, in place does not.
    std::ofstream(dir / "src" / "ask.tst", std::ios::binary) << "TST1 ask";
    CHECK(runOperation(*test, "shout", view, "/ask.tst", {{"output", out + "/ask.out"}}, viewIo));
    CHECK(readFile(dir / "out" / "ask.out") == "TST1 ASK" && readFile(dir / "src" / "ask.tst") == "TST1 ask");
    CHECK(!runOperation(*test, "shout", view, "/ask.tst", {{"to", std::string{"inplace"}}}, viewIo));
    CHECK(!runOperation(*test, "shout", local, "/ask.tst", {{"to", std::string{"copy"}}}, itemIo)); // no output
    HostOperationIo askIo(&local, "/ask.tst");
    CHECK(runOperation(*test, "shout", local, "/ask.tst", {{"to", std::string{"inplace"}}}, askIo));
    CHECK(readFile(dir / "src" / "ask.tst") == "TST1 ASK");

    // Inputs and outputs inside another filesystem.
    fs::create_directories(dir / "other" / "sub");
    std::ofstream(dir / "other" / "more.bin", std::ios::binary) << "+more";
    LocalFileSystem other(dir / "other");
    FileSystemOperationIo otherIo(other, &local, "/item.tst");
    const Parameters inOther = {{"input", std::string{"/more.bin"}}, {"folder", std::string{"/sub"}}};
    CHECK(runOperation(*test, "concat", local, "/item.tst", inOther, otherIo));
    CHECK(readFile(dir / "other" / "sub" / "joined.bin") == "TST1 BODY+more");
    CHECK(!runOperation(*test, "concat", local, "/item.tst", inOther, otherIo)); // exists
    Parameters newFolder = inOther;
    newFolder["folder"] = std::string{"/made"};
    CHECK(runOperation(*test, "concat", local, "/item.tst", newFolder, otherIo));
    CHECK(readFile(dir / "other" / "made" / "joined.bin") == "TST1 BODY+more");
    newFolder["folder"] = std::string{"/no/parent"};
    CHECK(!runOperation(*test, "concat", local, "/item.tst", newFolder, otherIo));
    Parameters escaping = inOther;
    escaping["prefix"] = std::string{"../up"};
    CHECK(!runOperation(*test, "concat", local, "/item.tst", escaping, otherIo));
    CHECK(!fs::exists(dir / "other" / "up.bin"));
    Parameters missing = inOther;
    missing["input"] = std::string{"/nope"};
    CHECK(!runOperation(*test, "concat", local, "/item.tst", missing, otherIo));
    const Parameters shoutOut = {{"output", std::string{"/shout.txt"}}};
    CHECK(runOperation(*test, "shout", local, "/plain.tst", shoutOut, otherIo));
    CHECK(runOperation(*test, "shout", local, "/plain.tst", shoutOut, otherIo)); // an output file is replaced
    CHECK(readFile(dir / "other" / "shout.txt") == "NO MAGIC" && readFile(dir / "src" / "plain.tst") == "no magic");
    CHECK(runOperation(*test, "upper", local, "/item.tst", {}, otherIo)); // the source, not the target
    CHECK(!fs::exists(dir / "other" / "item.tst"));
    FileSystemOperationIo noReplace(other, &view, "/item.tst");
    CHECK(!noReplace.replaceSource(1));
    CHECK(!otherIo.openInput({}) && !otherIo.createOutput({}, {}, std::nullopt));
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "unnamed_core_tests";
    fs::remove_all(root);
    fs::create_directories(root / "dir");
    std::ofstream(root / "a.txt") << "hello";
    std::ofstream(root / "src.bin") << "data";

    LocalFileSystem local(root);

    std::vector<Entry> entries;
    CHECK(local.list("/", entries));
    CHECK(entries.size() == 3);

    CHECK(!local.list("/../etc", entries));
    CHECK(!local.remove("/"));

    // Streams, make folder, rename, remove.
    {
        auto sink = local.openWrite("/dir/new.bin", 3, false);
        CHECK(sink);
        CHECK(!fs::exists(root / "dir" / "new.bin")); // invisible until finish()
        CHECK(sink.value()->write("abc", 3));
        CHECK(sink.value()->finish());
        CHECK(fs::file_size(root / "dir" / "new.bin") == 3);
        CHECK(!local.openWrite("/dir/new.bin", 3, false));  // exists, no overwrite
        CHECK(local.openWrite("/dir/new.bin", 3, true));   // overwrite allowed (dropped = discarded)
        CHECK(fs::file_size(root / "dir" / "new.bin") == 3);
        CHECK(!fs::exists(root / "dir" / "new.bin.part"));  // discarded sink cleans up
    }
    CHECK(local.makeDirectory("/dir/sub"));
    CHECK(!local.makeDirectory("/dir/sub"));
    CHECK(local.rename("/dir/new.bin", "renamed.bin"));
    CHECK(fs::exists(root / "dir" / "renamed.bin"));
    CHECK(!local.rename("/dir/renamed.bin", "../escape"));
    CHECK(!local.rename("/dir/renamed.bin", "sub")); // target exists
    {
        auto in = local.openRead("/dir/renamed.bin");
        CHECK(in);
        char buf[8];
        auto n = in.value()->read(buf, sizeof buf);
        CHECK(n && n.value() == 3 && std::string(buf, 3) == "abc");
        CHECK(in.value()->size() == 3u);
    }
    Entry st;
    CHECK(local.stat("/dir/renamed.bin", st) && st.size == 3 && st.kind == "file");
    CHECK(local.stat("/dir", st) && st.type == EntryType::Directory);
    CHECK(!local.stat("/nope", st));
    CHECK(local.remove("/dir/renamed.bin"));
    CHECK(!fs::exists(root / "dir" / "renamed.bin"));
    CHECK(local.remove("/dir/sub"));

    // copyTree: host -> host tree copy with progress, merge and cancel.
    {
        fs::create_directories(root / "tree" / "a" / "b");
        std::ofstream(root / "tree" / "one.txt") << "1111";
        std::ofstream(root / "tree" / "a" / "two.txt") << "22";
        std::ofstream(root / "tree" / "a" / "b" / "three.txt") << "333";
        fs::create_directories(root / "out");
        LocalFileSystem dest(root / "out");

        std::uint64_t lastDone = 0, total = 0;
        TransferOptions opts;
        opts.progress = [&](const TransferProgress& p) {
            lastDone = p.bytesDone;
            total = p.bytesTotal;
            return true;
        };
        CHECK(copyTree(local, "/tree", dest, "/tree", opts));
        CHECK(total == 9 && lastDone == 9);
        CHECK(fs::file_size(root / "out" / "tree" / "a" / "b" / "three.txt") == 3);

        // Existing files fail unless overwrite is set; folders merge.
        CHECK(!copyTree(local, "/tree", dest, "/tree"));
        TransferOptions over;
        over.overwrite = true;
        CHECK(copyTree(local, "/tree", dest, "/tree", over));

        // Cancel leaves no partial file behind.
        TransferOptions cancel;
        cancel.overwrite = true;
        cancel.progress = [](const TransferProgress& p) { return p.bytesDone == 0; };
        const Status st2 = copyTree(local, "/tree/one.txt", dest, "/cancelled.txt", cancel);
        CHECK(!st2 && st2.cancelled);
        CHECK(!fs::exists(root / "out" / "cancelled.txt"));
        CHECK(!fs::exists(root / "out" / "cancelled.txt.part"));

        // Single file into a fresh name, and a missing source.
        CHECK(copyTree(local, "/tree/one.txt", dest, "/renamed-copy.txt"));
        CHECK(!copyTree(local, "/missing", dest, "/x"));
    }

    // Memory streams.
    {
        MemorySource src(std::string("hello"));
        MemorySink sink;
        char buf[3];
        for (;;) {
            auto n = src.read(buf, sizeof buf);
            CHECK(n);
            if (n.value() == 0)
                break;
            sink.write(buf, n.value());
        }
        CHECK(sink.finish() && sink.data().size() == 5);
    }

    CHECK(parentPath("/dir/x") == "/dir");
    CHECK(parentPath("/dir") == "/");
    CHECK(joinPath("/", "x") == "/x");
    CHECK(joinPath("/dir", "x") == "/dir/x");

    // Kinds, timestamps and the expanded (details) view of the local backend.
    CHECK(kindFromName("default.xex") == "xex");
    CHECK(kindFromName("LAUNCH.INI") == "ini");
    CHECK(kindFromName("noextension") == "file");
    CHECK(local.list("/", entries));
    for (const Entry& e : entries) {
        if (e.name == "dir")
            CHECK(e.kind == "folder");
        if (e.name == "a.txt") {
            CHECK(e.kind == "file");
            CHECK(e.modified > 0);
        }
    }
    Details details;
    CHECK(local.describe("/a.txt", details));
    CHECK(details.title == "a.txt");
    CHECK(!details.groups.empty() && details.groups[0].title == "General");
    CHECK(!local.describe("/../etc", details));
    CHECK(local.describeFileSystem(details));
    CHECK(details.kind == "filesystem");
    CHECK(hasCapability(local.capabilities(), Capability::Inspect));

    // Demo (sample data) filesystem: browsable, inspectable, read-only.
    DemoFileSystem demo;
    CHECK(!hasCapability(demo.capabilities(), Capability::Remove));
    CHECK(demo.list("/Games/Sample Title", entries));
    CHECK(entries.size() == 2);
    CHECK(demo.describe("/Games/Sample Title/default.xex", details));
    CHECK(details.kind == "xex");
    CHECK(!details.notice.empty());
    bool hasExecutable = false;
    for (const auto& g : details.groups)
        hasExecutable = hasExecutable || g.title == "Executable";
    CHECK(hasExecutable);
    CHECK(demo.describeFileSystem(details));
    CHECK(!demo.list("/nope", entries));
    CHECK(!demo.remove("/readme.txt")); // unsupported by default
    CHECK(!demo.openRead("/readme.txt"));
    CHECK(!demo.makeDirectory("/new"));
    CHECK(demo.stat("/Games/Sample Title/default.xex", st) && st.kind == "xex");

    auto registry = FileSystemRegistry::withBuiltins();
    std::string error;
    CHECK(registry.open("Local", root, error) != nullptr);
    CHECK(registry.open("Nope", root, error) == nullptr);
    CHECK(registry.open("Demo", {}, error) != nullptr);
    CHECK(registry.open("Local", root / "a.txt", error) == nullptr);

    testDescriptors();
    testChecksums();
    testFormatHandlers(root);

    fs::remove_all(root);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
