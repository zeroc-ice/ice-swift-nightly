// Copyright (c) ZeroC, Inc.

#include "Gen.h"
#include "../Ice/FileUtil.h"
#include "../Slice/FileTracker.h"
#include "../Slice/Util.h"
#include "Ice/StringUtil.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <string>

using namespace std;
using namespace Slice;
using namespace IceInternal;

namespace
{
    bool isConstexprType(const TypePtr& type)
    {
        BuiltinPtr bp = dynamic_pointer_cast<Builtin>(type);
        if (bp)
        {
            switch (bp->kind())
            {
                case Builtin::KindByte:
                case Builtin::KindBool:
                case Builtin::KindShort:
                case Builtin::KindInt:
                case Builtin::KindLong:
                case Builtin::KindFloat:
                case Builtin::KindDouble:
                case Builtin::KindValue:
                case Builtin::KindObjectProxy:
                {
                    return true;
                }
                default:
                {
                    return false;
                }
            }
        }
        else if (
            dynamic_pointer_cast<Enum>(type) || dynamic_pointer_cast<InterfaceDecl>(type) ||
            dynamic_pointer_cast<ClassDecl>(type))
        {
            return true;
        }
        else
        {
            StructPtr s = dynamic_pointer_cast<Struct>(type);
            if (s)
            {
                DataMemberList members = s->dataMembers();
                for (const auto& member : members)
                {
                    if (!isConstexprType(member->type()))
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }
    }

    string getDeprecatedAttribute(const ContainedPtr& p1)
    {
        string deprecatedAttribute;
        if (p1->isDeprecated())
        {
            if (auto reason = p1->getDeprecationReason())
            {
                deprecatedAttribute = "[[deprecated(\"" + *reason + "\")]] ";
            }
            else
            {
                deprecatedAttribute = "[[deprecated]] ";
            }
        }
        return deprecatedAttribute;
    }

    void writeConstantValue(
        IceInternal::Output& out,
        const TypePtr& type,
        const SyntaxTreeBasePtr& valueType,
        const string& value,
        TypeContext typeContext,
        const MetadataList& metadata,
        const string& scope)
    {
        ConstPtr constant = dynamic_pointer_cast<Const>(valueType);
        if (constant)
        {
            out << getUnqualified(constant->mappedScoped("::", true), scope);
        }
        else
        {
            BuiltinPtr bp = dynamic_pointer_cast<Builtin>(type);
            if (bp && bp->kind() == Builtin::KindString)
            {
                if ((typeContext & TypeContext::UseWstring) != TypeContext::None ||
                    findMetadata(metadata) == "wstring") // wide strings
                {
                    out << "L\"";
                    out << toStringLiteral(value, "\a\b\f\n\r\t\v", "?", UCN, 0);
                    out << "\"";
                }
                else // narrow strings
                {
                    out << "\"" << toStringLiteral(value, "\a\b\f\n\r\t\v", "?", Octal, 0) << "\"";
                }
            }
            else if (bp && bp->kind() == Builtin::KindLong)
            {
                out << "INT64_C(" << value << ")";
            }
            else if (bp && bp->kind() == Builtin::KindFloat)
            {
                out << value;
                if (value.find('.') == string::npos)
                {
                    out << ".0";
                }
                out << "F";
            }
            else
            {
                EnumPtr ep = dynamic_pointer_cast<Enum>(type);
                if (ep && valueType)
                {
                    EnumeratorPtr enumerator = dynamic_pointer_cast<Enumerator>(valueType);
                    assert(enumerator);
                    out << getUnqualified(enumerator->mappedScoped("::", true), scope);
                }
                else if (!ep)
                {
                    out << value;
                }
            }
        }
    }

    // Marshals the parameters of an outgoing request.
    void
    writeInParamsLambda(IceInternal::Output& C, const OperationPtr& p, const ParameterList& inParams, const string&)
    {
        if (inParams.empty())
        {
            C << "nullptr";
        }
        else
        {
            C << "[&](Ice::OutputStream* ostr)";
            C << sb;
            writeMarshalCode(C, inParams, nullptr);
            if (p->sendsClasses())
            {
                C << nl << "ostr->writePendingValues();";
            }
            C << eb;
        }
    }

    void throwUserExceptionLambda(IceInternal::Output& C, ExceptionList throws, const string& scope)
    {
        if (throws.empty())
        {
            C << "nullptr";
        }
        else
        {
            // Arrange exceptions into most-derived to least-derived order. If we don't
            // do this, a base exception handler can appear before a derived exception
            // handler, causing compiler warnings and resulting in the base exception
            // being marshaled instead of the derived exception.
            throws.sort(Slice::DerivedToBaseCompare());

            C << "[](const Ice::UserException& ex)";
            C << sb;
            C << nl << "try";
            C << sb;
            C << nl << "ex.ice_throw();";
            C << eb;

            // Generate a catch block for each legal user exception.
            for (const auto& ex : throws)
            {
                C << nl << "catch (const " << getUnqualified(ex->mappedScoped("::", true), scope) << "&)";
                C << sb;
                C << nl << "throw;";
                C << eb;
            }
            C << nl << "catch (const Ice::UserException&)";
            C << sb;
            C << eb;
            C << eb;
        }
    }

    string marshaledResultStructName(const string& name)
    {
        assert(!name.empty());
        string stName = IceInternal::toUpper(name.substr(0, 1)) + name.substr(1);
        stName += "MarshaledResult";
        return stName;
    }

    string condMove(bool moveIt, const string& str) { return moveIt ? string("std::move(") + str + ")" : str; }

    /// Ensures that there is no collision between 'name' and any of the parameters in the provided 'params' list.
    /// If a collision exists, we return 'name' with an underscore appended to it. Otherwise we return 'name' as-is.
    string escapeParam(const ParameterList& params, const string& name)
    {
        for (const auto& param : params)
        {
            if (param->mappedName() == name)
            {
                return name + "_";
            }
        }
        return name;
    }

    void writeDocLines(Output& out, const StringList& lines, bool commentFirst, const string& space = " ")
    {
        auto l = lines.cbegin();
        if (!commentFirst)
        {
            assert(l != lines.cend());
            out << *l;
            l++;
        }
        for (; l != lines.cend(); ++l)
        {
            out << nl << "///";
            if (!l->empty())
            {
                out << space << *l;
            }
        }
    }

    void writeSeeAlso(Output& out, const StringList& lines, const string& space = " ")
    {
        for (const string& line : lines)
        {
            out << nl << "///";
            if (!line.empty())
            {
                out << space << "@see " << line;
            }
        }
    }

    struct DocSummaryOptions
    {
        bool generateDeprecated{true};
        bool includeHeaderFile{false};
    };

    void writeDocSummary(Output& out, const ContainedPtr& p, DocSummaryOptions options = {})
    {
        const optional<DocComment>& doc = p->docComment();
        if (!doc)
        {
            return;
        }

        const StringList& overview = doc->overview();
        if (!overview.empty())
        {
            writeDocLines(out, overview, true);
        }

        const StringList& remarks = doc->remarks();
        if (!remarks.empty())
        {
            out << nl << "/// @remarks ";
            writeDocLines(out, remarks, false);
        }

        const StringList& seeAlso = doc->seeAlso();
        if (!seeAlso.empty())
        {
            writeSeeAlso(out, seeAlso);
        }

        if (options.generateDeprecated)
        {
            const StringList& deprecated = doc->deprecated();
            if (!deprecated.empty())
            {
                out << nl << "///";
                out << nl << "/// @deprecated ";
                writeDocLines(out, deprecated, false);
            }
            else if (doc->isDeprecated())
            {
                out << nl << "///";
                out << nl << "/// @deprecated";
            }
        }

        string_view file = p->file();
        assert(!file.empty());
        DefinitionContextPtr dc = p->unit()->findDefinitionContext(file);
        assert(dc);

        if (options.includeHeaderFile)
        {
            // The metadata directive is cpp:doxygen, not cpp:doxygen:include. The arg of cpp:doxygen is something like
            // include:Ice/Ice.h.
            if (auto headerFile = dc->getMetadataArgs("cpp:doxygen"))
            {
                out << nl << "/// @headerfile " << headerFile->substr(8); // remove include: prefix
            }
        }
    }

    enum OpDocParamType
    {
        OpDocInParams,
        OpDocAllParams
    };

    // For all parameters or only in parameters.
    void writeOpDocParams(
        Output& out,
        const OperationPtr& op,
        const DocComment& doc,
        OpDocParamType type,
        const StringList& preParams = StringList(),
        const StringList& postParams = StringList())
    {
        ParameterList params;
        bool showOut = false;
        switch (type)
        {
            case OpDocInParams:
                params = op->inParameters();
                break;
            case OpDocAllParams:
                params = op->parameters();
                showOut = true;
                break;
        }

        if (!preParams.empty())
        {
            writeDocLines(out, preParams, true);
        }

        const map<string, StringList>& paramDoc = doc.parameters();
        for (const auto& param : params)
        {
            // We want to lookup the parameter by its slice identifier, ignoring any 'cpp:identifier' metadata.
            auto q = paramDoc.find(param->name());
            if (q != paramDoc.end())
            {
                // We only show [out], and never the [in] default.
                string direction = showOut && param->isOutParam() ? "[out]" : "";

                // But when we emit the parameter's name, we want it to take 'cpp:identifier' metadata into account.
                out << nl << "/// @param" << direction << ' ' << param->mappedName() << ' ';
                writeDocLines(out, q->second, false);
            }
        }

        if (!postParams.empty())
        {
            writeDocLines(out, postParams, true);
        }
    }

    // Only return + out parameters as a markdown list.
    StringList createOpOutParamsDoc(const OperationPtr& op, const DocComment& doc)
    {
        ParameterList outParams = op->outParameters();
        StringList result = doc.returns();

        if (!result.empty())
        {
            if (outParams.empty())
            {
                result.front().insert(0, "- ");
            }
            else
            {
                result.front().insert(0, "- `returnValue` ");
            }
        }

        const map<string, StringList>& paramDoc = doc.parameters();
        for (const auto& param : outParams)
        {
            auto q = paramDoc.find(param->name());
            if (q != paramDoc.end()) // it's documented
            {
                auto outParamDoc = q->second;
                outParamDoc.front().insert(0, "- `" + param->mappedName() + "` ");
                result.splice(result.end(), std::move(outParamDoc));
            }
        }

        return result;
    }

    void writeOpDocExceptions(Output& out, const OperationPtr& op, const DocComment& doc)
    {
        for (const auto& [name, lines] : doc.exceptions())
        {
            string scopedName = name;
            // Try to locate the exception's definition using the name given in the comment.
            ExceptionPtr ex = op->container()->lookupException(name, false);
            if (ex)
            {
                scopedName = ex->mappedScoped("::");
            }
            out << nl << "/// @throws " << scopedName << " ";
            writeDocLines(out, lines, false);
        }
    }

    void writeOpDocSummary(
        Output& out,
        const OperationPtr& op,
        const DocComment& doc,
        OpDocParamType type,
        bool showExceptions,
        DocSummaryOptions options = {},
        const StringList& preParams = StringList(),
        const StringList& postParams = StringList(),
        const StringList& returns = StringList())
    {
        const StringList& overview = doc.overview();
        if (!overview.empty())
        {
            writeDocLines(out, overview, true);
        }

        writeOpDocParams(out, op, doc, type, preParams, postParams);

        if (!returns.empty())
        {
            out << nl << "/// @return ";
            writeDocLines(out, returns, false);
        }

        if (showExceptions)
        {
            writeOpDocExceptions(out, op, doc);
        }

        const StringList& remarks = doc.remarks();
        if (!remarks.empty())
        {
            out << nl << "/// @remarks ";
            writeDocLines(out, remarks, false);
        }

        const StringList& seeAlso = doc.seeAlso();
        if (!seeAlso.empty())
        {
            writeSeeAlso(out, seeAlso);
        }

        if (options.generateDeprecated)
        {
            const StringList& deprecated = doc.deprecated();
            if (!deprecated.empty())
            {
                out << nl << "///";
                out << nl << "/// @deprecated ";
                writeDocLines(out, deprecated, false);
            }
            else if (doc.isDeprecated())
            {
                out << nl << "///";
                out << nl << "/// @deprecated";
            }
        }
    }

    // Returns the client-side result type for an operation - can be void, a single type, or a tuple.
    string createOutgoingAsyncTypeParam(const vector<string>& elements)
    {
        switch (elements.size())
        {
            case 0:
                return "void";
            case 1:
                return elements.front();
            default:
            {
                ostringstream os;
                Output out(os);
                out << "std::tuple";
                out.spar("<");
                for (const auto& element : elements)
                {
                    out << element;
                }
                out.epar(">");
                return os.str();
            }
        }
    }

    // Returns the C++ types that make up the client-side result type of an operation (first return type then out
    // parameter types, as per the future API).
    vector<string> createOutgoingAsyncParams(const OperationPtr& p, const string& scope, TypeContext typeContext)
    {
        vector<string> elements;
        TypePtr ret = p->returnType();
        if (ret)
        {
            elements.push_back(typeToString(ret, p->returnIsOptional(), scope, p->getMetadata(), typeContext));
        }
        for (const auto& param : p->outParameters())
        {
            elements.push_back(
                typeToString(param->type(), param->optional(), scope, param->getMetadata(), typeContext));
        }
        return elements;
    }

    string createLambdaResponse(const OperationPtr& p, TypeContext typeContext)
    {
        ostringstream os;
        Output out(os);
        out << "std::function<void" << spar << createOutgoingAsyncParams(p, "", typeContext) << epar << ">";
        return os.str();
    }
}

Slice::Gen::Gen(
    string base,
    string headerExtension,
    string sourceExtension,
    const vector<string>& extraHeaders,
    string include,
    const vector<string>& includePaths,
    string dllExport,
    string dir)
    : _base(std::move(base)),
      _headerExtension(std::move(headerExtension)),
      _sourceExtension(std::move(sourceExtension)),
      _extraHeaders(extraHeaders),
      _include(std::move(include)),
      _includePaths(includePaths),
      _dllExport(std::move(dllExport)),
      _dir(std::move(dir))
{
    for (auto& includePath : _includePaths)
    {
        includePath = fullPath(includePath);
    }

    string::size_type pos = _base.find_last_of("/\\");
    if (pos != string::npos)
    {
        _base.erase(0, pos + 1);
    }
}

Slice::Gen::~Gen()
{
    H << sp;
    H << nl << "// NOLINTEND(modernize-concat-nested-namespaces)";
    H << sp << nl << "#include <Ice/PopDisableWarnings.h>";
    H << nl << "#endif\n";
    C << '\n';

    H.close();
    C.close();
}

void
Slice::Gen::generate(const UnitPtr& p)
{
    string_view file = p->topLevelFile();
    DefinitionContextPtr dc = p->findDefinitionContext(file);
    assert(dc);

    // Give precedence to header-ext/source-ext file metadata.
    string headerExtension = getHeaderExt(file, p);
    if (!headerExtension.empty())
    {
        _headerExtension = headerExtension;
    }

    string sourceExtension = getSourceExt(file, p);
    if (!sourceExtension.empty())
    {
        _sourceExtension = sourceExtension;
    }

    // Give precedence to --dll-export command-line option.
    if (_dllExport.empty())
    {
        if (auto dllExport = dc->getMetadataArgs("cpp:dll-export"))
        {
            _dllExport = *dllExport;
        }
    }

    string fileH = _base + "." + _headerExtension;
    string fileC = _base + "." + _sourceExtension;
    if (!_dir.empty())
    {
        fileH = _dir + '/' + fileH;
        fileC = _dir + '/' + fileC;
    }

    H.open(fileH.c_str());
    if (!H)
    {
        ostringstream os;
        os << "cannot open '" << fileH << "': " << IceInternal::errorToString(errno);
        throw FileException(os.str());
    }
    FileTracker::instance()->addFile(fileH);

    C.open(fileC.c_str());
    if (!C)
    {
        ostringstream os;
        os << "cannot open '" << fileC << "': " << IceInternal::errorToString(errno);
        throw FileException(os.str());
    }
    FileTracker::instance()->addFile(fileC);

    printHeader(H);
    printGeneratedHeader(H, _base + ".ice");
    printHeader(C);
    printGeneratedHeader(C, _base + ".ice");

    // Reformatting moves NOLINT comments which is undesirable.
    H << nl << "// clang-format off";
    H << sp;

    string s = _base + "." + _headerExtension;
    if (_include.size())
    {
        s = _include + '/' + s;
    }
    transform(s.begin(), s.end(), s.begin(), ToIfdef());
    H << "\n#ifndef " << s << "_";
    H << "\n#define " << s << "_";
    H << '\n';

    validateCppMetadata(p);

    C << sp;

    // Main use-case: including a "pre-compiled" header at the very top of the implementation file.
    writeExtraHeaders(C);

    if (_dllExport.size())
    {
        C << "\n#ifndef " << _dllExport << "_EXPORTS";
        C << "\n#   define " << _dllExport << "_EXPORTS";
        C << "\n#endif";
    }

    C << "\n#define ICE_BUILDING_GENERATED_CODE";
    C << sp;
    C << "\n#include \"";
    if (_include.size())
    {
        C << _include << '/';
    }
    C << _base << "." << _headerExtension << "\"";

    H << "\n#include <Ice/PushDisableWarnings.h>";

    if (!dc->hasMetadata("cpp:no-default-include"))
    {
        H << "\n#include <Ice/Ice.h>";
    }

    for (const string& includeFile : p->includeFiles())
    {
        string extension = getHeaderExt(includeFile, p);
        if (extension.empty())
        {
            extension = _headerExtension;
        }
        if (isAbsolutePath(includeFile))
        {
            // This means mcpp found the .ice file in its -I paths. So we generate an angled include for the C++ header.
            H << "\n#include <" << changeInclude(includeFile, _includePaths) << "." << extension << ">";
        }
        else
        {
            H << "\n#include \"" << removeExtension(includeFile) << "." << extension << "\"";
        }
    }

    // Emit #include statements for any 'cpp:include' metadata directives in the top-level Slice file.
    for (const auto& metadata : dc->getMetadata())
    {
        string_view directive = metadata->directive();
        string_view arguments = metadata->arguments();

        if (directive == "cpp:include")
        {
            H << nl << "#include <" << arguments << ">";
        }
        else if (directive == "cpp:source-include")
        {
            C << nl << "#include <" << arguments << ">";
        }
    }

    // Include Ice.h since it was not included in the header.
    if (dc->hasMetadata("cpp:no-default-include"))
    {
        C << "\n#include <Ice/Ice.h>";
    }

    // For simplicity, we include these extra headers all the time.
    C << "\n#include <Ice/AsyncResponseHandler.h>"; // for async dispatches
    C << "\n#include <Ice/DefaultSliceLoader.h>";   // for class and exception unmarshaling
    C << "\n#include <Ice/OutgoingAsync.h>";        // for proxies
    C << "\n#include <algorithm>";                  // for the dispatch implementation
    C << "\n#include <array>";                      // for the dispatch implementation

    // Disable shadow and deprecation warnings in .cpp file
    C << sp;
    C.zeroIndent();
    C << nl << "#if defined(_MSC_VER)";
    C << nl << "#   pragma warning(disable : 4458) // declaration of ... hides class member";
    C << nl << "#   pragma warning(disable : 4996) // ... was declared deprecated";
    C << nl << "#elif defined(__clang__)";
    C << nl << "#   pragma clang diagnostic ignored \"-Wshadow\"";
    C << nl << "#   pragma clang diagnostic ignored \"-Wdeprecated-declarations\"";
    C << nl << "#elif defined(__GNUC__)";
    C << nl << "#   pragma GCC diagnostic ignored \"-Wshadow\"";
    C << nl << "#   pragma GCC diagnostic ignored \"-Wdeprecated-declarations\"";
    C << nl << "#endif";

    printVersionCheck(H);
    printVersionCheck(C);

    printDllExportStuff(H, _dllExport);
    if (_dllExport.size())
    {
        _dllExport += " ";
    }

    H << sp;
    H << nl << "// NOLINTBEGIN(modernize-concat-nested-namespaces)";

    {
        ForwardDeclVisitor forwardDeclVisitor(H, C, _dllExport);
        p->visit(&forwardDeclVisitor);

        SliceLoaderVisitor sliceLoaderVisitor(C);
        p->visit(&sliceLoaderVisitor);

        ProxyVisitor proxyVisitor(H, C, _dllExport);
        p->visit(&proxyVisitor);

        DataDefVisitor dataDefVisitor(H, C, _dllExport);
        p->visit(&dataDefVisitor);

        InterfaceVisitor interfaceVisitor(H, C, _dllExport);
        p->visit(&interfaceVisitor);

        if (!dc->hasMetadata("cpp:no-stream"))
        {
            StreamVisitor streamVisitor(H);
            p->visit(&streamVisitor);
        }
    }
}

void
Slice::Gen::writeExtraHeaders(IceInternal::Output& out)
{
    for (string header : _extraHeaders)
    {
        string guard;
        string::size_type pos = header.rfind(',');
        if (pos != string::npos)
        {
            guard = header.substr(pos + 1);
            header = header.substr(0, pos);
        }
        if (!guard.empty())
        {
            out << "\n#ifndef " << guard;
            out << "\n#define " << guard;
        }
        out << "\n#include <" << header << '>';
        if (!guard.empty())
        {
            out << "\n#endif";
        }
        out << sp;
    }
}

TypeContext
Slice::Gen::setUseWstring(const ContainedPtr& p, list<TypeContext>& hist, TypeContext typeCtx)
{
    hist.push_back(typeCtx);
    if (auto argument = p->getMetadataArgs("cpp:type"))
    {
        if (argument == "wstring")
        {
            typeCtx = TypeContext::UseWstring;
        }
        else if (argument == "string")
        {
            typeCtx = TypeContext::None;
        }
    }
    return typeCtx;
}

TypeContext
Slice::Gen::resetUseWstring(list<TypeContext>& hist)
{
    TypeContext use = hist.back();
    hist.pop_back();
    return use;
}

string
Slice::Gen::getHeaderExt(string_view file, const UnitPtr& ut)
{
    DefinitionContextPtr dc = ut->findDefinitionContext(file);
    assert(dc);
    return dc->getMetadataArgs("cpp:header-ext").value_or("");
}

string
Slice::Gen::getSourceExt(string_view file, const UnitPtr& ut)
{
    DefinitionContextPtr dc = ut->findDefinitionContext(file);
    assert(dc);
    return dc->getMetadataArgs("cpp:source-ext").value_or("");
}

Slice::Gen::ForwardDeclVisitor::ForwardDeclVisitor(Output& h, Output& c, string dllExport)
    : H(h),
      C(c),
      _dllExport(std::move(dllExport))
{
}

bool
Slice::Gen::ForwardDeclVisitor::visitModuleStart(const ModulePtr& p)
{
    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);
    H << sp;
    writeDocSummary(H, p);
    H << nl << "namespace " << p->mappedName() << nl << '{';
    H.inc();
    return true;
}

void
Slice::Gen::ForwardDeclVisitor::visitModuleEnd(const ModulePtr&)
{
    H.dec();
    H << nl << '}';
    _useWstring = resetUseWstring(_useWstringHist);
}

void
Slice::Gen::ForwardDeclVisitor::visitClassDecl(const ClassDeclPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    const string name = p->mappedName();
    H << nl << "class " << name << ';';

    H << sp;
    H << nl << "/// A shared pointer to " << getArticleFor(name) << ' ' << name << ".";
    H << nl << "using " << name << "Ptr " << getDeprecatedAttribute(p) << "= std::shared_ptr<" << name << ">;";
}

bool
Slice::Gen::ForwardDeclVisitor::visitStructStart(const StructPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    H << nl << "struct " << getDeprecatedAttribute(p) << p->mappedName() << ';';
    return false;
}

void
Slice::Gen::ForwardDeclVisitor::visitInterfaceDecl(const InterfaceDeclPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    H << nl << "class " << getDeprecatedAttribute(p) << p->mappedName() << "Prx;";
}

void
Slice::Gen::ForwardDeclVisitor::visitEnum(const EnumPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    string mappedName = p->mappedName();

    writeDocSummary(H, p);
    H << nl << "enum class " << getDeprecatedAttribute(p) << mappedName;
    H << " : std::" << (p->maxValue() <= numeric_limits<uint8_t>::max() ? "uint8_t" : "int32_t");

    if (p->maxValue() > numeric_limits<uint8_t>::max() && p->maxValue() <= numeric_limits<int16_t>::max())
    {
        H << " // NOLINT(performance-enum-size)";
    }
    H << sb;

    // Check if any of the enumerators were assigned an explicit value.
    EnumeratorList enumerators = p->enumerators();
    const bool hasExplicitValues = p->hasExplicitValues();
    for (const auto& enumerator : enumerators)
    {
        if (!isFirstElement(enumerator))
        {
            H << ",";
            H << sp;
        }
        writeDocSummary(H, enumerator);
        H << nl << enumerator->mappedName();

        string deprecatedAttribute = getDeprecatedAttribute(enumerator);
        if (!deprecatedAttribute.empty())
        {
            // The string returned by `deprecatedAttribute` has a trailing space character,
            // here we need to remove it, and instead add it to the front.
            deprecatedAttribute.pop_back();
            H << ' ' << deprecatedAttribute;
        }

        // If any of the enumerators were assigned an explicit value, we emit
        // an explicit value for *all* enumerators.
        if (hasExplicitValues)
        {
            H << " = " << enumerator->value();
        }
    }
    H << eb << ';';

    H << sp;
    H << nl << "/// Outputs the enumerator name or underlying value of " << getArticleFor(mappedName) << ' '
      << mappedName << " to a stream.";
    H << nl << "/// @param os The output stream.";
    H << nl << "/// @param value The value to output.";
    H << nl << "/// @return The output stream.";
    H << nl << _dllExport << "std::ostream& operator<<(std::ostream& os, " << mappedName << " value);";

    if (!p->hasMetadata("cpp:custom-print"))
    {
        // We generate the implementation unless custom-print tells us not to.
        // If the provided value corresponds to a named enumerator value, we print the corresponding name.
        // Otherwise, we print the underlying integer value.
        C << sp << nl << "std::ostream&";
        C << nl << p->mappedScope("::") << "operator<<(std::ostream& os, " << mappedName << " value)";
        C << sb;
        C << nl << "switch (value)";
        C << sb;
        for (const auto& enumerator : enumerators)
        {
            const string enumeratorName = enumerator->mappedName();
            C << nl << "case " << mappedName << "::" << enumeratorName << ":";
            C.inc();
            C << nl << "return os << \"" + enumeratorName + "\";";
            C.dec();
        }
        C << nl << "default:";
        C.inc();
        C << nl << "return os << static_cast<std::int32_t>(value);";
        C.dec();
        C << eb;
        C << eb;
    }
}

void
Slice::Gen::ForwardDeclVisitor::visitSequence(const SequencePtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    const string name = p->mappedName();
    const string scope = p->mappedScope("::", true);
    const TypePtr type = p->type();
    const TypeContext typeCtx = _useWstring;

    string seqType = findMetadata(p->getMetadata(), _useWstring);

    writeDocSummary(H, p);
    string deprecatedAttribute = getDeprecatedAttribute(p);
    H << nl << "using " << name << " " << deprecatedAttribute << "= ";

    if (!seqType.empty())
    {
        H << seqType << ';';
    }
    else
    {
        auto builtin = dynamic_pointer_cast<Builtin>(type);
        if (builtin && builtin->kind() == Builtin::KindByte)
        {
            H << "std::vector<std::byte>;";
        }
        else
        {
            string s = typeToString(type, false, scope, p->typeMetadata(), typeCtx);
            H << "std::vector<" << s << ">;";
        }
    }
}

void
Slice::Gen::ForwardDeclVisitor::visitDictionary(const DictionaryPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    const string name = p->mappedName();
    const string scope = p->mappedScope("::", true);
    const string dictType = findMetadata(p->getMetadata());
    const TypeContext typeCtx = _useWstring;

    writeDocSummary(H, p);
    string deprecatedAttribute = getDeprecatedAttribute(p);
    H << nl << "using " << name << " " << deprecatedAttribute << "= ";

    if (dictType.empty())
    {
        // A default std::map dictionary
        TypePtr keyType = p->keyType();
        TypePtr valueType = p->valueType();
        string ks = typeToString(keyType, false, scope, p->keyMetadata(), typeCtx);
        string vs = typeToString(valueType, false, scope, p->valueMetadata(), typeCtx);

        H << "std::map<" << ks << ", " << vs << ">;";
    }
    else
    {
        // A custom dictionary
        H << dictType << ';';
    }
}

void
Slice::Gen::ForwardDeclVisitor::visitConst(const ConstPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    const string name = p->mappedName();
    const string scope = p->mappedScope("::", true);
    writeDocSummary(H, p);
    H << nl << (isConstexprType(p->type()) ? "constexpr " : "const ")
      << typeToString(p->type(), false, scope, p->typeMetadata(), _useWstring) << " " << name << " "
      << getDeprecatedAttribute(p) << "= ";
    writeConstantValue(H, p->type(), p->valueType(), p->value(), _useWstring, p->typeMetadata(), scope);
    H << ';';
    if (!isConstexprType(p->type())) // i.e. string or wstring
    {
        // The string/wstring constructor can throw, which produces a clang-tidy lint for const or static objects.
        H << " // NOLINT(cert-err58-cpp, modernize-raw-string-literal)";
    }
}

Slice::Gen::SliceLoaderVisitor::SliceLoaderVisitor(Output& c) : C(c) {}

bool
Slice::Gen::SliceLoaderVisitor::visitUnitStart(const UnitPtr& unit)
{
    if (unit->contains<ClassDef>() || unit->contains<Exception>())
    {
        C << sp << nl << "namespace" << nl << "{";
        C.inc();
        return true;
    }
    else
    {
        return false;
    }
}

void
Slice::Gen::SliceLoaderVisitor::visitUnitEnd(const UnitPtr&)
{
    C.dec();
    C << nl << "}";
}

bool
Slice::Gen::SliceLoaderVisitor::visitClassDefStart(const ClassDefPtr& p)
{
    const string scopedName = p->mappedScoped("::", true);
    const string flatScopedName = p->mappedScoped("_", true);

    C << nl << "const IceInternal::ClassInit<" << scopedName << "> iceC" << flatScopedName << "_init";
    if (p->compactId() != -1)
    {
        C << '{' << p->compactId() << '}';
    }
    C << ';';

    return false;
}

bool
Slice::Gen::SliceLoaderVisitor::visitExceptionStart(const ExceptionPtr& p)
{
    const string scopedName = p->mappedScoped("::", true);
    const string flatScopedName = p->mappedScoped("_", true);

    C << nl << "const IceInternal::ExceptionInit<" << scopedName << "> iceC" << flatScopedName << "_init;";
    return false;
}

Slice::Gen::ProxyVisitor::ProxyVisitor(Output& h, Output& c, string dllExport)
    : H(h),
      C(c),
      _dllExport(std::move(dllExport))
{
}

bool
Slice::Gen::ProxyVisitor::visitModuleStart(const ModulePtr& p)
{
    if (!p->contains<InterfaceDef>())
    {
        return false;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);

    H << sp << nl << "namespace " << p->mappedName() << nl << '{';
    H.inc();
    return true;
}

void
Slice::Gen::ProxyVisitor::visitModuleEnd(const ModulePtr&)
{
    H.dec();
    H << nl << '}';

    _useWstring = resetUseWstring(_useWstringHist);
}

bool
Slice::Gen::ProxyVisitor::visitInterfaceDefStart(const InterfaceDefPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);

    const string scope = p->mappedScope("::", true);
    const string prx = p->mappedName() + "Prx";
    const string scopedPrx = p->mappedScoped("::") + "Prx";
    const InterfaceList bases = p->bases();

    writeDocSummary(H, p, {.includeHeaderFile = true});
    H << nl << "class " << _dllExport << getDeprecatedAttribute(p) << prx << " : public Ice::Proxy";
    H.spar("<");
    H << prx;
    if (bases.empty())
    {
        H << "Ice::ObjectPrx";
    }
    else
    {
        for (const auto& base : bases)
        {
            H << getUnqualified(base->mappedScoped("::", true) + "Prx", scope);
        }
    }
    H.epar(">");

    H << sb;
    H.dec();
    H << nl << "public:";
    H.inc();

    if (!bases.empty())
    {
        // -Wextra wants to initialize all the virtual base classes _in the right order_, which is not practical, and
        // is useless here.
        H.zeroIndent();
        H << nl << "#if defined(__GNUC__) && !defined(__clang__)";
        H << nl << "#   pragma GCC diagnostic push";
        H << nl << "#   pragma GCC diagnostic ignored \"-Wextra\" // initialize all virtual bases in correct order";
        H << nl << "#endif";
        H.restoreIndent();
        H << sp;
    }

    // We can't use "= default" for the copy/move ctor/assignment operator as it's not correct with virtual inheritance.
    H << nl << "/// Constructs a proxy from a Communicator and a proxy string.";
    H << nl << "/// @param communicator The communicator of the new proxy.";
    H << nl << "/// @param proxyString The proxy string to parse.";
    H << nl << prx << "(const Ice::CommunicatorPtr& communicator, std::string_view proxyString)"
      << " : Ice::ObjectPrx{communicator, proxyString} {} // NOLINT(modernize-use-equals-default)";

    H << sp;
    H << nl << "/// Copy constructor. Constructs with a copy of the contents of @p other.";
    H << nl << "/// @param other The proxy to copy from.";
    H << nl << prx << "(const " << prx << "& other) noexcept : Ice::ObjectPrx{other}";
    H << " {} // NOLINT(modernize-use-equals-default)";

    H << sp;
    H << nl << "/// Move constructor. Constructs a proxy with the contents of @p other using move semantics.";
    H << nl << "/// @param other The proxy to move from.";
    H << nl << prx << "(" << prx << "&& other) noexcept : Ice::ObjectPrx{std::move(other)}"
      << " {} // NOLINT(modernize-use-equals-default)";

    H << sp;
    H << nl << "~" << prx << "() override;";
    C << sp;
    C << nl << scopedPrx << "::~" << prx << "() = default;"; // avoid weak table

    H << sp;
    H << nl
      << "/// Copy assignment operator. Replaces the contents of this proxy with a copy of the contents of @p rhs.";
    H << nl << "/// @param rhs The proxy to copy from.";
    H << nl << "/// @return A reference to this proxy.";
    H << nl << prx << "& operator=(const " << prx << "& rhs) noexcept";
    H << sb;
    // The self-assignment check is to make clang-tidy happy.
    H << nl << "if (this != &rhs)";
    H << sb;
    H << nl << "Ice::ObjectPrx::operator=(rhs);";
    H << eb;
    H << nl << "return *this;";
    H << eb;

    H << sp;
    H << nl
      << "/// Move assignment operator. Replaces the contents of this proxy with the contents of @p rhs using move "
         "semantics.";
    H << nl << "/// @param rhs The proxy to move from.";
    H << nl << prx << "& operator=(" << prx << "&& rhs) noexcept";
    H << sb;
    // The self-assignment check is to make clang-tidy happy.
    H << nl << "if (this != &rhs)";
    H << sb;
    H << nl << "Ice::ObjectPrx::operator=(std::move(rhs));";
    H << eb;
    H << nl << "return *this;";
    H << eb;

    return true;
}

void
Slice::Gen::ProxyVisitor::visitInterfaceDefEnd(const InterfaceDefPtr& p)
{
    const string prx = p->mappedName() + "Prx";
    const string scopedPrx = p->mappedScoped("::") + "Prx";
    const InterfaceList bases = p->allBases();

    H << sp;
    H << nl << "/// Gets the type ID of the associated Slice interface.";
    H << nl << "/// @return The string `\"" << p->scoped() << "\"`.";
    H << nl << "static const char* ice_staticId() noexcept;";

    C << sp;
    C << nl << "const char*" << nl << scopedPrx << "::ice_staticId() noexcept";
    C << sb;
    C << nl << "return \"" << p->scoped() << "\";";
    C << eb;

    H << sp;
    H << nl << "/// @private";
    H << nl << "static " << prx << " _fromReference(IceInternal::ReferencePtr ref) { return " << prx
      << "{std::move(ref)}; }";
    H.dec();

    H << sp << nl << "protected:";
    H.inc();
    H << nl << "/// @private";
    H << nl << prx << "() = default;";
    H << sp;
    H << nl << "/// @private";
    H << nl << "explicit " << prx << "(IceInternal::ReferencePtr&& ref) : Ice::ObjectPrx{std::move(ref)}";
    H << sb << eb;

    if (!bases.empty())
    {
        // -Wextra wants to initialize all the virtual base classes _in the right order_, which is not practical, and
        // is useless here.
        H << sp;
        H.zeroIndent();
        H << nl << "#if defined(__GNUC__) && !defined(__clang__)";
        H << nl << "#   pragma GCC diagnostic pop";
        H << nl << "#endif";
        H.restoreIndent();
    }

    H << eb << ';';

    _useWstring = resetUseWstring(_useWstringHist);
}

void
Slice::Gen::ProxyVisitor::visitOperation(const OperationPtr& p)
{
    const InterfaceDefPtr container = p->interface();
    const string opName = p->mappedName();
    const string interfaceScope = container->mappedScope("::", true);
    const string interfaceName = container->mappedName();

    const string scopedPrxPrefix = (interfaceScope + interfaceName + "Prx::").substr(2);
    const string prxScopedOpName = scopedPrxPrefix + opName;
    const string scopedPrxFutureImplPrefix = interfaceName + "Prx::_iceI_";
    const string prxFutureImplScopedOpName = scopedPrxFutureImplPrefix + opName;

    const TypePtr ret = p->returnType();
    const bool retIsOpt = p->returnIsOptional();
    const string retS = ret ? typeToString(ret, retIsOpt, interfaceScope, p->getMetadata(), _useWstring) : "void";
    const string retSImpl = ret ? typeToString(ret, retIsOpt, "", p->getMetadata(), _useWstring) : "void";

    // All parameters
    vector<string> paramsDecl;
    vector<string> paramsImplDecl;

    // Only the parameters marshaled into the request.
    vector<string> inParamsDecl;
    vector<string> inParamsImplDecl;
    vector<string> inParamsImpl;

    vector<string> futureOutParams = createOutgoingAsyncParams(p, interfaceScope, _useWstring);
    vector<string> lambdaOutParams =
        createOutgoingAsyncParams(p, interfaceScope, _useWstring | TypeContext::UnmarshalParamZeroCopy);

    const string futureImplPrefix = "_iceI_";
    const string lambdaImplPrefix = futureOutParams == lambdaOutParams ? "_iceI_" : "_iceIL_";

    ParameterList paramList = p->parameters();
    ParameterList inParams = p->inParameters();
    ParameterList outParams = p->outParameters();

    for (const auto& q : paramList)
    {
        const string paramName = q->mappedName();
        const string prefixedParamName = paramPrefix + paramName;
        const MetadataList& metadata = q->getMetadata();

        if (q->isOutParam())
        {
            string outputTypeString =
                outputTypeToString(q->type(), q->optional(), interfaceScope, metadata, _useWstring);

            paramsDecl.push_back(outputTypeString + ' ' + paramName);
            paramsImplDecl.push_back(outputTypeString + ' ' + prefixedParamName);
        }
        else
        {
            string typeString = inputTypeToString(q->type(), q->optional(), interfaceScope, metadata, _useWstring);

            paramsDecl.push_back(typeString + ' ' + paramName);
            paramsImplDecl.push_back(typeString + ' ' + prefixedParamName);

            inParamsDecl.push_back(typeString + ' ' + paramName);
            inParamsImplDecl.push_back(typeString + ' ' + prefixedParamName);
            inParamsImpl.push_back(prefixedParamName);
        }
    }

    const string contextParam = escapeParam(paramList, "context");
    const string contextDef = "const Ice::Context& " + contextParam;
    const string contextDecl = contextDef + " = Ice::noExplicitContext";

    string futureT = createOutgoingAsyncTypeParam(futureOutParams);
    string futureTAbsolute = createOutgoingAsyncTypeParam(createOutgoingAsyncParams(p, "", _useWstring));
    string lambdaT = createOutgoingAsyncTypeParam(lambdaOutParams);

    const string deprecatedAttribute = getDeprecatedAttribute(p);

    const optional<DocComment>& comment = p->docComment();
    const string contextDoc = "@param " + contextParam + " The request context.";

    H << sp;

    //
    // Synchronous operation
    //
    if (comment)
    {
        StringList postParams;
        postParams.push_back(contextDoc);
        writeOpDocSummary(H, p, *comment, OpDocAllParams, true, {}, StringList{}, postParams, comment->returns());
    }
    H << nl << deprecatedAttribute << retS << ' ' << opName << spar << paramsDecl << contextDecl << epar << " const;";

    // We don't want to add [[nodiscard]] to proxy member functions.
    if (ret && p->outParameters().empty())
    {
        H << " // NOLINT(modernize-use-nodiscard)";
    }

    C << sp;
    C << nl << retSImpl << nl << prxScopedOpName << spar << paramsImplDecl << "const Ice::Context& context" << epar
      << " const";
    C << sb;
    C << nl;
    if (futureOutParams.size() == 1)
    {
        if (ret)
        {
            C << "return ";
        }
        else
        {
            C << paramPrefix << (*outParams.begin())->mappedName() << " = ";
        }
    }
    else if (futureOutParams.size() > 1)
    {
        C << "auto result = ";
    }

    // We call makePromiseOutgoing with the "sync" parameter set to true; the Promise/future implementation later on
    // calls makePromiseOutgoing with this parameter set to false. This parameter is useful for collocated calls.
    C << "IceInternal::makePromiseOutgoing<" << futureT << ">";
    C << spar << "true, this" << "&" + prxFutureImplScopedOpName;
    C << inParamsImpl;
    C << "context" << epar << ".get();";
    if (futureOutParams.size() > 1)
    {
        int index = ret ? 1 : 0;
        for (const auto& q : outParams)
        {
            C << nl << paramPrefix << q->mappedName() << " = ";
            C << condMove(isMovable(q->type()), "std::get<" + std::to_string(index++) + ">(result)") << ";";
        }
        if (ret)
        {
            C << nl << "return " + condMove(isMovable(ret), "std::get<0>(result)") + ";";
        }
    }
    C << eb;

    //
    // Promise-based asynchronous operation
    //
    H << sp;
    if (comment)
    {
        StringList postParamsDoc;
        postParamsDoc.push_back(contextDoc);

        StringList futureDoc;
        if (futureOutParams.empty())
        {
            futureDoc.emplace_back("A future that becomes available when the invocation completes.");
        }
        else
        {
            futureDoc.emplace_back("A future that becomes available when the invocation completes. This future holds:");
            futureDoc.splice(futureDoc.end(), createOpOutParamsDoc(p, *comment));
        }

        writeOpDocSummary(H, p, *comment, OpDocInParams, false, {}, StringList{}, postParamsDoc, futureDoc);
    }

    H << nl << deprecatedAttribute << "[[nodiscard]] std::future<" << futureT << "> " << opName << "Async" << spar
      << inParamsDecl << contextDecl << epar << " const;";

    C << sp;
    C << nl << "std::future<" << futureTAbsolute << ">";
    C << nl;
    C << prxScopedOpName << "Async" << spar << inParamsImplDecl << "const Ice::Context& context" << epar << " const";

    C << sb;
    C << nl << "return IceInternal::makePromiseOutgoing<" << futureT << ">" << spar;
    C << "false, this" << string("&" + prxFutureImplScopedOpName);
    C << inParamsImpl;
    C << "context" << epar << ";";
    C << eb;

    //
    // Lambda based asynchronous operation
    //
    const string responseParam = escapeParam(inParams, "response");
    const string exceptionParam = escapeParam(inParams, "exception");
    const string sentParam = escapeParam(inParams, "sent");
    const string lambdaResponse = createLambdaResponse(p, _useWstring | TypeContext::UnmarshalParamZeroCopy);

    H << sp;
    if (comment)
    {
        StringList postParams;
        if (!lambdaOutParams.empty())
        {
            postParams.push_back("@param " + responseParam + " The response callback. It accepts:");
            postParams.splice(postParams.end(), createOpOutParamsDoc(p, *comment));
        }
        else
        {
            postParams.push_back("@param " + responseParam + " The response callback.");
        }

        postParams.push_back("@param " + exceptionParam + " The exception callback.");
        postParams.push_back("@param " + sentParam + " The sent callback.");
        postParams.push_back(contextDoc);

        StringList returns;
        returns.emplace_back("A function that can be called to cancel the invocation locally.");
        writeOpDocSummary(H, p, *comment, OpDocInParams, false, {}, StringList{}, postParams, returns);
    }
    H << nl << "// NOLINTNEXTLINE(modernize-use-nodiscard)";
    H << nl << deprecatedAttribute << "std::function<void()> " << opName << "Async" << spar;
    H.useCurrentPosAsIndent();
    H << inParamsDecl;

    H << lambdaResponse + " " + responseParam;
    H << "std::function<void(std::exception_ptr)> " + exceptionParam + " = nullptr";
    H << "std::function<void(bool)> " + sentParam + " = nullptr";
    H << contextDecl << epar << " const;";
    H.restoreIndent();

    C << sp;
    C << nl << "std::function<void()>";
    C << nl << prxScopedOpName << "Async" << spar;
    C.useCurrentPosAsIndent();
    C << inParamsImplDecl;
    C << lambdaResponse + " response";
    C << "std::function<void(std::exception_ptr)> exception";
    C << "std::function<void(bool)> sent";
    C << "const Ice::Context& context" << epar << " const";
    C.restoreIndent();

    C << sb;
    if (lambdaOutParams.size() > 1)
    {
        C << nl << "auto responseCb = [response = std::move(response)](" << lambdaT << "&& result) mutable";
        C << sb;
        C << nl << "std::apply(std::move(response), std::move(result));";
        C << eb << ";";
    }

    C << nl << "return IceInternal::makeLambdaOutgoing<" << lambdaT << ">" << spar;

    C << "std::move(" + (lambdaOutParams.size() > 1 ? string("responseCb") : "response") + ")" << "std::move(exception)"
      << "std::move(sent)" << "this";
    C << string("&" + getUnqualified(scopedPrxPrefix, interfaceScope.substr(2)) + lambdaImplPrefix + opName);
    C << inParamsImpl;
    C << "context" << epar << ";";
    C << eb;

    // Implementation

    emitOperationImpl(p, futureImplPrefix, futureOutParams);

    if (lambdaImplPrefix != futureImplPrefix)
    {
        emitOperationImpl(p, lambdaImplPrefix, lambdaOutParams);
    }
}

void
Slice::Gen::ProxyVisitor::emitOperationImpl(
    const OperationPtr& p,
    const string& prefix,
    const std::vector<std::string>& outgoingAsyncParams)
{
    const InterfaceDefPtr container = p->interface();
    const string opName = p->mappedName();
    const string opImplName = prefix + opName;
    const string interfaceScope = container->mappedScope("::", true);
    const string scopedPrxPrefix = container->mappedScoped("::") + "Prx" + "::";

    const TypePtr ret = p->returnType();

    vector<string> inParamsS;
    vector<string> inParamsImplDecl;

    ParameterList inParams = p->inParameters();
    ParameterList outParams = p->outParameters();

    bool outParamsHasOpt = false;

    if (ret)
    {
        outParamsHasOpt |= p->returnIsOptional();
    }
    outParamsHasOpt |=
        std::any_of(outParams.begin(), outParams.end(), [](const ParameterPtr& q) { return q->optional(); });

    for (const auto& q : inParams)
    {
        string typeString = inputTypeToString(q->type(), q->optional(), interfaceScope, q->getMetadata(), _useWstring);

        inParamsS.push_back(typeString);
        inParamsImplDecl.push_back(typeString + ' ' + paramPrefix + q->mappedName());
    }

    string returnT = createOutgoingAsyncTypeParam(outgoingAsyncParams);

    H << sp;
    H << nl << "/// @private";
    H << nl << "void " << opImplName << spar;
    H << "const std::shared_ptr<IceInternal::OutgoingAsyncT<" + returnT + ">>&";
    H << inParamsS;
    H << "const Ice::Context&";
    H << epar << " const;";

    C << sp;
    C << nl << "void" << nl << scopedPrxPrefix << opImplName << spar;
    C << "const std::shared_ptr<IceInternal::OutgoingAsyncT<" + returnT + ">>& outAsync";
    C << inParamsImplDecl << "const Ice::Context& context";
    C << epar << " const";
    C << sb;
    C << nl << "static constexpr std::string_view operationName = \"" << p->name() << "\";";
    C << sp;
    if (p->returnsData())
    {
        C << nl << "_checkTwowayOnly(operationName);";
    }
    C << nl << "outAsync->invoke(";
    C.inc();
    C << nl << "operationName,";
    C << nl << operationModeToString(p->mode()) << ",";
    C << nl << opFormatTypeToString(p) << ",";
    C << nl << "context,";
    C << nl;
    writeInParamsLambda(C, p, inParams, interfaceScope);
    C << "," << nl;
    throwUserExceptionLambda(C, p->throws(), interfaceScope);

    if (outgoingAsyncParams.size() > 1)
    {
        // Generate a read method if there are more than one ret/out parameter. If there's
        // only one, we rely on the default read method from LambdaOutgoing
        // except if the unique ret/out is optional or is an array.
        C << "," << nl << "[](Ice::InputStream* istr)";
        C << sb;
        C << nl << returnT << " v;";

        writeUnmarshalCode(C, outParams, p);

        if (p->returnsClasses())
        {
            C << nl << "istr->readPendingValues();";
        }
        C << nl << "return v;";
        C << eb;
    }
    else if (outParamsHasOpt || p->returnsClasses())
    {
        // If there's only one optional ret/out parameter, we still need to generate
        // a read method, we can't rely on the default read method which wouldn't
        // known which tag to use.
        C << "," << nl << "[](Ice::InputStream* istr)";
        C << sb;

        writeAllocateCode(C, outParams, p, interfaceScope, _useWstring);
        writeUnmarshalCode(C, outParams, p);

        if (p->returnsClasses())
        {
            C << nl << "istr->readPendingValues();";
        }

        if (ret)
        {
            C << nl << "return ret;";
        }
        else
        {
            C << nl << "return " << paramPrefix + outParams.front()->mappedName() << ";";
        }
        C << eb;
    }

    C.dec();
    C << ");" << eb;
}

Slice::Gen::DataDefVisitor::DataDefVisitor(IceInternal::Output& h, IceInternal::Output& c, string dllExport)
    : H(h),
      C(c),
      _dllExport(std::move(dllExport))
{
}

bool
Slice::Gen::DataDefVisitor::visitModuleStart(const ModulePtr& p)
{
    if (!p->contains<Struct>() && !p->contains<ClassDef>() && !p->contains<Exception>())
    {
        return false;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);
    H << sp << nl << "namespace " << p->mappedName() << nl << '{';
    H.inc();
    return true;
}

void
Slice::Gen::DataDefVisitor::visitModuleEnd(const ModulePtr& p)
{
    if (p->contains<Struct>())
    {
        // Bring in relational operators for structs.
        // Don't show it in the generated doxygen doc as it's more confusing than helpful.
        H << sp;
        H << nl << "/// @cond INTERNAL";
        H << nl << "using Ice::Tuple::operator<;";
        H << nl << "using Ice::Tuple::operator<=;";
        H << nl << "using Ice::Tuple::operator>;";
        H << nl << "using Ice::Tuple::operator>=;";
        H << nl << "using Ice::Tuple::operator==;";
        H << nl << "using Ice::Tuple::operator!=;";
        H << nl << "/// @endcond";
    }

    H.dec();
    H << nl << '}';
    _useWstring = resetUseWstring(_useWstringHist);
}

bool
Slice::Gen::DataDefVisitor::visitStructStart(const StructPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);

    writeDocSummary(H, p, {.includeHeaderFile = true});
    H << nl << "struct " << getDeprecatedAttribute(p) << p->mappedName();
    H << sb;

    return true;
}

void
Slice::Gen::DataDefVisitor::visitStructEnd(const StructPtr& p)
{
    H << sp;
    H << nl << "/// Creates a tuple with all the fields of this struct.";
    H << nl << "/// @return A tuple with all the fields of this struct.";
    writeIceTuple(H, p->dataMembers(), _useWstring);

    H << sp;
    H << nl << "/// Outputs the name and value of each field of this instance to the stream.";
    H << nl << "/// @param os The output stream.";
    H << nl << _dllExport << "void ice_printFields(std::ostream& os) const;";
    H << eb << ';';

    const string scoped = p->mappedScoped("::");
    const string scope = p->mappedScope("::");
    const string name = p->mappedName();

    C << sp << nl << "void";
    C << nl << scoped << "::ice_printFields(std::ostream& os) const";
    C << sb;
    printFields(p->dataMembers(), true);
    C << eb;

    H << sp;
    H << nl << "/// Outputs the description of " << getArticleFor(name) << ' ' << name
      << " to a stream, including all its fields.";
    H << nl << "/// @param os The output stream.";
    H << nl << "/// @param value The instance to output.";
    H << nl << "/// @return The output stream.";
    H << nl << _dllExport << "std::ostream& operator<<(std::ostream& os, const " << name << "& value);";

    if (!p->hasMetadata("cpp:custom-print"))
    {
        // We generate the implementation unless custom-print tells us not to.
        C << sp << nl << "std::ostream&";
        C << nl << scope << "operator<<(std::ostream& os, const ::" << scoped << "& value)";
        C << sb;
        C << sp << nl << "os << \"" << scoped << "{\";";
        C << nl << "value.ice_printFields(os);";
        C << nl << "os << '}';";
        C << nl << "return os;";
        C << eb;
    }

    _useWstring = resetUseWstring(_useWstringHist);
}

void
Slice::Gen::DataDefVisitor::visitDataMember(const DataMemberPtr& p)
{
    auto container = p->container();
    if (dynamic_pointer_cast<Struct>(container) || dynamic_pointer_cast<Exception>(container))
    {
        emitDataMember(p);
    }
    // else don't do anything - visitClassXxx already calls emitDataMember explicitly.
}

bool
Slice::Gen::DataDefVisitor::visitExceptionStart(const ExceptionPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);

    const string name = p->mappedName();
    const string scope = p->mappedScope("::", true);
    const string scoped = p->mappedScoped("::");
    const ExceptionPtr base = p->base();
    const DataMemberList dataMembers = p->dataMembers();
    const DataMemberList allDataMembers = p->allDataMembers();
    DataMemberList baseDataMembers;

    vector<string> allParameters;
    map<string, StringList> allOverviews;

    for (const auto& dataMember : allDataMembers)
    {
        string typeName =
            typeToString(dataMember->type(), dataMember->optional(), scope, dataMember->getMetadata(), _useWstring);
        allParameters.push_back(typeName + " " + dataMember->mappedName());

        if (const auto& comment = dataMember->docComment())
        {
            allOverviews[dataMember->name()] = comment->overview();
        }
    }

    if (base)
    {
        baseDataMembers = base->allDataMembers();
    }

    const string baseClass = base ? getUnqualified(base->mappedScoped("::", true), scope) : "Ice::UserException";
    const string baseName = base ? base->mappedName() : "UserException";

    writeDocSummary(H, p, {.includeHeaderFile = true});
    H << nl << "class " << _dllExport << getDeprecatedAttribute(p) << name << " : public " << baseClass;
    H << sb;

    H.dec();
    H << nl << "public:";
    H.inc();

    if (!allDataMembers.empty())
    {
        if (base && dataMembers.empty())
        {
            // Reuse the base class constructors.
            H << nl << "using " << baseClass << "::" << baseName << ";";
        }
        else
        {
            H << nl << "/// Default constructor.";
            H << nl << name << "() noexcept = default;";

            H << sp;
            H << nl << "/// One-shot constructor to initialize all data members.";
            for (const auto& dataMember : allDataMembers)
            {
                auto r = allOverviews.find(dataMember->name());
                if (r != allOverviews.end())
                {
                    H << nl << "/// @param " << dataMember->mappedName() << " " << getFirstSentence(r->second);
                }
            }
            H << nl << name << "(";

            for (auto q = allParameters.begin(); q != allParameters.end(); ++q)
            {
                if (q != allParameters.begin())
                {
                    H << ", ";
                }
                H << (*q);
            }
            H << ") noexcept :";
            H.inc();
            if (base || !baseDataMembers.empty())
            {
                H << nl << baseClass << "(";

                for (auto q = baseDataMembers.begin(); q != baseDataMembers.end(); ++q)
                {
                    if (q != baseDataMembers.begin())
                    {
                        H << ", ";
                    }
                    string memberName = (*q)->mappedName();
                    TypePtr memberType = (*q)->type();
                    H << condMove(isMovable(memberType), memberName);
                }

                H << ")";
                if (!dataMembers.empty())
                {
                    H << ",";
                }
            }

            for (auto q = dataMembers.begin(); q != dataMembers.end(); ++q)
            {
                string memberName = (*q)->mappedName();
                TypePtr memberType = (*q)->type();

                if (q != dataMembers.begin())
                {
                    H << ",";
                }
                H << nl << memberName << "(" << condMove(isMovable(memberType), memberName) << ")";
            }

            H.dec();
            H << sb;
            H << eb;

            // We generate a noexcept copy constructor all the time. A C++ exception must have a noexcept copy
            // constructor but the default constructor is not always noexcept (e.g. if the exception has a string
            // field).
            H << sp;
            H << nl << "/// Copy constructor.";
            H << nl << name << "(const " << name << "&) noexcept = default;";
        }
        H << sp;
    }

    if (p->hasMetadata("cpp:custom-print") || p->hasMetadata("cpp:ice_print"))
    {
        H << sp;
        H << nl << "// Custom ice_print implemented by the application.";
        H << nl << "void ice_print(std::ostream& os) const override;";
    }

    if (!dataMembers.empty())
    {
        H << nl << "/// Creates a tuple with all the fields of this exception.";
        H << nl << "/// @return A tuple with all the fields of this exception.";
        writeIceTuple(H, p->allDataMembers(), _useWstring);

        H << sp << nl << "void ice_printFields(std::ostream& os) const override;";
        C << sp << nl << "void" << nl << scoped << "::ice_printFields(std::ostream& os) const";
        C << sb;
        bool firstField = true;
        if (base && !base->allDataMembers().empty())
        {
            C << nl << baseClass << "::ice_printFields(os);";
            firstField = false;
        }
        printFields(dataMembers, firstField);
        C << eb;
    }

    H << nl << "/// Gets the type ID of the associated Slice exception.";
    H << nl << "/// @return The string `\"" << p->scoped() << "\"`.";
    H << nl << "static const char* ice_staticId() noexcept;";

    C << sp << nl << "const char*" << nl << scoped << "::ice_staticId() noexcept";
    C << sb;
    C << nl << "return \"" << p->scoped() << "\";";
    C << eb;

    H << sp << nl << "[[nodiscard]] const char* ice_id() const noexcept override;";
    C << sp << nl << "const char*" << nl << scoped << "::ice_id() const noexcept";
    C << sb;
    C << nl << "return ice_staticId();";
    C << eb;

    H << sp << nl << "void ice_throw() const override;";
    C << sp << nl << "void" << nl << scoped << "::ice_throw() const";
    C << sb;
    C << nl << "throw *this;";
    C << eb;

    if (p->usesClasses() && !(base && base->usesClasses()))
    {
        H << sp;
        H << nl << "/// @private";
        H << nl << "[[nodiscard]] bool _usesClasses() const override;";

        C << sp;
        C << nl << "bool";
        C << nl << scoped << "::_usesClasses() const";
        C << sb;
        C << nl << "return true;";
        C << eb;
    }

    if (!dataMembers.empty())
    {
        H << sp;
    }
    return true;
}

void
Slice::Gen::DataDefVisitor::visitExceptionEnd(const ExceptionPtr& p)
{
    const string scope = p->mappedScope("::", true);
    const string scoped = p->mappedScoped("::");
    const DataMemberList dataMembers = p->dataMembers();

    const ExceptionPtr base = p->base();
    const string baseClass = base ? getUnqualified(base->mappedScoped("::", true), scope) : "Ice::UserException";

    H.dec();
    H << sp << nl << "protected:";
    H.inc();

    H << nl << "/// @private";
    H << nl << "void _writeImpl(Ice::OutputStream*) const override;";
    C << sp << nl << "void" << nl << scoped << "::_writeImpl(Ice::OutputStream* ostr) const";
    C << sb;
    // lastSlice is true or false.
    C << nl << "ostr->startSlice(ice_staticId(), -1, " << (base ? "false" : "true") << ");";
    if (!dataMembers.empty())
    {
        writeDataMembers(C, dataMembers);
    }
    C << nl << "ostr->endSlice();";
    if (base)
    {
        C << nl << baseClass << "::_writeImpl(ostr);";
    }
    C << eb;

    H << sp;
    H << nl << "/// @private";
    H << nl << "void _readImpl(Ice::InputStream*) override;";
    C << sp << nl << "void" << nl << scoped << "::_readImpl(Ice::InputStream* istr)";
    C << sb;
    C << nl << "istr->startSlice();";
    if (!dataMembers.empty())
    {
        readDataMembers(C, dataMembers);
    }
    C << nl << "istr->endSlice();";
    if (base)
    {
        C << nl << baseClass << "::_readImpl(istr);";
    }
    C << eb;

    H << eb << ';';

    _useWstring = resetUseWstring(_useWstringHist);
}

bool
Slice::Gen::DataDefVisitor::visitClassDefStart(const ClassDefPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);

    const string name = p->mappedName();
    const string scope = p->mappedScope("::", true);
    const string scoped = p->mappedScoped("::");
    const ClassDefPtr base = p->base();
    const DataMemberList dataMembers = p->dataMembers();
    const DataMemberList allDataMembers = p->allDataMembers();
    const string baseClass = base ? getUnqualified(base->mappedScoped("::", true), scope) : "Ice::Value";

    writeDocSummary(H, p, {.includeHeaderFile = true});
    H << nl << "class " << _dllExport << getDeprecatedAttribute(p) << name << " : public " << baseClass;
    H << sb;
    H.dec();
    H << nl << "public:";
    H.inc();

    if (base && dataMembers.empty())
    {
        H << "using " << getUnqualified(base->mappedScoped("::", true), scope) << "::" << base->mappedName() << ";";
    }
    else
    {
        // We always generate this default constructor because we always generate a protected copy constructor.
        H << nl << "/// Default constructor.";
        H << nl << name << "() noexcept = default;";

        if (!allDataMembers.empty())
        {
            emitOneShotConstructor(p);
        }
    }

    H << sp;
    H << nl << "/// Gets the type ID of the associated Slice class.";
    H << nl << "/// @return The string `\"" << p->scoped() << "\"`.";
    H << nl << "static const char* ice_staticId() noexcept;";
    C << sp;
    C << nl << "const char*" << nl << scoped << "::ice_staticId() noexcept";
    C << sb;
    C << nl << "return \"" << p->scoped() << "\";";
    C << eb;

    H << sp << nl << "[[nodiscard]] const char* ice_id() const noexcept override;";
    C << sp << nl << "const char*" << nl << scoped << "::ice_id() const noexcept";
    C << sb;
    C << nl << "return ice_staticId();";
    C << eb;

    if (!dataMembers.empty())
    {
        H << sp;
        H << nl << "/// Creates a tuple with all the fields of this class.";
        H << nl << "/// @return A tuple with all the fields of this class.";
        writeIceTuple(H, p->allDataMembers(), _useWstring);
    }

    H << sp;
    H << nl << "/// Creates a shallow polymorphic copy of this instance.";
    H << nl << "/// @return The cloned value.";
    H << nl << "[[nodiscard]] " << name << "Ptr ice_clone() const { return std::static_pointer_cast<" << name
      << ">(_iceCloneImpl()); }";

    if (p->hasMetadata("cpp:custom-print"))
    {
        H << sp;
        H << nl << "/// Custom ice_print implemented by the application.";
        H << nl << "void ice_print(std::ostream& os) const override;";
    }

    if (!dataMembers.empty())
    {
        H << sp << nl << "void ice_printFields(std::ostream& os) const override;";
        C << sp << nl << "void" << nl << scoped << "::ice_printFields(std::ostream& os) const";
        C << sb;
        bool firstField = true;
        if (base && !base->allDataMembers().empty())
        {
            C << nl << baseClass << "::ice_printFields(os);";
            firstField = false;
        }
        printFields(dataMembers, firstField);
        C << eb;
    }

    return true;
}

void
Slice::Gen::DataDefVisitor::visitClassDefEnd(const ClassDefPtr& p)
{
    const string name = p->mappedName();
    const string scoped = p->mappedScoped("::");
    const string scope = p->mappedScope("::", true);
    const ClassDefPtr base = p->base();

    const DataMemberList dataMembers = p->dataMembers();
    const string baseClass = base ? getUnqualified(base->mappedScoped("::", true), scope) : "Ice::Value";

    H << sp;
    for (const auto& dataMember : dataMembers)
    {
        emitDataMember(dataMember);
    }

    if (!dataMembers.empty())
    {
        H << sp;
    }

    H.dec();
    H << nl << "protected:";
    H.inc();
    H << nl << "/// Copy constructor.";
    H << nl << name << "(const " << name << "&) = default;";

    H << sp;
    H << nl << "/// @private";
    H << nl << "[[nodiscard]] Ice::ValuePtr _iceCloneImpl() const override;";
    C << sp;
    C << nl << "Ice::ValuePtr" << nl << scoped << "::_iceCloneImpl() const";
    C << sb;
    C << nl << "return CloneEnabler<" << name << ">::clone(*this);";
    C << eb;

    H << sp;
    H << nl << "/// @private";
    H << nl << "void _iceWriteImpl(Ice::OutputStream*) const override;";
    C << sp << nl << "void" << nl << scoped << "::_iceWriteImpl(Ice::OutputStream* ostr) const";
    C << sb;
    // lastSlice is true or false.
    C << nl << "ostr->startSlice(ice_staticId(), -1, " << (base ? "false" : "true") << ");";
    if (!dataMembers.empty())
    {
        writeDataMembers(C, dataMembers);
    }
    C << nl << "ostr->endSlice();";
    if (base)
    {
        C << nl << baseClass << "::_iceWriteImpl(ostr);";
    }
    C << eb;

    H << sp;
    H << nl << "/// @private";
    H << nl << "void _iceReadImpl(Ice::InputStream*) override;";
    C << sp << nl << "void" << nl << scoped << "::_iceReadImpl(Ice::InputStream* istr)";
    C << sb;
    C << nl << "istr->startSlice();";
    if (!dataMembers.empty())
    {
        readDataMembers(C, dataMembers);
    }
    C << nl << "istr->endSlice();";
    if (base)
    {
        C << nl << baseClass << "::_iceReadImpl(istr);";
    }
    C << eb;

    H << eb << ';';

    _useWstring = resetUseWstring(_useWstringHist);
}

bool
Slice::Gen::DataDefVisitor::emitBaseInitializers(const ClassDefPtr& p)
{
    ClassDefPtr base = p->base();
    if (!base)
    {
        return false;
    }

    DataMemberList allBaseDataMembers = base->allDataMembers();
    if (allBaseDataMembers.empty())
    {
        return false;
    }

    string upcall = "(";
    for (auto q = allBaseDataMembers.begin(); q != allBaseDataMembers.end(); ++q)
    {
        string memberName = (*q)->mappedName();
        TypePtr memberType = (*q)->type();
        if (q != allBaseDataMembers.begin())
        {
            upcall += ", ";
        }
        upcall += condMove(isMovable(memberType), memberName);
    }
    upcall += ")";

    H << nl << getUnqualified(base->mappedScoped("::", true), p->mappedScope("::", true)) << upcall;
    return true;
}

void
Slice::Gen::DataDefVisitor::emitOneShotConstructor(const ClassDefPtr& p)
{
    DataMemberList allDataMembers = p->allDataMembers();

    // Use empty scope to get full qualified names in types used with future declarations.
    string scope = "";
    if (!allDataMembers.empty())
    {
        vector<string> allParameters;
        map<string, StringList> allOverviews;
        DataMemberList dataMembers = p->dataMembers();

        for (const auto& dataMember : allDataMembers)
        {
            string typeName =
                typeToString(dataMember->type(), dataMember->optional(), scope, dataMember->getMetadata(), _useWstring);
            allParameters.push_back(typeName + " " + dataMember->mappedName());
            if (const auto& comment = dataMember->docComment())
            {
                allOverviews[dataMember->name()] = comment->overview();
            }
        }

        H << sp;
        H << nl << "/// One-shot constructor to initialize all data members.";
        for (const auto& dataMember : allDataMembers)
        {
            auto r = allOverviews.find(dataMember->name());
            if (r != allOverviews.end())
            {
                H << nl << "/// @param " << dataMember->mappedName() << " " << getFirstSentence(r->second);
            }
        }
        H << nl;
        if (allParameters.size() == 1)
        {
            H << "explicit ";
        }
        H << p->mappedName() << spar << allParameters << epar << " noexcept :";
        H.inc();

        if (emitBaseInitializers(p))
        {
            if (!dataMembers.empty())
            {
                H << ',';
            }
        }

        if (!dataMembers.empty())
        {
            H << nl;
        }

        for (auto q = dataMembers.begin(); q != dataMembers.end(); ++q)
        {
            if (q != dataMembers.begin())
            {
                H << ',' << nl;
            }
            string memberName = (*q)->mappedName();
            TypePtr memberType = (*q)->type();
            H << memberName << "(" << condMove(isMovable(memberType), memberName) << ')';
        }

        H.dec();
        H << sb;
        H << eb;
    }
}

void
Slice::Gen::DataDefVisitor::emitDataMember(const DataMemberPtr& p)
{
    const string name = p->mappedName();

    // Use empty scope to get full qualified names in types used with future declarations.
    string scope = "";

    if (!isFirstElement(p))
    {
        H << sp;
    }

    writeDocSummary(H, p);
    H << nl << getDeprecatedAttribute(p) << typeToString(p->type(), p->optional(), scope, p->getMetadata(), _useWstring)
      << ' ' << name;

    if (p->defaultValue())
    {
        H << '{';
        // We don't want to generate `string{""}` because it uses a constructor that is not noexcept.
        if (!p->defaultValue()->empty() || p->optional())
        {
            writeConstantValue(
                H,
                p->type(),
                p->defaultValueType(),
                *p->defaultValue(),
                _useWstring,
                p->getMetadata(),
                scope);
        }
        H << '}';
    }
    H << ";";
}

void
Slice::Gen::DataDefVisitor::printFields(const DataMemberList& fields, bool firstField)
{
    for (const auto& field : fields)
    {
        C << nl << "Ice::print(os << \"";
        if (firstField)
        {
            firstField = false;
        }
        else
        {
            C << ", ";
        }

        C << field->mappedName() << " = \", this->" << field->mappedName() << ");";
    }
}

Slice::Gen::InterfaceVisitor::InterfaceVisitor(IceInternal::Output& h, IceInternal::Output& c, string dllExport)
    : H(h),
      C(c),
      _dllExport(std::move(dllExport))
{
}

bool
Slice::Gen::InterfaceVisitor::visitModuleStart(const ModulePtr& p)
{
    if (!p->contains<InterfaceDef>())
    {
        return false;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);
    H << sp << nl << "namespace " << p->mappedName() << nl << '{';
    H.inc();
    return true;
}

void
Slice::Gen::InterfaceVisitor::visitModuleEnd(const ModulePtr&)
{
    H.dec();
    H << nl << '}';

    _useWstring = resetUseWstring(_useWstringHist);
}

bool
Slice::Gen::InterfaceVisitor::visitInterfaceDefStart(const InterfaceDefPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    _useWstring = setUseWstring(p, _useWstringHist, _useWstring);
    const string name = p->mappedName();
    const string scope = p->mappedScope("::", true);
    const string scoped = p->mappedScoped("::");
    const InterfaceList bases = p->bases();

    writeDocSummary(H, p, {.generateDeprecated = false, .includeHeaderFile = true});
    H << nl << "class " << _dllExport << name << " : ";
    H.spar("");
    if (bases.empty())
    {
        H << "public virtual Ice::Object";
    }
    else
    {
        for (const auto& base : bases)
        {
            H << ("public virtual " + getUnqualified(base->mappedScoped("::", true), scope));
        }
    }
    H.epar("");
    H << sb;
    H.dec();
    H << nl << "public:";
    H.inc();

    H << nl << "/// The associated proxy type.";
    H << nl << "using ProxyType = " << p->mappedName() << "Prx;";

    // The dispatch function.
    OperationList allOps = p->allOperations();
    if (!allOps.empty())
    {
        list<pair<string, string>> allOpNames;
        transform(
            allOps.begin(),
            allOps.end(),
            back_inserter(allOpNames),
            [](const ContainedPtr& it) { return std::make_pair(it->name(), it->mappedName()); });
        allOpNames.emplace_back("ice_id", "ice_id");
        allOpNames.emplace_back("ice_ids", "ice_ids");
        allOpNames.emplace_back("ice_isA", "ice_isA");
        allOpNames.emplace_back("ice_ping", "ice_ping");
        allOpNames.sort();

        // The Ice:: qualification confuses doxygen, so we remove it when in the Ice module.
        bool inIceModule = p->scope() == "::Ice::";
        string_view incomingRequestType = inIceModule ? "IncomingRequest" : "Ice::IncomingRequest";
        string_view outgoingResponseType = inIceModule ? "OutgoingResponse" : "Ice::OutgoingResponse";

        H << sp;
        H << nl
          << "/// Dispatches an incoming request to one of the member functions of this generated class, based on the "
             "operation name carried by the request.";
        H << nl << "/// @param request The incoming request.";
        H << nl << "/// @param sendResponse The callback to send the response.";
        H << nl << "void dispatch(" << incomingRequestType << "& request, std::function<void(" << outgoingResponseType
          << ")> sendResponse) override;";

        C << sp;
        C << nl << "void";
        C << nl << scoped
          << "::dispatch(Ice::IncomingRequest& request, std::function<void(Ice::OutgoingResponse)> sendResponse)";
        C << sb;

        C << sp;
        C << nl << "static constexpr std::array<std::string_view, " << allOpNames.size() << "> allOperations";
        C.spar("{");
        for (const auto& opNames : allOpNames)
        {
            C << '"' + opNames.first + '"';
        }
        C.epar("}");
        C << ";";

        C << sp;
        C << nl << "const Ice::Current& current = request.current();";
        C << nl << "auto r = std::equal_range(allOperations.begin(), allOperations.end(), current.operation);";
        // range is a C++ 20 feature and we want to keep the generated code C++17 compatible and lint-free.
        C << " // NOLINT(modernize-use-ranges)";
        C << nl << "if (r.first == r.second)";
        C << sb;
        C << nl
          << "sendResponse(Ice::makeOutgoingResponse(std::make_exception_ptr(Ice::OperationNotExistException{__"
             "FILE__, __LINE__}), current));";
        C << nl << "return;";
        C << eb;
        C << sp;
        C << nl << "switch (r.first - allOperations.begin())";
        C << sb;
        int i = 0;
        for (const auto& opNames : allOpNames)
        {
            C << nl << "case " << i++ << ':';
            C << sb;
            C << nl << "_iceD_" << opNames.second << "(request, std::move(sendResponse));";
            C << nl << "break;";
            C << eb;
        }
        C << nl << "default:";
        C << sb;
        C << nl << "assert(false);";
        C << nl
          << "sendResponse(Ice::makeOutgoingResponse(std::make_exception_ptr(Ice::OperationNotExistException{__"
             "FILE__, __LINE__}), current));";
        C << eb;
        C << eb;
        C << eb;
    }

    H << sp;
    H << nl << "[[nodiscard]] std::vector<std::string> ice_ids(const Ice::Current& current) const override;";
    H << sp;
    H << nl << "[[nodiscard]] std::string ice_id(const Ice::Current& current) const override;";

    C << sp;
    C << nl << "std::vector<std::string>" << nl << scoped << "::ice_ids(const Ice::Current&) const";
    C << sb;
    // These type IDs are sorted alphabetically.
    C << nl << "static const std::vector<std::string> allTypeIds = ";
    C.spar("{");
    for (const auto& typeId : p->ids())
    {
        C << '"' + typeId + '"';
    }
    C.epar("}");
    C << ";";
    C << nl << "return allTypeIds;";
    C << eb;

    C << sp;
    C << nl << "std::string" << nl << scoped << "::ice_id(const Ice::Current&) const";
    C << sb;
    C << nl << "return std::string{ice_staticId()};";
    C << eb;

    return true;
}

void
Slice::Gen::InterfaceVisitor::visitInterfaceDefEnd(const InterfaceDefPtr& p)
{
    const string name = p->mappedName();
    const string scoped = p->mappedScoped("::");

    H << sp;
    H << nl << "/// Gets the type ID of the associated Slice interface.";
    H << nl << "/// @return The string `\"" << p->scoped() << "\"`.";
    H << nl << "static const char* ice_staticId() noexcept;";

    C << sp;
    C << nl << "const char*" << nl << scoped << "::ice_staticId() noexcept";
    C << sb;
    C << nl << "return \"" << p->scoped() << "\";";
    C << eb;

    H << eb << ';';
    H << sp;
    H << nl << "/// A shared pointer to " << getArticleFor(name) << ' ' << name << ".";
    H << nl << "using " << name << "Ptr = std::shared_ptr<" << name << ">;";

    _useWstring = resetUseWstring(_useWstringHist);
}

void
Slice::Gen::InterfaceVisitor::visitOperation(const OperationPtr& p)
{
    const string name = p->mappedName();
    const string scope = p->mappedScope("::");
    const InterfaceDefPtr container = p->interface();
    const string interfaceScope = container->mappedScope("::", true);

    TypePtr ret = p->returnType();

    vector<string> params;
    vector<string> args;

    vector<string> responseParams;
    vector<string> responseParamsDecl;
    vector<string> responseParamsImplDecl;

    const ParameterList inParams = p->inParameters();
    const ParameterList outParams = p->outParameters();
    const ParameterList paramList = p->parameters();

    const bool amd = (container->hasMetadata("amd") || p->hasMetadata("amd"));
    const string opName = amd ? (name + "Async") : name;

    const string returnValueParam = escapeParam(outParams, "returnValue");
    const string responsecbParam = escapeParam(inParams, "response");
    const string excbParam = escapeParam(inParams, "exception");
    const string currentParam = escapeParam(amd ? inParams : paramList, "current");
    const string currentTypeDecl = "const Ice::Current&";
    const string currentDecl = currentTypeDecl + " " + currentParam;

    const optional<DocComment>& comment = p->docComment();

    string isConst = p->hasMetadata("cpp:const") ? " const" : "";
    string noDiscard = "";

    if (ret)
    {
        string typeS = inputTypeToString(ret, p->returnIsOptional(), interfaceScope, p->getMetadata(), _useWstring);
        responseParams.push_back(typeS + " " + returnValueParam);
        responseParamsDecl.push_back(typeS + " ret");
        responseParamsImplDecl.push_back(typeS + " ret");

        // clang-tidy produces a lint for a const member function that returns a value, is not [[nodiscard]] and
        // has no non-const reference parameters (= Slice out parameters).
        // See https://clang.llvm.org/extra/clang-tidy/checks/modernize/use-nodiscard.html
        if (!amd && !isConst.empty() && p->outParameters().empty())
        {
            noDiscard = "[[nodiscard]] ";
        }
    }

    string retS;
    if (amd || !ret)
    {
        retS = "void";
    }
    else if (p->hasMarshaledResult())
    {
        retS = marshaledResultStructName(name);
    }
    else
    {
        retS = typeToString(ret, p->returnIsOptional(), interfaceScope, p->getMetadata(), _useWstring);
    }

    for (const auto& param : paramList)
    {
        TypePtr type = param->type();
        string paramName = param->mappedName();
        string prefixedParamName = paramPrefix + paramName;
        bool isOutParam = param->isOutParam();

        if (!isOutParam)
        {
            params.push_back(
                typeToString(
                    type,
                    param->optional(),
                    interfaceScope,
                    param->getMetadata(),
                    _useWstring | TypeContext::UnmarshalParamZeroCopy) +
                " " + paramName);
            args.push_back(condMove(isMovable(type), prefixedParamName));
        }
        else
        {
            if (!p->hasMarshaledResult() && !amd)
            {
                params.push_back(
                    outputTypeToString(type, param->optional(), interfaceScope, param->getMetadata(), _useWstring) +
                    " " + paramName);
                args.push_back(condMove(isMovable(type) && !isOutParam, prefixedParamName));
            }

            string responseTypeS =
                inputTypeToString(param->type(), param->optional(), interfaceScope, param->getMetadata(), _useWstring);
            responseParams.push_back(responseTypeS + " " + paramName);
            responseParamsDecl.push_back(responseTypeS + " " + prefixedParamName);
            responseParamsImplDecl.push_back(responseTypeS + " " + prefixedParamName);
        }
    }
    if (amd)
    {
        if (p->hasMarshaledResult())
        {
            string resultName = marshaledResultStructName(name);
            params.push_back("std::function<void(" + resultName + ")> " + responsecbParam);
            args.push_back(
                "[responseHandler](" + resultName +
                " marshaledResult) { responseHandler->sendResponse(std::move(marshaledResult)); }");
        }
        else
        {
            params.push_back("std::function<void(" + joinString(responseParams, ", ") + ")> " + responsecbParam);
            args.emplace_back(
                ret || !outParams.empty() ? "std::move(responseCb)"
                                          : "[responseHandler] { responseHandler->sendEmptyResponse(); }");
        }
        params.push_back("std::function<void(std::exception_ptr)> " + excbParam);
        args.emplace_back("[responseHandler](std::exception_ptr ex) { responseHandler->sendException(ex); }");
        params.push_back(currentDecl);
        args.emplace_back("responseHandler->current()");
    }
    else
    {
        params.push_back(currentDecl);
        args.emplace_back("request.current()");
    }

    if (p->hasMarshaledResult())
    {
        string resultName = marshaledResultStructName(name);
        H << sp;
        H << nl << "/// Marshaled result structure for operation " << opName << ".";
        H << nl << "class " << resultName << " : public Ice::MarshaledResult";
        H << sb;
        H.dec();
        H << nl << "public:";
        H.inc();
        H << nl << "/// Marshals the result immediately.";
        if (comment)
        {
            const StringList& returnsDoc = comment->returns();
            if (ret && !returnsDoc.empty())
            {
                H << nl << "/// @param " << returnValueParam << " " << getFirstSentence(returnsDoc);
            }

            const map<string, StringList>& paramComments = comment->parameters();
            for (const auto& param : outParams)
            {
                auto r = paramComments.find(param->name());
                if (r != paramComments.end())
                {
                    H << nl << "/// @param " << param->mappedName() << " " << getFirstSentence(r->second);
                }
            }
        }
        const string mrcurrent = escapeParam(outParams, "current");
        H << nl << "/// @param " << mrcurrent << " The Current object of the incoming request.";
        H << nl << resultName << spar << responseParams << currentTypeDecl + " " + mrcurrent << epar << ";";
        H << eb << ';';

        C << sp << nl << scope << resultName << "::" << resultName;
        C << spar << responseParamsImplDecl << currentTypeDecl + " current" << epar << ":";
        C.inc();
        C << nl << "MarshaledResult(current)";
        C.dec();
        C << sb;
        // Switch to ostr pointer expected by WriteMarshalCode.
        C << nl << "Ice::OutputStream* ostr = &_ostr;";
        C << nl << "ostr->startEncapsulation(current.encoding, " << opFormatTypeToString(p) << ");";
        writeMarshalCode(C, outParams, p);
        if (p->returnsClasses())
        {
            C << nl << "ostr->writePendingValues();";
        }
        C << nl << "ostr->endEncapsulation();";
        C << eb;
    }

    H << sp;
    if (comment)
    {
        OpDocParamType pt = (amd || p->hasMarshaledResult()) ? OpDocInParams : OpDocAllParams;
        StringList postParams;
        StringList returns;
        if (amd)
        {
            if (p->hasMarshaledResult())
            {
                postParams.push_back(
                    "@param " + responsecbParam + " The response callback. It accepts a marshaled result.");
            }
            else
            {
                if (p->returnsAnyValues())
                {
                    postParams.push_back("@param " + responsecbParam + " The response callback. It accepts:");
                    postParams.splice(postParams.end(), createOpOutParamsDoc(p, *comment));
                }
                else
                {
                    postParams.push_back("@param " + responsecbParam + " The response callback.");
                }
            }

            postParams.push_back("@param " + excbParam + " The exception callback.");
        }
        else if (p->hasMarshaledResult())
        {
            returns.emplace_back("The marshaled result.");
        }
        else
        {
            returns = comment->returns();
        }
        postParams.push_back("@param " + currentParam + " The Current object of the incoming request.");
        writeOpDocSummary(H, p, *comment, pt, true, {.generateDeprecated = false}, StringList(), postParams, returns);
    }
    H << nl << noDiscard << "virtual " << retS << ' ' << opName << spar << params << epar << isConst << " = 0;";
    H << sp;
    H << nl << "/// @private";
    H << nl << "void _iceD_" << p->mappedName() << "(Ice::IncomingRequest&, std::function<void(Ice::OutgoingResponse)>)"
      << isConst << ';';

    C << sp;
    C << nl << "void";
    C << nl << scope << "_iceD_" << p->mappedName() << "(";
    C.inc();
    C << nl << "Ice::IncomingRequest& request," << nl << "std::function<void(Ice::OutgoingResponse)> sendResponse)"
      << isConst;

    if (!amd)
    {
        // We want to use the same signature for sync and async dispatch functions. There is no performance penalty for
        // sync functions since we always move this parameter.
        C << " // NOLINT(performance-unnecessary-value-param)";
    }
    C.dec();
    C << sb;
    if (p->mode() == Operation::Mode::Normal)
    {
        C << nl << "checkNonIdempotent(request.current());";
    }

    if (!inParams.empty())
    {
        C << nl << "auto istr = &request.inputStream();";
        C << nl << "istr->startEncapsulation();";
        writeAllocateCode(C, inParams, nullptr, interfaceScope, _useWstring | TypeContext::UnmarshalParamZeroCopy);
        writeUnmarshalCode(C, inParams, nullptr);
        if (p->sendsClasses())
        {
            C << nl << "istr->readPendingValues();";
        }
        C << nl << "istr->endEncapsulation();";
    }
    else
    {
        C << nl << "request.inputStream().skipEmptyEncapsulation();";
    }

    if (!amd)
    {
        if (p->hasMarshaledResult())
        {
            C << nl << "sendResponse(Ice::OutgoingResponse{";
        }
        else
        {
            writeAllocateCode(C, outParams, nullptr, interfaceScope, _useWstring);
            if (ret)
            {
                C << nl << "const " << retS << " ret = ";
            }
            else
            {
                C << nl;
            }
        }

        C << "this->" << opName << spar << args << epar;
        if (p->hasMarshaledResult())
        {
            C << ".outputStream(), request.current()});";
        }
        else
        {
            C << ";";
            if (ret || !outParams.empty())
            {
                C << nl << "sendResponse(Ice::makeOutgoingResponse([&](Ice::OutputStream* ostr)";
                C.inc();
                C << sb;
                writeMarshalCode(C, outParams, p);
                if (p->returnsClasses())
                {
                    C << nl << "ostr->writePendingValues();";
                }
                C << eb << ",";
                C << nl << "request.current()";
                if (p->format())
                {
                    C << ",";
                    C << nl << opFormatTypeToString(p);
                }
                C << "));";
                C.dec();
            }
            else
            {
                C << nl << "sendResponse(Ice::makeEmptyOutgoingResponse(request.current()));";
            }
        }
    }
    else
    {
        C << nl
          << "auto responseHandler = "
             "std::make_shared<IceInternal::AsyncResponseHandler>(std::move(sendResponse), request.current());";
        if (!p->hasMarshaledResult() && (ret || !outParams.empty()))
        {
            C << nl << "auto responseCb = [responseHandler]" << spar << responseParamsDecl << epar;
            C << sb;
            C << nl << "responseHandler->sendResponse(";
            C.inc();
            C << nl << "[&](Ice::OutputStream* ostr)";
            C << sb;
            writeMarshalCode(C, outParams, p);
            if (p->returnsClasses())
            {
                C << nl << "ostr->writePendingValues();";
            }
            C << eb;
            if (p->format())
            {
                C << ",";
                C << nl << opFormatTypeToString(p);
            }
            C << ");";
            C.dec();
            C << eb << ';';
        }
        C << nl << "try";
        C << sb;
        C << nl << "this->" << opName << spar << args << epar << ';';
        C << eb;
        C << nl << "catch (...)";
        C << sb;
        C << nl << "responseHandler->sendException(std::current_exception());";
        C << eb;
    }
    C << eb;
}

Slice::Gen::StreamVisitor::StreamVisitor(Output& h) : H(h) {}

bool
Slice::Gen::StreamVisitor::visitModuleStart(const ModulePtr& m)
{
    if (!m->contains<Struct>() && !m->contains<Enum>())
    {
        return false;
    }

    if (m->isTopLevel())
    {
        // Only emit this for the top-level module.
        H << sp;
        H << nl << "namespace Ice" << nl << '{';
        H.inc();
    }
    return true;
}

void
Slice::Gen::StreamVisitor::visitModuleEnd(const ModulePtr& m)
{
    if (m->isTopLevel())
    {
        // Only emit this for the top-level module.
        H.dec();
        H << nl << '}';
    }
}

bool
Slice::Gen::StreamVisitor::visitStructStart(const StructPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    H << nl << "/// @cond INTERNAL";
    H << nl << "template<>";
    H << nl << "struct StreamableTraits<" << p->mappedScoped("::", true) << ">";
    H << sb;
    H << nl << "static constexpr StreamHelperCategory helper = StreamHelperCategoryStruct;";
    H << nl << "static constexpr int minWireSize = " << p->minWireSize() << ";";
    H << nl << "static constexpr bool fixedLength = " << (p->isVariableLength() ? "false" : "true") << ";";
    H << eb << ";";
    H << sp;

    writeStreamReader(H, p, p->dataMembers());
    H << nl << "/// @endcond";
    return false;
}

void
Slice::Gen::StreamVisitor::visitEnum(const EnumPtr& p)
{
    if (_firstElement)
    {
        _firstElement = false;
    }
    else
    {
        H << sp;
    }

    H << nl << "/// @cond INTERNAL";
    H << nl << "template<>";
    H << nl << "struct StreamableTraits<" << p->mappedScoped("::", true) << ">";
    H << sb;
    H << nl << "static constexpr StreamHelperCategory helper = StreamHelperCategoryEnum;";
    H << nl << "static constexpr int minValue = " << p->minValue() << ";";
    H << nl << "static constexpr int maxValue = " << p->maxValue() << ";";
    H << nl << "static constexpr int minWireSize = " << p->minWireSize() << ";";
    H << nl << "static constexpr bool fixedLength = false;";
    H << eb << ";";
    H << nl << "/// @endcond";
}
