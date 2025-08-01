// Copyright (c) ZeroC, Inc.

#include "PythonUtil.h"
#include "../Ice/ConsoleUtil.h"
#include "../Ice/FileUtil.h"
#include "../Ice/Options.h"
#include "../Slice/DocCommentParser.h"
#include "../Slice/FileTracker.h"
#include "../Slice/MetadataValidation.h"
#include "../Slice/Preprocessor.h"
#include "../Slice/Util.h"
#include "Ice/CtrlCHandler.h"
#include "Ice/StringUtil.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <iostream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace std;
using namespace Slice;
using namespace IceInternal;

namespace
{
    const char* const tripleQuotes = R"(""")";
}

string
Slice::Python::getPythonModuleForDefinition(const SyntaxTreeBasePtr& p)
{
    if (auto builtin = dynamic_pointer_cast<Builtin>(p))
    {
        static const char* builtinTable[] = {"", "", "", "", "", "", "", "", "Ice.ObjectPrx", "Ice.Value"};

        return builtinTable[builtin->kind()];
    }
    else
    {
        auto contained = dynamic_pointer_cast<Contained>(p);
        assert(contained);
        return contained->mappedScoped(".");
    }
}

string
Slice::Python::getPythonModuleForForwardDeclaration(const SyntaxTreeBasePtr& p)
{
    string declarationModule = getPythonModuleForDefinition(p);
    if (!declarationModule.empty())
    {
        // Append "_forward" to the module name for generated modules.
        declarationModule += "_forward";
    }
    return declarationModule;
}

string
Slice::Python::getImportAlias(
    const ContainedPtr& source,
    const map<string, string>& allImports,
    const SyntaxTreeBasePtr& p)
{
    if (auto builtin = dynamic_pointer_cast<Builtin>(p))
    {
        if (builtin->kind() == Builtin::KindObjectProxy)
        {
            return getImportAlias(source, allImports, "Ice.ObjectPrx", "ObjectPrx");
        }
        else if (builtin->kind() == Builtin::KindValue)
        {
            return getImportAlias(source, allImports, "Ice.Value", "Value");
        }
        return "";
    }
    else
    {
        auto contained = dynamic_pointer_cast<Contained>(p);
        assert(contained);
        return getImportAlias(source, allImports, contained->mappedScoped("."), contained->mappedName());
    }
}

string
Slice::Python::getImportAlias(
    const ContainedPtr& source,
    const std::map<std::string, std::string>& allImports,
    const string& moduleName,
    const string& name)
{
    // Get the list of definitions exported for the source Python module.
    auto all = getAll(source);

    // Whether we need to use an alias for the import.
    bool useAlias = false;

    if (moduleName == source->mappedScoped("."))
    {
        // If the source module is the same as the module name being imported. We are using a
        // definition from the current module and we don't need an alias.
        useAlias = false;
    }
    else if (find(all.begin(), all.end(), name) != all.end())
    {
        // The name being bound comes from a different module and conflicts with one of the names
        // exported by the source module. We need to use an alias.
        useAlias = true;
    }
    else
    {
        // If the name being bound is already imported from a different module, we need to use an alias.
        auto p = allImports.find(name);
        if (p != allImports.end())
        {
            useAlias = p->second != moduleName;
        }
    }

    if (useAlias)
    {
        string alias = "_m_" + moduleName + "_" + name;
        std::replace(alias.begin(), alias.end(), '.', '_');
        return alias;
    }
    else
    {
        return name;
    }
}

string
Slice::Python::getMetaType(const SyntaxTreeBasePtr& p)
{
    if (auto builtin = dynamic_pointer_cast<Builtin>(p))
    {
        static const char* builtinTable[] = {
            "IcePy._t_byte",
            "IcePy._t_bool",
            "IcePy._t_short",
            "IcePy._t_int",
            "IcePy._t_long",
            "IcePy._t_float",
            "IcePy._t_double",
            "IcePy._t_string",
            "_Ice_ObjectPrx_t",
            "_Ice_Value_t"};

        return builtinTable[builtin->kind()];
    }
    else
    {
        auto contained = dynamic_pointer_cast<Contained>(p);
        assert(contained);
        string s = "_" + contained->mappedScoped("_");
        // Replace here is required in case any module is remapped with python:identifier into a nested package.
        replace(s.begin(), s.end(), '.', '_');
        if (dynamic_pointer_cast<InterfaceDef>(contained) || dynamic_pointer_cast<InterfaceDecl>(contained))
        {
            s += "Prx";
        }
        s += "_t";
        return s;
    }
}

string
Slice::Python::CodeVisitor::typeToTypeHintString(
    const TypePtr& type,
    bool optional,
    const ContainedPtr& source,
    bool forMarshaling)
{
    assert(type);

    if (optional)
    {
        if (isProxyType(type) || type->isClassType())
        {
            // We map optional proxies and classes like regular ones.
            return typeToTypeHintString(type, false, source, forMarshaling);
        }
        else
        {
            return typeToTypeHintString(type, false, source, forMarshaling) + " | None";
        }
    }

    if (auto builtin = dynamic_pointer_cast<Builtin>(type))
    {
        if (builtin->kind() == Builtin::KindObjectProxy)
        {
            return getImportAlias(source, "Ice.ObjectPrx", "ObjectPrx") + " | None";
        }
        else if (builtin->kind() == Builtin::KindValue)
        {
            return getImportAlias(source, "Ice.Value", "Value") + " | None";
        }
        else
        {
            static constexpr string_view builtinTable[] = {"int", "bool", "int", "int", "int", "float", "float", "str"};
            return string{builtinTable[builtin->kind()]};
        }
    }
    else
    {
        string definitionModule = getPythonModuleForDefinition(type);
        string sourceModule = getPythonModuleForDefinition(source);

        auto contained = dynamic_pointer_cast<Contained>(type);
        assert(contained);

        if (auto proxy = dynamic_pointer_cast<InterfaceDecl>(type))
        {
            return getImportAlias(source, proxy->mappedScoped("."), proxy->mappedName() + "Prx") + " | None";
        }
        else if (auto cls = dynamic_pointer_cast<ClassDecl>(type))
        {
            return getImportAlias(source, cls->mappedScoped("."), cls->mappedName()) + " | None";
        }
        else if (auto seq = dynamic_pointer_cast<Sequence>(type))
        {
            ostringstream os;
            // Map Slice built-in numeric types to NumPy types.
            static const char* numpyBuiltinTable[] = {"int8", "bool", "int16", "int32", "int64", "float32", "float64"};

            auto elementType = dynamic_pointer_cast<Builtin>(seq->type());
            bool isByteSequence = elementType && elementType->kind() == Builtin::KindByte;
            if (forMarshaling)
            {
                // For marshaling, we use a generic Sequence type hint, additionally we accept a bytes object for byte
                // sequences, and for sequences with NumPy metadata we use the NumPy NDArray type hint because it
                // doesn't conform to the generic sequence type.
                os << "Sequence[" + typeToTypeHintString(seq->type(), false, source, forMarshaling) + "]";
                if (isByteSequence)
                {
                    os << " | bytes";
                }
                if (seq->hasMetadata("python:numpy.ndarray"))
                {
                    assert(elementType && elementType->kind() <= Builtin::KindDouble);
                    os << " | numpy.typing.NDArray[numpy." << numpyBuiltinTable[elementType->kind()] << "]";
                }
            }
            else if (seq->hasMetadata("python:list"))
            {
                os << "list[" << typeToTypeHintString(seq->type(), false, source, forMarshaling) << "]";
            }
            else if (seq->hasMetadata("python:tuple"))
            {
                os << "tuple[" << typeToTypeHintString(seq->type(), false, source, forMarshaling) << "]";
            }
            else if (seq->hasMetadata("python:numpy.ndarray"))
            {
                assert(elementType && elementType->kind() <= Builtin::KindDouble);
                os << "numpy.typing.NDArray[numpy." << numpyBuiltinTable[elementType->kind()] << "]";
            }
            else if (seq->hasMetadata("python:array.array"))
            {
                assert(elementType && elementType->kind() <= Builtin::KindDouble);
                os << "array.array[" << typeToTypeHintString(seq->type(), false, source, forMarshaling) << "]";
            }
            else if (isByteSequence)
            {
                os << "bytes";
            }
            else
            {
                return "list[" + typeToTypeHintString(seq->type(), false, source, forMarshaling) + "]";
            }
            // TODO add support for python:memoryview.
            return os.str();
        }
        else if (auto dict = dynamic_pointer_cast<Dictionary>(type))
        {
            ostringstream os;
            os << "dict[" << typeToTypeHintString(dict->keyType(), false, source, forMarshaling) << ", "
               << typeToTypeHintString(dict->valueType(), false, source, forMarshaling) << "]";
            return os.str();
        }
        else
        {
            return getImportAlias(source, contained);
        }
    }
}

string
Slice::Python::CodeVisitor::returnTypeHint(const OperationPtr& operation, MethodKind methodKind)
{
    auto source = dynamic_pointer_cast<Contained>(operation->container());
    string returnTypeHint;
    ParameterList outParameters = operation->outParameters();
    bool forMarshaling = methodKind == MethodKind::Dispatch;
    if (operation->returnsMultipleValues())
    {
        ostringstream os;
        os << "tuple[";
        if (operation->returnType())
        {
            os << typeToTypeHintString(operation->returnType(), operation->returnIsOptional(), source, forMarshaling);
            os << ", ";
        }

        for (const auto& param : outParameters)
        {
            os << typeToTypeHintString(param->type(), param->optional(), source, forMarshaling);
            if (param != outParameters.back())
            {
                os << ", ";
            }
        }
        os << "]";
        returnTypeHint = os.str();
    }
    else if (operation->returnType())
    {
        returnTypeHint =
            typeToTypeHintString(operation->returnType(), operation->returnIsOptional(), source, forMarshaling);
    }
    else if (!outParameters.empty())
    {
        const auto& param = outParameters.front();
        returnTypeHint = typeToTypeHintString(param->type(), param->optional(), source, forMarshaling);
    }
    else
    {
        returnTypeHint = "None";
    }

    switch (methodKind)
    {
        case MethodKind::AsyncInvocation:
            return "Awaitable[" + returnTypeHint + "]";
        case MethodKind::Dispatch:
            return returnTypeHint + " | Awaitable[" + returnTypeHint + "]";
        case MethodKind::SyncInvocation:
        default:
            return returnTypeHint;
    }
}

string
Slice::Python::CodeVisitor::operationReturnTypeHint(const OperationPtr& operation, MethodKind methodKind)
{
    return " -> " + returnTypeHint(operation, methodKind);
}

string
Slice::Python::formatFields(const DataMemberList& members)
{
    if (members.empty())
    {
        return "";
    }

    ostringstream os;
    bool first = true;
    os << "{format_fields(";
    for (const auto& dataMember : members)
    {
        if (!first)
        {
            os << ", ";
        }
        first = false;
        os << dataMember->mappedName() << "=self." << dataMember->mappedName();
    }
    os << ")}";
    return os.str();
}

Python::CodeFragment
Slice::Python::createCodeFragmentForPythonModule(const ContainedPtr& contained, const string& code)
{
    Python::CodeFragment fragment;
    bool isForwardDeclaration =
        dynamic_pointer_cast<InterfaceDecl>(contained) || dynamic_pointer_cast<ClassDecl>(contained);
    fragment.moduleName = isForwardDeclaration ? getPythonModuleForForwardDeclaration(contained)
                                               : getPythonModuleForDefinition(contained);
    fragment.code = code;
    fragment.packageName = fragment.moduleName;

    auto pos = fragment.packageName.rfind('.');
    assert(pos != string::npos);
    fragment.packageName = fragment.packageName.substr(0, pos);

    fragment.sliceFileName = contained->file();
    string fileName = fragment.moduleName;
    replace(fileName.begin(), fileName.end(), '.', '/');
    fragment.fileName = fileName + ".py";
    fragment.isPackageIndex = false;
    return fragment;
}

void
Slice::Python::writeHeader(IceInternal::Output& out)
{
    out << "# Copyright (c) ZeroC, Inc.";
    out << sp;
    out << nl << "# slice2py version " << ICE_STRING_VERSION;
}

void
Slice::Python::writePackageIndex(const std::map<std::string, std::set<std::string>>& imports, IceInternal::Output& out)
{
    out << sp;
    writeHeader(out);
    if (!imports.empty())
    {
        out << sp;
        std::list<string> allDefinitions;
        for (const auto& [moduleName, definitions] : imports)
        {
            for (const auto& name : definitions)
            {
                out << nl << "from ." << moduleName << " import " << name;
                allDefinitions.push_back(name);
            }
        }
        out << nl;

        out << sp;
        out << nl << "__all__ = [";
        out.inc();
        for (auto it = allDefinitions.begin(); it != allDefinitions.end();)
        {
            out << nl << ("\"" + *it + "\"");
            if (++it != allDefinitions.end())
            {
                out << ",";
            }
        }
        out.dec();
        out << nl << "]";
        out << nl;
    }
}

void
Slice::Python::createPackagePath(const string& packageName, const string& outputPath)
{
    vector<string> packageParts;
    IceInternal::splitString(string_view{packageName}, ".", packageParts);
    assert(!packageParts.empty());
    string packagePath = outputPath;
    for (const auto& part : packageParts)
    {
        packagePath += "/" + part;
        int err = IceInternal::mkdir(packagePath, 0777);
        if (err == 0)
        {
            FileTracker::instance()->addDirectory(packagePath);
        }
        else if (errno == EEXIST && IceInternal::directoryExists(packagePath))
        {
            // If the Slice compiler is run concurrently, it's possible that another instance of it has already
            // created the directory.
        }
        else
        {
            ostringstream os;
            os << "cannot create directory '" << packagePath << "': " << IceInternal::errorToString(errno);
            throw FileException(os.str());
        }
    }
}

bool
Slice::Python::ImportVisitor::visitStructStart(const StructPtr& p)
{
    addRuntimeImport("dataclasses", "dataclass", p);
    return true;
}

bool
Slice::Python::ImportVisitor::visitClassDefStart(const ClassDefPtr& p)
{
    addRuntimeImport("dataclasses", "dataclass", p);

    // Import the meta type that is created in the Xxx_forward module for forward declarations.
    addRuntimeImportForMetaType(p->declaration(), p);

    // Add imports required for the base class type.
    if (ClassDefPtr base = p->base())
    {
        addRuntimeImport(base, p);
        addRuntimeImportForMetaType(base, p);
    }
    else
    {
        // If the class has no base, we import the Ice.Object type.
        addRuntimeImport("Ice.Value", "Value", p);
    }

    return true;
}

bool
Slice::Python::ImportVisitor::visitExceptionStart(const ExceptionPtr& p)
{
    addRuntimeImport("dataclasses", "dataclass", p);

    // Add imports required for base exception types.
    if (ExceptionPtr base = p->base())
    {
        addRuntimeImport(base, p);
        addRuntimeImportForMetaType(base, p);
    }
    else
    {
        // If the exception has no base, we import the Ice.UserException type.
        addRuntimeImport("Ice.UserException", "UserException", p);
    }
    return true;
}

void
Slice::Python::ImportVisitor::visitDataMember(const DataMemberPtr& p)
{
    auto parent = dynamic_pointer_cast<Contained>(p->container());
    auto type = p->type();

    // Import 'field' from dataclasses to initialize non-optional Struct, Dictionary, and Sequence members.
    // These types cannot use direct field initializers.
    if (!p->optional() && (dynamic_pointer_cast<Struct>(type) || dynamic_pointer_cast<Sequence>(type) ||
                           dynamic_pointer_cast<Dictionary>(type)))
    {
        addRuntimeImport("dataclasses", "field", parent);
    }

    // Add imports required for data member types.

    // For fields with a type that is a Struct, we need to import it as a RuntimeImport, to
    // initialize the field in the constructor. For other contained types, we only need the
    // import for type hints.
    if (auto sequence = dynamic_pointer_cast<Sequence>(type))
    {
        addRuntimeImportForSequence(sequence, parent);
    }
    else if (dynamic_pointer_cast<Struct>(type) || dynamic_pointer_cast<Enum>(type))
    {
        addRuntimeImport(type, parent);
    }
    else
    {
        addTypingImport(type, parent, true);
    }
    addRuntimeImportForMetaType(type, parent);

    // If the data member has a default value, and the type of the default value is an Enum or a Const
    // we need to import the corresponding Enum or Const.
    if (p->defaultValue() &&
        (dynamic_pointer_cast<Const>(p->defaultValueType()) || dynamic_pointer_cast<Enum>(p->defaultValueType())))
    {
        addRuntimeImport(p->defaultValueType(), parent);
    }
}

bool
Slice::Python::ImportVisitor::visitInterfaceDefStart(const InterfaceDefPtr& p)
{
    // Import the proxy meta type that is created in the XxxF module for forward declarations.
    addRuntimeImportForMetaType(p->declaration(), p);

    // Add imports required for base interfaces types.
    const InterfaceList& bases = p->bases();
    if (bases.empty())
    {
        addRuntimeImport("Ice.ObjectPrx", "ObjectPrx", p);
        addRuntimeImport("Ice.Object", "Object", p);
    }
    else
    {
        for (const auto& base : bases)
        {
            addRuntimeImport(base, p, TypeContext::Proxy);
            addRuntimeImport(base, p, TypeContext::Servant);
        }
    }

    addRuntimeImport("abc", "ABC", p);
    addRuntimeImport("typing", "overload", p);

    addRuntimeImport("Ice.ObjectPrx", "checkedCast", p);
    addRuntimeImport("Ice.ObjectPrx", "checkedCastAsync", p);
    addRuntimeImport("Ice.ObjectPrx", "uncheckedCast", p);

    addTypingImport("Ice.ObjectPrx", "ObjectPrx", p);
    addTypingImport("Ice.Current", "Current", p);

    // Required by the core operations, ice_isA, ice_ids, ice_id, and ice_ping.
    addTypingImport("collections.abc", "Awaitable", p);
    addTypingImport("collections.abc", "Sequence", p);

    // Add imports required for operation parameters and return types.
    const OperationList& operations = p->allOperations();
    if (!operations.empty())
    {
        addRuntimeImport("abc", "abstractmethod", p);
        addRuntimeImport("Ice.OperationMode", "OperationMode", p);
    }

    for (const auto& op : operations)
    {
        // We need to call `addTypingImport` twice per parameter. This is required because for list the marshaling
        // and unmarshaling code might require different type hints.
        auto ret = op->returnType();
        if (ret)
        {
            if (auto sequence = dynamic_pointer_cast<Sequence>(ret))
            {
                addRuntimeImportForSequence(sequence, p);
            }
            addTypingImport(ret, p, false);
            addTypingImport(ret, p, true);

            addRuntimeImportForMetaType(ret, p);
        }

        for (const auto& param : op->parameters())
        {
            if (auto sequence = dynamic_pointer_cast<Sequence>(param->type()))
            {
                addRuntimeImportForSequence(sequence, p);
            }
            addTypingImport(param->type(), p, false);
            addTypingImport(param->type(), p, true);

            addRuntimeImportForMetaType(param->type(), p);
        }

        for (const auto& ex : op->throws())
        {
            addRuntimeImportForMetaType(ex, p);
        }

        if (op->format())
        {
            addRuntimeImport("Ice.FormatType", "FormatType", p);
        }
    }

    return false;
}

void
Slice::Python::ImportVisitor::visitSequence(const SequencePtr& p)
{
    // Add import required for the sequence element type.
    addRuntimeImportForMetaType(p->type(), p);
}

void
Slice::Python::ImportVisitor::visitDictionary(const DictionaryPtr& p)
{
    // Add imports required for the dictionary key and value meta types
    addRuntimeImportForMetaType(p->keyType(), p);
    addRuntimeImportForMetaType(p->valueType(), p);
}

void
Slice::Python::ImportVisitor::visitEnum(const EnumPtr& p)
{
    // TODO if a value is initialized with a constant, we need to import the type of the constant.
    addRuntimeImport("enum", "Enum", p);
}

void
Slice::Python::ImportVisitor::visitConst(const ConstPtr& p)
{
    // If the constant value is a Slice enum, we need to import the enum type.
    if (dynamic_pointer_cast<Enum>(p->type()))
    {
        addRuntimeImport(p->type(), p);
    }
}

void
Slice::Python::ImportVisitor::addRuntimeImportForSequence(const SequencePtr& sequence, const ContainedPtr& source)
{
    if (sequence->hasMetadata("python:numpy.ndarray"))
    {
        // Import numpy for using it in the field factory.
        addRuntimeImport("numpy", "", source);
    }
    else if (sequence->hasMetadata("python:array.array"))
    {
        // Import array for using it in the field factory.
        addRuntimeImport("array", "", source);
    }
    else if (sequence->hasMetadata("python:memoryview"))
    {
        // TODO
    }
    else
    {
        // This is required to import the sequence element type in case it is not a built-in type.
        addTypingImport(sequence, source, true);
    }
}

void
Slice::Python::ImportVisitor::addRuntimeImport(
    const SyntaxTreeBasePtr& definition,
    const ContainedPtr& source,
    TypeContext typeContext)
{
    // The module containing the definition we want to import.
    auto definitionModule = getPythonModuleForDefinition(definition);

    // The module importing the definition.
    string sourceModule = getPythonModuleForDefinition(source);

    if (definitionModule == sourceModule)
    {
        // If the definition and source are in the same module, we don't need to import it.
        return;
    }

    auto& allImports = _allImports[sourceModule];

    string name;
    string alias;

    if (auto builtin = dynamic_pointer_cast<Builtin>(definition))
    {
        if (builtin->kind() != Builtin::KindObjectProxy && builtin->kind() != Builtin::KindValue)
        {
            // Builtin types other than ObjectPrx and Value don't need imports.
            return;
        }

        name = builtin->kind() == Builtin::KindObjectProxy ? "ObjectPrx" : "Value";
        alias = getImportAlias(source, allImports, definition);
    }
    else
    {
        auto contained = dynamic_pointer_cast<Contained>(definition);
        assert(contained);

        name = contained->mappedName();
        if ((dynamic_pointer_cast<InterfaceDef>(definition) || dynamic_pointer_cast<InterfaceDecl>(definition)) &&
            typeContext == TypeContext::Proxy)
        {
            name += "Prx";
        }

        alias = getImportAlias(source, allImports, contained->mappedScoped("."), name);
    }

    auto& sourceModuleImports = _runtimeImports[sourceModule];
    auto& definitionImports = sourceModuleImports[definitionModule];

    if (name == alias)
    {
        definitionImports.insert({name, ""});
        allImports[name] = definitionModule;
    }
    else
    {
        definitionImports.insert({name, alias});
        allImports[alias] = definitionModule;
    }
}

void
Slice::Python::ImportVisitor::addRuntimeImport(
    const string& definitionModule,
    const string& definition,
    const ContainedPtr& source)
{
    // The module importing the definition.
    string sourceModule = getPythonModuleForDefinition(source);

    if (definitionModule == sourceModule)
    {
        // If the definition and source are in the same module, we don't need to import it.
        return;
    }

    auto& sourceModuleImports = _runtimeImports[sourceModule];
    if (definition.empty())
    {
        if (sourceModuleImports.find(definitionModule) == sourceModuleImports.end())
        {
            // If the package does not exist, we create an empty map for it.
            sourceModuleImports[definitionModule] = {};
        }
        return;
    }

    auto& definitionImports = sourceModuleImports[definitionModule];

    auto& imports = _allImports[sourceModule];
    string alias = getImportAlias(source, imports, definitionModule, definition);

    if (definition == alias)
    {
        definitionImports.insert({definition, ""});
        imports[definition] = definitionModule;
    }
    else
    {
        definitionImports.insert({definition, alias});
        imports[alias] = definitionModule;
    }
}

void
Slice::Python::ImportVisitor::addTypingImport(
    const string& moduleName,
    const string& definition,
    const ContainedPtr& source)
{
    // The module importing the definition.
    string sourceModule = getPythonModuleForDefinition(source);
    auto& sourceModuleImports = _typingImports[sourceModule];
    auto& definitionImports = sourceModuleImports[moduleName];

    auto& imports = _allImports[sourceModule];
    string alias = getImportAlias(source, imports, moduleName, definition);

    if (definition == alias)
    {
        definitionImports.insert({definition, ""});
        imports[definition] = moduleName;
    }
    else
    {
        definitionImports.insert({definition, alias});
        imports[alias] = moduleName;
    }

    // If we are importing a type with the TypingImport scope, we also need a runtime import for TYPE_CHECKING from
    // typing.
    addRuntimeImport("typing", "TYPE_CHECKING", source);
}

void
Slice::Python::ImportVisitor::addTypingImport(
    const SyntaxTreeBasePtr& definition,
    const ContainedPtr& source,
    bool forMarshaling)
{
    if (auto builtin = dynamic_pointer_cast<Builtin>(definition))
    {
        if (builtin->kind() == Builtin::KindValue)
        {
            addTypingImport("Ice.Value", "Value", source);
        }
        else if (builtin->kind() == Builtin::KindObjectProxy)
        {
            addTypingImport("Ice.ObjectPrx", "ObjectPrx", source);
        }
    }
    else if (auto sequence = dynamic_pointer_cast<Sequence>(definition))
    {
        addTypingImport(sequence->type(), source, forMarshaling);
    }
    else if (auto dictionary = dynamic_pointer_cast<Dictionary>(definition))
    {
        addTypingImport(dictionary->keyType(), source, forMarshaling);
        addTypingImport(dictionary->valueType(), source, forMarshaling);
    }
    else if (auto interface = dynamic_pointer_cast<InterfaceDecl>(definition))
    {
        addTypingImport(interface->mappedScoped("."), interface->mappedName() + "Prx", source);
    }
    else if (auto contained = dynamic_pointer_cast<Contained>(definition))
    {
        addTypingImport(contained->mappedScoped("."), contained->mappedName(), source);
    }
}

void
Slice::Python::ImportVisitor::addTypingImport(const string& packageName, const ContainedPtr& source)
{
    // The module importing the definition.
    string sourceModule = getPythonModuleForDefinition(source);

    auto& sourceModuleImports = _typingImports[sourceModule];
    if (sourceModuleImports.find(packageName) == sourceModuleImports.end())
    {
        // If the package does not exist, we create an empty map for it.
        sourceModuleImports[packageName] = {};
    }

    // If we are importing a type with the TypingImport scope, we also need a runtime import for TYPE_CHECKING from
    // typing.
    addRuntimeImport("typing", "TYPE_CHECKING", source);
}

void
Slice::Python::ImportVisitor::addRuntimeImportForMetaType(
    const SyntaxTreeBasePtr& definition,
    const ContainedPtr& source)
{
    auto builtin = dynamic_pointer_cast<Builtin>(definition);
    if (builtin && builtin->kind() != Builtin::KindObjectProxy && builtin->kind() != Builtin::KindValue)
    {
        // Builtin types other than ObjectPrx and Value don't need imports.
        return;
    }

    // The meta type for a Slice class or interface is always imported from the Xxx_forward module.
    bool isForwardDeclared =
        dynamic_pointer_cast<ClassDecl>(definition) || dynamic_pointer_cast<ClassDef>(definition) ||
        dynamic_pointer_cast<InterfaceDecl>(definition) || dynamic_pointer_cast<InterfaceDef>(definition) || builtin;

    // The module containing the definition we want to import.
    string definitionModule =
        isForwardDeclared ? getPythonModuleForForwardDeclaration(definition) : getPythonModuleForDefinition(definition);

    // The module importing the definition.
    string sourceModule = getPythonModuleForDefinition(source);

    if (definitionModule == sourceModule)
    {
        // If the definition and source are in the same module, we don't need to import it.
        return;
    }

    auto& sourceModuleImports = _runtimeImports[sourceModule];
    auto& definitionImports = sourceModuleImports[definitionModule];
    definitionImports.insert({getMetaType(definition), ""});
}

bool
Slice::Python::PackageVisitor::visitModuleStart(const ModulePtr& p)
{
    string packageName = p->mappedScoped(".");

    // Ensure all parent packages exits, that is necessary to account for modules
    // that are mapped to a nested package using python:identifier metadata.
    vector<string> packageParts;
    IceInternal::splitString(string_view{packageName}, ".", packageParts);
    string current = "";
    for (const auto& part : packageParts)
    {
        current += part + ".";
        if (_imports.find(current) == _imports.end())
        {
            // If the package does not exist, we create an empty map for it.
            _imports[current] = {};
            string currentPath = current;
            replace(currentPath.begin(), currentPath.end(), '.', '/');
            currentPath += "__init__.py";
            _generated[p->unit()->topLevelFile()].insert(currentPath);
        }
    }
    return true;
}

bool
Slice::Python::PackageVisitor::visitClassDefStart(const ClassDefPtr& p)
{
    // Add the class to the package imports.
    addRuntimeImport(p);
    addRuntimeImportForMetaType(p->declaration());
    return false;
}

bool
Slice::Python::PackageVisitor::visitInterfaceDefStart(const InterfaceDefPtr& p)
{
    addRuntimeImport(p);
    addRuntimeImport(p, "Prx");
    addRuntimeImportForMetaType(p->declaration());

    return false;
}

bool
Slice::Python::PackageVisitor::visitStructStart(const StructPtr& p)
{
    addRuntimeImport(p);
    addRuntimeImportForMetaType(p);
    return false;
}

bool
Slice::Python::PackageVisitor::visitExceptionStart(const ExceptionPtr& p)
{
    addRuntimeImport(p);
    addRuntimeImportForMetaType(p);
    return false;
}

void
Slice::Python::PackageVisitor::visitSequence(const SequencePtr& p)
{
    addRuntimeImportForMetaType(p);
}

void
Slice::Python::PackageVisitor::visitDictionary(const DictionaryPtr& p)
{
    addRuntimeImportForMetaType(p);
}

void
Slice::Python::PackageVisitor::visitEnum(const EnumPtr& p)
{
    addRuntimeImport(p);
    addRuntimeImportForMetaType(p);
}

void
Slice::Python::PackageVisitor::visitConst(const ConstPtr& p)
{
    addRuntimeImport(p);
}

void
Slice::Python::PackageVisitor::addRuntimeImport(const ContainedPtr& definition, const string& prefix)
{
    string packageName = definition->mappedScope(".");
    string moduleName = definition->mappedName();
    auto& packageImports = _imports[packageName];
    auto& definitions = packageImports[moduleName];
    definitions.insert(definition->mappedName() + prefix);

    // Add the definition to the list of generated Python modules.
    string modulePath = packageName;
    replace(modulePath.begin(), modulePath.end(), '.', '/');

    _generated[definition->unit()->topLevelFile()].insert(modulePath + moduleName + ".py");
}

void
Slice::Python::PackageVisitor::addRuntimeImportForMetaType(const ContainedPtr& definition)
{
    string packageName = definition->mappedScope(".");
    string moduleName = definition->mappedName();

    // The meta type for Slice classes or interfaces is always imported from the Xxx_forward module containing the
    // forward declaration.
    if (dynamic_pointer_cast<ClassDecl>(definition) || dynamic_pointer_cast<InterfaceDecl>(definition))
    {
        moduleName += "_forward";
    }
    auto& packageImports = _imports[packageName];
    auto& definitions = packageImports[moduleName];
    definitions.insert(getMetaType(definition));

    // Add the definition to the list of generated Python modules.
    replace(packageName.begin(), packageName.end(), '.', '/');
    _generated[definition->unit()->topLevelFile()].insert(packageName + moduleName + ".py");
}

// CodeVisitor implementation.

void
Slice::Python::CodeVisitor::writeOperations(const InterfaceDefPtr& p, Output& out)
{
    const string currentAlias = getImportAlias(p, "Ice.Current", "Current");
    // Emits an abstract method for each operation.
    for (const auto& operation : p->operations())
    {
        const string sliceName = operation->name();
        const string mappedName = operation->mappedName();

        if (operation->hasMarshaledResult())
        {
            string capName = sliceName;
            capName[0] = static_cast<char>(toupper(static_cast<unsigned char>(capName[0])));
            out << sp;
            out << nl << "@staticmethod";
            out << nl << "def " << capName << "MarshaledResult(result, current: " << currentAlias << "):";
            out.inc();
            out << nl << tripleQuotes;
            out << nl << "Immediately marshals the result of an invocation of " << sliceName;
            out << nl << "and returns an object that the servant implementation must return";
            out << nl << "as its result.";
            out << nl;
            out << nl << "Args:";
            out << nl << "  result: The result (or result tuple) of the invocation.";
            out << nl << "  current: The Current object passed to the invocation.";
            out << nl;
            out << nl << "Returns";
            out << nl << "  An object containing the marshaled result.";
            out << nl << tripleQuotes;
            out << nl << "return IcePy.MarshaledResult(result, " << p->mappedName() << "._op_" << sliceName
                << ", current.adapter.getCommunicator()._getImpl(), current.encoding)";
            out.dec();
        }

        out << sp;
        out << nl << "@abstractmethod";
        out << nl << "def " << mappedName << spar << "self";

        for (const auto& param : operation->inParameters())
        {
            out << (param->mappedName() + ": " + typeToTypeHintString(param->type(), param->optional(), p, false));
        }

        const string currentParamName = getEscapedParamName(operation->parameters(), "current");
        out << (currentParamName + ": " + currentAlias);
        out << epar << operationReturnTypeHint(operation, MethodKind::Dispatch) << ":";
        out.inc();

        writeDocstring(operation, MethodKind::Dispatch, out);

        out << nl << "pass";
        out.dec();
    }
}

bool
Slice::Python::CodeVisitor::visitStructStart(const StructPtr& p)
{
    const string name = p->mappedName();
    const DataMemberList members = p->dataMembers();

    _out = std::make_unique<BufferedOutput>();
    auto& out = *_out;

    out << sp;
    out << nl << "@dataclass";
    if (Dictionary::isLegalKeyType(p))
    {
        out << "(order=True, unsafe_hash=True)";
    }

    out << nl << "class " << name << ":";
    out.inc();

    writeDocstring(p->docComment(), members, out);
    return true;
}

void
Slice::Python::CodeVisitor::visitStructEnd(const StructPtr& p)
{
    const string scoped = p->scoped();
    const string name = p->mappedName();
    const string metaTypeName = getMetaType(p);

    assert(_out);
    auto& out = *_out;

    out.dec();

    // Emit the type information.
    out << sp;
    out << nl << metaTypeName << " = IcePy.defineStruct(";
    out.inc();
    out << nl << "\"" << scoped << "\",";
    out << nl << name << ",";
    out << nl;
    writeMetadata(p->getMetadata(), out);
    out << ",";

    writeMetaTypeDataMembers(p, p->dataMembers(), out);
    out << ")";
    out.dec();

    out << sp;
    out << nl << "__all__ = [\"" << name << "\", \"" << metaTypeName << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
    _out.reset();
}

bool
Slice::Python::CodeVisitor::visitClassDefStart(const ClassDefPtr& p)
{
    const string scoped = p->scoped();
    const string metaType = getMetaType(p);
    const string valueName = p->mappedName();
    const ClassDefPtr base = p->base();
    const DataMemberList members = p->dataMembers();

    // Emit a forward declaration for the class meta-type.
    BufferedOutput outF;
    outF << nl << metaType << " = IcePy.declareValue(\"" << p->scoped() << "\")";

    outF << sp;
    outF << nl << "__all__ = [\"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p->declaration(), outF.str()));

    // Emit the class definition.
    _out = std::make_unique<BufferedOutput>();
    auto& out = *_out;

    // Equality for Slice classes is reference equality, so we disable the dataclass eq.
    out << nl << "@dataclass(eq=False)";
    out << nl << "class " << valueName << '('
        << (base ? getImportAlias(p, base) : getImportAlias(p, "Ice.Value", "Value")) << "):";
    out.inc();

    writeDocstring(p->docComment(), members, out);
    return true;
}

void
Slice::Python::CodeVisitor::visitClassDefEnd(const ClassDefPtr& p)
{
    const string scoped = p->scoped();
    const string metaType = getMetaType(p);
    const string valueName = p->mappedName();
    const ClassDefPtr base = p->base();

    assert(_out);
    auto& out = *_out;

    // ice_staticId
    out << sp;
    out << nl << "@staticmethod";
    out << nl << "def ice_staticId() -> str:";
    out.inc();
    out << nl << "return \"" << scoped << "\"";
    out.dec();

    out.dec();

    out << sp;
    out << nl << metaType << " = IcePy.defineValue(";
    out.inc();
    out << nl << "\"" << scoped << "\",";
    out << nl << valueName << ",";
    out << nl << p->compactId() << ",";
    out << nl;
    writeMetadata(p->getMetadata(), out);
    out << ",";
    out << nl << "False,";
    out << nl << (base ? getMetaType(base) : "None") << ",";

    writeMetaTypeDataMembers(p, p->dataMembers(), out);

    out << ")";
    out.dec();

    // Use setattr to set the _ice_type attribute on the class. Linting tools will complain about this if we
    // don't use setattr, as the _ice_type attribute is not defined in the class body.
    out << sp;
    out << nl << "setattr(" << valueName << ", '_ice_type', " << metaType << ")";

    out << sp;
    out << nl << "__all__ = [\"" << valueName << "\", \"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
    _out.reset();
}

bool
Slice::Python::CodeVisitor::visitExceptionStart(const ExceptionPtr& p)
{
    const string scoped = p->scoped();
    const string name = p->mappedName();
    const ExceptionPtr base = p->base();

    _out = std::make_unique<BufferedOutput>();
    auto& out = *_out;

    const DataMemberList members = p->dataMembers();

    out << sp;
    out << nl << "@dataclass";
    out << nl << "class " << name << '('
        << (base ? getImportAlias(p, base) : getImportAlias(p, "Ice.UserException", "UserException")) << "):";
    out.inc();
    writeDocstring(p->docComment(), members, out);

    return true;
}

void
Slice::Python::CodeVisitor::visitExceptionEnd(const ExceptionPtr& p)
{
    const string scoped = p->scoped();
    const string name = p->mappedName();
    const ExceptionPtr base = p->base();

    assert(_out);
    auto& out = *_out;

    // _ice_id
    out << sp;
    out << nl << "_ice_id = \"" << scoped << "\"";

    out.dec();

    // Emit the type information.
    string metaType = getMetaType(p);
    out << sp;
    out << nl << metaType << " = IcePy.defineException(";
    out.inc();
    out << nl << "\"" << scoped << "\",";
    out << nl << name << ",";
    out << nl;
    writeMetadata(p->getMetadata(), out);
    out << ",";
    out << nl << (base ? getMetaType(base) : "None") << ",";

    writeMetaTypeDataMembers(p, p->dataMembers(), out);

    out << ")";
    out.dec();

    // Use setattr to set the _ice_type attribute on the exception. Linting tools will complain about this if we
    // don't use setattr, as the _ice_type attribute is not defined in the exception body.
    out << sp;
    out << nl << "setattr(" << name << ", '_ice_type', " << metaType << ")";

    out << sp;
    out << nl << "__all__ = [\"" << name << "\", \"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
    _out.reset();
}

void
Slice::Python::CodeVisitor::visitDataMember(const DataMemberPtr& p)
{
    assert(_out);
    auto& out = *_out;
    auto parent = dynamic_pointer_cast<Contained>(p->container());

    out << nl << p->mappedName() << ": " << typeToTypeHintString(p->type(), p->optional(), parent, false);

    if (p->defaultValue())
    {
        out << " = ";
        writeConstantValue(parent, p->type(), p->defaultValueType(), *p->defaultValue(), out);
    }
    else if (p->optional())
    {
        out << " = None";
    }
    else if (auto builtin = dynamic_pointer_cast<Builtin>(p->type()))
    {
        static constexpr string_view builtinTable[] = {
            "0",     // Builtin::KindByte
            "False", // Builtin::KindBool
            "0",     // Builtin::KindShort
            "0",     // Builtin::KindInt
            "0",     // Builtin::KindLong
            "0.0",   // Builtin::KindFloat
            "0.0",   // Builtin::KindDouble
            R"("")", // Builtin::KindString
            "None",  // Builtin::KindObjectProxy.
            "None"}; // Builtin::KindValue.
        out << " = " << builtinTable[builtin->kind()];
    }
    else if (auto enumeration = dynamic_pointer_cast<Enum>(p->type()))
    {
        out << " = " << getImportAlias(parent, enumeration) << "." + enumeration->enumerators().front()->mappedName();
    }
    else if (dynamic_pointer_cast<Struct>(p->type()))
    {
        out << " = field(default_factory=" << getImportAlias(parent, p->type()) << ")";
    }
    else if (auto seq = dynamic_pointer_cast<Sequence>(p->type()))
    {
        auto elementType = dynamic_pointer_cast<Builtin>(seq->type());
        bool isByteSequence = elementType && elementType->kind() == Builtin::KindByte;

        out << " = field(default_factory=";
        if (seq->hasMetadata("python:list"))
        {
            out << "list";
        }
        else if (seq->hasMetadata("python:tuple"))
        {
            out << "tuple";
        }
        else if (seq->hasMetadata("python:numpy.ndarray"))
        {
            assert(elementType && elementType->kind() <= Builtin::KindDouble);
            static const char* builtinTable[] = {"int8", "bool", "int16", "int32", "int64", "float32", "float64"};
            out << "lambda: numpy.empty(0, numpy." << builtinTable[elementType->kind()] << ")";
        }
        else if (seq->hasMetadata("python:array.array"))
        {
            assert(elementType && elementType->kind() <= Builtin::KindDouble);
            static const char* builtinTable[] = {"b", "b", "h", "i", "q", "f", "d"};
            out << "lambda: array.array('" << builtinTable[elementType->kind()] << "')";
        }
        else if (isByteSequence)
        {
            out << "bytes";
        }
        else
        {
            out << "list";
        }
        out << ")";
    }
    else if (dynamic_pointer_cast<Dictionary>(p->type()))
    {
        out << " = field(default_factory=dict)";
    }
    else
    {
        out << " = None";
    }
}

bool
Slice::Python::CodeVisitor::visitInterfaceDefStart(const InterfaceDefPtr& p)
{
    string scoped = p->scoped();
    string className = p->mappedName();
    string prxName = className + "Prx";
    string metaType = getMetaType(p);
    InterfaceList bases = p->bases();
    OperationList operations = p->operations();

    // Emit a forward declarations for the proxy meta type.
    BufferedOutput outF;
    outF << nl << metaType << " = IcePy.declareProxy(\"" << scoped << "\")";
    outF << sp;
    outF << nl << "__all__ = [\"" << metaType << "\"]";
    _codeFragments.push_back(createCodeFragmentForPythonModule(p->declaration(), outF.str()));

    // Emit the proxy class.
    BufferedOutput out;
    out << sp;
    out << nl << "class " << prxName << spar;

    if (bases.empty())
    {
        out << getImportAlias(p, "Ice.ObjectPrx", "ObjectPrx");
    }
    else
    {
        for (const auto& base : bases)
        {
            out << getImportAlias(p, base->mappedScoped("."), base->mappedName() + "Prx");
        }
    }
    out << epar << ":";
    out.inc();

    const string communicatorAlias = getImportAlias(p, "Ice.Communicator", "Communicator");
    const string objectPrxAlias = getImportAlias(p, "Ice.ObjectPrx", "ObjectPrx");
    const string currentAlias = getImportAlias(p, "Ice.Current", "Current");
    const string formatTypeAlias = getImportAlias(p, "Ice.FormatType", "FormatType");

    for (const auto& operation : operations)
    {
        const string opName = operation->name();
        string mappedOpName = operation->mappedName();
        if (mappedOpName == "checkedCast" || mappedOpName == "uncheckedCast")
        {
            mappedOpName.insert(0, "_");
        }
        TypePtr ret = operation->returnType();
        ParameterList paramList = operation->parameters();
        string inParams;
        string inParamsDecl;

        // Find the last required parameter, all optional parameters after the last required parameter will use
        // None as the default.
        ParameterPtr lastRequiredParameter;
        for (const auto& q : operation->inParameters())
        {
            if (!q->optional())
            {
                lastRequiredParameter = q;
            }
        }

        bool afterLastRequiredParameter = lastRequiredParameter == nullptr;
        for (const auto& q : operation->inParameters())
        {
            if (!inParams.empty())
            {
                inParams.append(", ");
                inParamsDecl.append(", ");
            }
            string param = q->mappedName();
            inParams.append(param);
            param += ": " + typeToTypeHintString(q->type(), q->optional(), p, true);
            if (afterLastRequiredParameter)
            {
                param += " = None";
            }
            inParamsDecl.append(param);

            if (q == lastRequiredParameter)
            {
                afterLastRequiredParameter = true;
            }
        }

        out << sp;
        out << nl << "def " << mappedOpName << "(self";
        if (!inParamsDecl.empty())
        {
            out << ", " << inParamsDecl;
        }
        const string contextParamName = getEscapedParamName(operation->parameters(), "context");
        out << ", " << contextParamName << ": dict[str, str] | None = None)"
            << operationReturnTypeHint(operation, MethodKind::SyncInvocation) << ":";
        out.inc();
        writeDocstring(operation, MethodKind::SyncInvocation, out);
        out << nl << "return " << className << "._op_" << opName << ".invoke(self, ((" << inParams;
        if (!inParams.empty() && inParams.find(',') == string::npos)
        {
            out << ", ";
        }
        out << "), " << contextParamName << "))";
        out.dec();

        // Async operations.
        out << sp;
        out << nl << "def " << mappedOpName << "Async(self";
        if (!inParams.empty())
        {
            out << ", " << inParamsDecl;
        }
        out << ", " << contextParamName << ": dict[str, str] | None = None)"
            << operationReturnTypeHint(operation, MethodKind::AsyncInvocation) << ":";
        out.inc();
        writeDocstring(operation, MethodKind::AsyncInvocation, out);
        out << nl << "return " << className << "._op_" << opName << ".invokeAsync(self, ((" << inParams;
        if (!inParams.empty() && inParams.find(',') == string::npos)
        {
            out << ", ";
        }
        out << "), " << contextParamName << "))";
        out.dec();
    }

    const string prxTypeHint = typeToTypeHintString(p->declaration(), false, p, false);

    out << sp;
    out << nl << "@staticmethod";
    out << nl << "def checkedCast(";
    out.inc();
    out << nl << "proxy: " << objectPrxAlias << " | None,";
    out << nl << "facet: str | None = None,";
    out << nl << "context: dict[str, str] | None = None";
    out.dec();
    out << nl << ") -> " << prxTypeHint << ":";
    out.inc();
    out << nl << "return checkedCast(" << prxName << ", proxy, facet, context)";
    out.dec();

    out << sp;
    out << nl << "@staticmethod";
    out << nl << "def checkedCastAsync(";
    out.inc();
    out << nl << "proxy: " << objectPrxAlias << " | None,";
    out << nl << "facet: str | None = None,";
    out << nl << "context: dict[str, str] | None = None";
    out.dec();
    out << nl << ") -> Awaitable[" << prxName << " | None ]:";
    out.inc();
    out << nl << "return checkedCastAsync(" << prxName << ", proxy, facet, context)";
    out.dec();

    out << sp << nl << "@overload";
    out << nl << "@staticmethod";
    out << nl << "def uncheckedCast(proxy: " << objectPrxAlias << ", facet: str | None = None) -> " << prxName << ":";
    out.inc();
    out << nl << "...";
    out.dec();

    out << sp << nl << "@overload";
    out << nl << "@staticmethod";
    out << nl << "def uncheckedCast(proxy: None, facet: str | None = None) -> None:";
    out.inc();
    out << nl << "...";
    out.dec();

    out << sp << nl << "@staticmethod";
    out << nl << "def uncheckedCast(proxy: " << objectPrxAlias << " | None, facet: str | None = None) -> "
        << prxTypeHint << ":";
    out.inc();
    out << nl << "return uncheckedCast(" << prxName << ", proxy, facet)";
    out.dec();

    // ice_staticId
    out << sp;
    out << nl << "@staticmethod";
    out << nl << "def ice_staticId() -> str:";
    out.inc();
    out << nl << "return \"" << scoped << "\"";
    out.dec();

    out.dec(); // end prx class

    out << sp;
    out << nl << "IcePy.defineProxy(\"" << scoped << "\", " << prxName << ")";

    // Emit the servant class (to the same code fragment as the proxy class).
    out << sp;
    out << nl << "class " << className;
    out << spar;
    if (bases.empty())
    {
        out << getImportAlias(p, "Ice.Object", "Object");
    }
    else
    {
        for (const auto& base : bases)
        {
            out << getImportAlias(p, base);
        }
    }
    out << "ABC" << epar << ':';
    out.inc();

    out << sp;
    // Declare _ice_ids class variable to hold the ice_ids.
    out << nl << "_ice_ids: Sequence[str] = (";
    auto ids = p->ids();
    for (const auto& id : ids)
    {
        out << "\"" << id << "\", ";
    }
    out << ")";

    // Pre-declare the _op_ methods
    for (const auto& operation : operations)
    {
        out << nl << "_op_" << operation->name() << ": IcePy.Operation";
    }

    // ice_ids is implemented by the base Ice.Object class using the `_ice_ids` class variable.

    // ice_staticId
    out << sp;
    out << nl << "@staticmethod";
    out << nl << "def ice_staticId() -> str:";
    out.inc();
    out << nl << "return \"" << scoped << "\"";
    out.dec();

    writeOperations(p, out);

    out.dec();

    //
    // Define each operation. The arguments to the IcePy.Operation constructor are:
    //
    // "sliceOpName", "mappedOpName", Mode, AMD, Format, Metadata, (InParams), (OutParams), ReturnParam, (Exceptions)
    //
    // where InParams and OutParams are tuples of type descriptions, and Exceptions
    // is a tuple of exception type ids.

    for (const auto& operation : operations)
    {
        string format;
        optional<FormatType> opFormat = operation->format();
        if (opFormat)
        {
            switch (*opFormat)
            {
                case CompactFormat:
                    format = formatTypeAlias + ".CompactFormat";
                    break;
                case SlicedFormat:
                    format = formatTypeAlias + ".SlicedFormat";
                    break;
                default:
                    assert(false);
            }
        }
        else
        {
            format = "None";
        }

        const string sliceName = operation->name();

        const string operationMode = operation->mode() == Operation::Mode::Normal ? "Normal" : "Idempotent";

        out << sp;
        out << nl << className << "._op_" << sliceName << " = IcePy.Operation(";
        out.inc();
        out << nl << "\"" << sliceName << "\",";
        out << nl << "\"" << operation->mappedName() << "\",";
        out << nl << getImportAlias(p, "Ice.OperationMode", "OperationMode") << "." << operationMode << ",";
        out << nl << format << ",";
        out << nl;
        writeMetadata(operation->getMetadata(), out);
        out << ",";
        out << nl << "(";
        for (const auto& param : operation->inParameters())
        {
            if (param != operation->inParameters().front())
            {
                out << ", ";
            }
            out << '(';
            writeMetadata(param->getMetadata(), out);
            out << ", " << getMetaType(param->type());
            out << ", ";
            if (param->optional())
            {
                out << "True, " << param->tag();
            }
            else
            {
                out << "False, 0";
            }
            out << ')';
        }

        // A trailing comma to ensure that the output is interpreted as a Python tuple.
        if (operation->inParameters().size() == 1)
        {
            out << ',';
        }
        out << "),";
        out << nl << "(";
        for (const auto& param : operation->outParameters())
        {
            if (param != operation->outParameters().front())
            {
                out << ", ";
            }
            out << '(';
            writeMetadata(param->getMetadata(), out);
            out << ", " << getMetaType(param->type());
            out << ", ";
            if (param->optional())
            {
                out << "True, " << param->tag();
            }
            else
            {
                out << "False, 0";
            }
            out << ')';
        }

        // A trailing command to ensure that the outut is interpreted as a Python tuple.
        if (operation->outParameters().size() == 1)
        {
            out << ',';
        }
        out << "),";

        out << nl;
        TypePtr returnType = operation->returnType();
        if (returnType)
        {
            // The return type has the same format as an in/out parameter:
            //
            // Metadata, Type, Optional?, OptionalTag
            out << "((), " << getMetaType(returnType) << ", ";
            if (operation->returnIsOptional())
            {
                out << "True, " << operation->returnTag();
            }
            else
            {
                out << "False, 0";
            }
            out << ')';
        }
        else
        {
            out << "None";
        }
        out << ",";
        out << nl << "(";
        for (const auto& ex : operation->throws())
        {
            if (ex != operation->throws().front())
            {
                out << ", ";
            }
            out << getMetaType(ex);
        }

        // A trailing command to ensure that the outut is interpreted as a Python tuple.
        if (operation->throws().size() == 1)
        {
            out << ',';
        }
        out << "))";
        out.dec();

        if (operation->isDeprecated())
        {
            // Get the deprecation reason if present, or default to an empty string.
            string reason = operation->getDeprecationReason().value_or("");
            out << nl << className << "._op_" << sliceName << ".deprecate(\"" << reason << "\")";
        }
    }

    out << sp;
    out << nl << "__all__ = [\"" << className << "\", \"" << prxName << "\", \"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
    return false;
}

void
Slice::Python::CodeVisitor::visitSequence(const SequencePtr& p)
{
    string metaType = getMetaType(p);

    BufferedOutput out;
    out << nl << metaType << " = IcePy.defineSequence(\"" << p->scoped() << "\", ";
    writeMetadata(p->getMetadata(), out);
    out << ", " << getMetaType(p->type());
    out << ")";

    out << sp;
    out << nl << "__all__ = [\"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
}

void
Slice::Python::CodeVisitor::visitDictionary(const DictionaryPtr& p)
{
    BufferedOutput out;
    string metaType = getMetaType(p);
    out << nl << metaType << " = IcePy.defineDictionary(\"" << p->scoped() << "\", ";
    writeMetadata(p->getMetadata(), out);
    out << ", " << getMetaType(p->keyType()) << ", " << getMetaType(p->valueType()) << ")";

    out << sp;
    out << nl << "__all__ = [\"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
}

void
Slice::Python::CodeVisitor::visitEnum(const EnumPtr& p)
{
    string scoped = p->scoped();
    string name = p->mappedName();
    string metaType = getMetaType(p);
    EnumeratorList enumerators = p->enumerators();

    BufferedOutput out;
    out << nl << "class " << name << "(Enum):";
    out.inc();

    writeDocstring(p->docComment(), p, out);

    out << nl;
    for (const auto& enumerator : enumerators)
    {
        out << nl << enumerator->mappedName() << " = " << enumerator->value();
    }

    out.dec();

    // Meta type definition.
    out << sp;
    out << nl << metaType << " = IcePy.defineEnum(";
    out.inc();
    out << nl << "\"" << scoped << "\",";
    out << nl << name << ",";
    out << nl;
    writeMetadata(p->getMetadata(), out);
    out << ",";
    out << nl << "{";
    out.inc();
    for (const auto& enumerator : enumerators)
    {
        out << nl << to_string(enumerator->value()) << ": " << name << "." << enumerator->mappedName() << ",";
    }
    out.dec();
    out << nl << "}";
    out.dec();
    out << nl << ")";

    out << sp;
    out << nl << "__all__ = [\"" << name << "\", \"" << metaType << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
}

void
Slice::Python::CodeVisitor::visitConst(const ConstPtr& p)
{
    string name = p->mappedName();
    BufferedOutput out;
    out << sp;
    out << nl << name << " = ";
    writeConstantValue(p, p->type(), p->valueType(), p->value(), out);

    out << sp;
    out << nl << "__all__ = [\"" << name << "\"]";

    _codeFragments.push_back(createCodeFragmentForPythonModule(p, out.str()));
}

void
Slice::Python::CodeVisitor::writeMetaTypeDataMembers(
    const ContainedPtr& parent,
    const DataMemberList& members,
    Output& out)
{
    bool includeOptional = !dynamic_pointer_cast<Struct>(parent);
    out << nl << "(";

    if (members.size() > 1)
    {
        out.inc();
        out << nl;
    }

    bool isFirst = true;
    for (const auto& member : members)
    {
        if (!isFirst)
        {
            out << ',';
            out << nl;
        }
        isFirst = false;
        out << "(\"" << member->mappedName() << "\", ";
        writeMetadata(member->getMetadata(), out);
        out << ", " << getMetaType(member->type());
        if (includeOptional)
        {
            out << ", " << (member->optional() ? "True" : "False");
            out << ", " << (member->optional() ? member->tag() : 0);
        }
        out << ')';
    }

    // A trailing comma is required for Python tuples with a single element.
    if (members.size() == 1)
    {
        out << ',';
    }

    if (members.size() > 1)
    {
        out.dec();
        out << nl;
    }

    out << ")";
}

void
Slice::Python::CodeVisitor::writeMetadata(const MetadataList& metadata, Output& out)
{
    MetadataList pythonMetadata = metadata;
    auto newEnd = remove_if(
        pythonMetadata.begin(),
        pythonMetadata.end(),
        [](const MetadataPtr& meta) { return meta->directive().find("python:") != 0; });
    pythonMetadata.erase(newEnd, pythonMetadata.end());

    out << '(';

    for (const auto& meta : pythonMetadata)
    {
        out << "\"" << *meta << "\"";
        if (meta != pythonMetadata.back() || pythonMetadata.size() == 1)
        {
            out << ", ";
        }
    }

    out << ')';
}

void
Slice::Python::CodeVisitor::writeConstantValue(
    const ContainedPtr& source,
    const TypePtr& type,
    const SyntaxTreeBasePtr& valueType,
    const string& value,
    Output& out)
{
    if (auto constant = dynamic_pointer_cast<Const>(valueType))
    {
        out << getImportAlias(source, constant);
    }
    else if (auto builtin = dynamic_pointer_cast<Slice::Builtin>(type))
    {
        switch (builtin->kind())
        {
            case Builtin::KindBool:
            {
                out << (value == "true" ? "True" : "False");
                break;
            }
            case Builtin::KindByte:
            case Builtin::KindShort:
            case Builtin::KindInt:
            case Builtin::KindFloat:
            case Builtin::KindDouble:
            case Builtin::KindLong:
            {
                out << value;
                break;
            }
            case Builtin::KindString:
            {
                const string controlChars = "\a\b\f\n\r\t\v";
                const unsigned char cutOff = 0;

                out << "\"" << toStringLiteral(value, controlChars, "", UCN, cutOff) << "\"";
                break;
            }
            case Builtin::KindValue:
            case Builtin::KindObjectProxy:
                assert(false);
        }
    }
    else if (auto enumeration = dynamic_pointer_cast<Slice::Enum>(type))
    {
        EnumeratorPtr enumerator = dynamic_pointer_cast<Enumerator>(valueType);
        assert(enumerator);
        out << getImportAlias(source, enumeration) << "." << enumerator->mappedName();
    }
    else
    {
        assert(false); // Unknown const type.
    }
}

// Get a list of all definitions exported for the Python module corresponding to the given Slice definition.
vector<string>
Slice::Python::getAll(const ContainedPtr& definition)
{
    vector<string> all;

    all.push_back(definition->mappedName());

    if (dynamic_pointer_cast<InterfaceDecl>(definition) || dynamic_pointer_cast<InterfaceDef>(definition))
    {
        all.push_back(definition->mappedName() + "Prx");
    }

    if (dynamic_pointer_cast<Type>(definition) || dynamic_pointer_cast<Exception>(definition) ||
        dynamic_pointer_cast<InterfaceDef>(definition))
    {
        // for types and exceptions, we also export the meta type.
        all.push_back(getMetaType(definition));
    }
    return all;
}

void
Slice::Python::CodeVisitor::writeRemarksDocComment(const StringList& remarks, bool needsNewline, Output& out)
{
    if (!remarks.empty())
    {
        if (needsNewline)
        {
            out << nl;
        }
        out << nl << "Notes";
        out << nl << "-----";
        for (const auto& line : remarks)
        {
            out << nl << "    " << line;
        }
    }
}

void
Slice::Python::CodeVisitor::writeDocstring(const optional<DocComment>& comment, const string& prefix, Output& out)
{
    if (!comment)
    {
        return;
    }

    const StringList& overview = comment->overview();
    const StringList& remarks = comment->remarks();
    if (overview.empty() && remarks.empty())
    {
        return;
    }

    out << nl << prefix << tripleQuotes;
    for (const auto& line : overview)
    {
        out << nl << line;
    }

    writeRemarksDocComment(remarks, !overview.empty(), out);

    out << nl << tripleQuotes;
}

void
Slice::Python::CodeVisitor::writeDocstring(
    const optional<DocComment>& comment,
    const DataMemberList& members,
    Output& out)
{
    if (!comment)
    {
        return;
    }

    // Collect docstrings (if any) for the members.
    map<string, list<string>> docs;
    for (const auto& member : members)
    {
        if (const auto& memberDoc = member->docComment())
        {
            const StringList& memberOverview = memberDoc->overview();
            if (!memberOverview.empty())
            {
                docs[member->name()] = memberOverview;
            }
        }
    }

    const StringList& overview = comment->overview();
    const StringList& remarks = comment->remarks();
    if (overview.empty() && remarks.empty() && docs.empty())
    {
        return;
    }

    out << nl << tripleQuotes;

    for (const auto& line : overview)
    {
        out << nl << line;
    }

    // Only emit members if there's a docstring for at least one member.
    if (!docs.empty())
    {
        if (!overview.empty())
        {
            out << nl;
        }
        out << nl << "Attributes";
        out << nl << "----------";
        for (const auto& member : members)
        {
            out << nl << member->mappedName() << " : "
                << typeToTypeHintString(
                       member->type(),
                       member->optional(),
                       dynamic_pointer_cast<Contained>(member->container()),
                       false);
            auto p = docs.find(member->name());
            if (p != docs.end())
            {
                for (const auto& line : p->second)
                {
                    out << nl << "    " << line;
                }
            }
        }
    }

    writeRemarksDocComment(remarks, !overview.empty() || !docs.empty(), out);

    out << nl << tripleQuotes;
}

void
Slice::Python::CodeVisitor::writeDocstring(const optional<DocComment>& comment, const EnumPtr& enumeration, Output& out)
{
    if (!comment)
    {
        return;
    }

    // Collect docstrings (if any) for the enumerators.
    const EnumeratorList& enumerators = enumeration->enumerators();
    map<string, list<string>> docs;
    for (const auto& enumerator : enumerators)
    {
        if (const auto& enumeratorDoc = enumerator->docComment())
        {
            const StringList& enumeratorOverview = enumeratorDoc->overview();
            if (!enumeratorOverview.empty())
            {
                docs[enumerator->name()] = enumeratorOverview;
            }
        }
    }

    const StringList& overview = comment->overview();
    const StringList& remarks = comment->remarks();
    if (overview.empty() && remarks.empty() && docs.empty())
    {
        return;
    }

    out << nl << tripleQuotes;

    for (const auto& line : overview)
    {
        out << nl << line;
    }

    // Only emit enumerators if there's a docstring for at least one enumerator.
    if (!docs.empty())
    {
        if (!overview.empty())
        {
            out << nl;
        }
        out << nl << "Enumerators:";
        for (const auto& enumerator : enumerators)
        {
            out << nl << nl << "- " << enumerator->mappedName();
            auto p = docs.find(enumerator->name());
            if (p != docs.end())
            {
                out << ":"; // Only emit a trailing ':' if there's documentation to emit for it.
                for (const auto& line : p->second)
                {
                    out << nl << "    " << line;
                }
            }
        }
    }

    writeRemarksDocComment(remarks, !overview.empty() || !docs.empty(), out);

    out << nl << tripleQuotes;
}

void
Slice::Python::CodeVisitor::writeDocstring(const OperationPtr& op, MethodKind methodKind, Output& out)
{
    const optional<DocComment>& comment = op->docComment();
    if (!comment)
    {
        return;
    }

    auto p = op->interface();

    TypePtr returnType = op->returnType();
    ParameterList params = op->parameters();
    ParameterList inParams = op->inParameters();
    ParameterList outParams = op->outParameters();

    const StringList& overview = comment->overview();
    const StringList& remarks = comment->remarks();
    const StringList& returnsDoc = comment->returns();
    const auto& parametersDoc = comment->parameters();
    const auto& exceptionsDoc = comment->exceptions();

    if (overview.empty() && remarks.empty())
    {
        if ((methodKind == MethodKind::SyncInvocation || methodKind == MethodKind::Dispatch) && parametersDoc.empty() &&
            exceptionsDoc.empty() && returnsDoc.empty())
        {
            return;
        }
        else if (methodKind == MethodKind::AsyncInvocation && inParams.empty())
        {
            return;
        }
        else if (methodKind == MethodKind::Dispatch && inParams.empty() && exceptionsDoc.empty())
        {
            return;
        }
    }

    // Emit the general description.
    out << nl << tripleQuotes;
    for (const string& line : overview)
    {
        out << nl << line;
    }

    // Emit arguments.
    bool needArgs = false;
    switch (methodKind)
    {
        case MethodKind::SyncInvocation:
        case MethodKind::AsyncInvocation:
        case MethodKind::Dispatch:
            needArgs = true;
            break;
    }

    if (needArgs)
    {
        if (!overview.empty())
        {
            out << nl;
        }

        out << nl << "Parameters";
        out << nl << "----------";
        for (const auto& param : inParams)
        {
            out << nl << param->mappedName() << " : "
                << typeToTypeHintString(param->type(), param->optional(), p, methodKind != MethodKind::Dispatch);
            const auto r = parametersDoc.find(param->name());
            if (r != parametersDoc.end())
            {
                for (const auto& line : r->second)
                {
                    out << nl << "    " << line;
                }
            }
        }

        if (methodKind == MethodKind::SyncInvocation || methodKind == MethodKind::AsyncInvocation)
        {
            const string contextParamName = getEscapedParamName(op->parameters(), "context");
            out << nl << contextParamName << " : dict[str, str]";
            out << nl << "    The request context for the invocation.";
        }

        if (methodKind == MethodKind::Dispatch)
        {
            const string currentParamName = getEscapedParamName(op->parameters(), "current");
            out << nl << currentParamName << " : Ice.Current";
            out << nl << "    The Current object for the dispatch.";
        }
    }

    // Emit return value(s).
    bool hasReturnValue = false;
    if (!op->returnsAnyValues() && (methodKind == MethodKind::AsyncInvocation || methodKind == MethodKind::Dispatch))
    {
        hasReturnValue = true;
        if (!overview.empty() || needArgs)
        {
            out << nl;
        }
        out << nl << "Returns";
        out << nl << "-------";
        out << nl << returnTypeHint(op, methodKind);
        if (methodKind == MethodKind::AsyncInvocation)
        {
            out << nl << "    An awaitable that is completed when the invocation completes.";
        }
        else if (methodKind == MethodKind::Dispatch)
        {
            out << nl << "    None or an awaitable that completes when the dispatch completes.";
        }
    }
    else if (op->returnsAnyValues())
    {
        hasReturnValue = true;
        if (!overview.empty() || needArgs)
        {
            out << nl;
        }
        out << nl << "Returns";
        out << nl << "-------";
        out << nl << returnTypeHint(op, methodKind);

        if (op->returnsMultipleValues())
        {
            out << nl;
            out << nl << "    A tuple containing:";
            if (returnType)
            {
                out << nl << "        - "
                    << typeToTypeHintString(returnType, op->returnIsOptional(), p, methodKind == MethodKind::Dispatch);
                bool firstLine = true;
                for (const string& line : returnsDoc)
                {
                    if (firstLine)
                    {
                        firstLine = false;
                        out << " " << line;
                    }
                    else
                    {
                        out << nl << "          " << line;
                    }
                }
            }

            for (const auto& param : outParams)
            {
                out << nl << "        - "
                    << typeToTypeHintString(param->type(), param->optional(), p, methodKind == MethodKind::Dispatch);
                const auto r = parametersDoc.find(param->name());
                if (r != parametersDoc.end())
                {
                    bool firstLine = true;
                    for (const string& line : r->second)
                    {
                        if (firstLine)
                        {
                            firstLine = false;
                            out << " " << line;
                        }
                        else
                        {
                            out << nl << "          " << line;
                        }
                    }
                }
            }
        }
        else if (returnType)
        {
            for (const string& line : returnsDoc)
            {
                out << nl << "    " << line;
            }
        }
        else if (!outParams.empty())
        {
            assert(outParams.size() == 1);
            const auto& param = outParams.front();
            out << nl << typeToTypeHintString(param->type(), param->optional(), p, methodKind == MethodKind::Dispatch);
            const auto r = parametersDoc.find(param->name());
            if (r != parametersDoc.end())
            {
                for (const string& line : r->second)
                {
                    out << nl << "    " << line;
                }
            }
        }
    }

    // Emit exceptions.
    if ((methodKind == MethodKind::SyncInvocation || methodKind == MethodKind::Dispatch) && !exceptionsDoc.empty())
    {
        if (!overview.empty() || needArgs || hasReturnValue)
        {
            out << nl;
        }
        out << nl << "Raises";
        out << nl << "------";
        for (const auto& [exception, exceptionDescription] : exceptionsDoc)
        {
            out << nl << exception;
            for (const auto& line : exceptionDescription)
            {
                out << nl << "    " << line;
            }
        }
    }

    writeRemarksDocComment(remarks, true, out);

    out << nl << tripleQuotes;
}

string
Slice::Python::CodeVisitor::getImportAlias(const ContainedPtr& source, const SyntaxTreeBasePtr& definition)
{
    string sourceModule = getPythonModuleForDefinition(source);
    auto& imports = _allImports[sourceModule];
    return Slice::Python::getImportAlias(source, imports, definition);
}

string
Slice::Python::CodeVisitor::getImportAlias(
    const ContainedPtr& source,
    const string& moduleName,
    const string& definitionName)
{
    string sourceModule = getPythonModuleForDefinition(source);
    auto& imports = _allImports[sourceModule];
    return Slice::Python::getImportAlias(source, imports, moduleName, definitionName);
}

namespace
{
    void writeImports(
        const Python::ModuleImportsMap& runtimeImports,
        const Python::ModuleImportsMap& typingImports,
        Output& out)
    {
        out << sp;
        out << nl << "from __future__ import annotations";
        out << nl << "import IcePy";

        set<string> allImports;

        // Write the runtime imports.
        for (const auto& [moduleName, definitions] : runtimeImports)
        {
            out << sp;
            if (!definitions.empty())
            {
                for (const auto& [name, alias] : definitions)
                {
                    out << nl << "from " << moduleName << " import " << name;
                    if (!alias.empty())
                    {
                        out << " as " << alias;
                        allImports.insert(alias);
                    }
                    else
                    {
                        allImports.insert(name);
                    }
                }
            }
            else
            {
                // If there are no definitions, we just import the module.
                out << nl << "import " << moduleName;
                allImports.insert(moduleName);
            }
        }

        // Write typing imports
        if (!typingImports.empty())
        {
            Python::BufferedOutput outT;
            outT << sp;
            outT << nl << "if TYPE_CHECKING:";
            outT.inc();
            bool hasTypingImports = false;
            for (const auto& [moduleName, definitions] : typingImports)
            {
                if (!definitions.empty())
                {
                    for (const auto& [name, alias] : definitions)
                    {
                        bool allreadyImported = !allImports.insert(alias.empty() ? name : alias).second;

                        if (allreadyImported)
                        {
                            continue; // Skip already imported names.
                        }

                        outT << nl << "from " << moduleName << " import " << name;
                        if (!alias.empty())
                        {
                            outT << " as " << alias;
                        }
                        hasTypingImports = true;
                    }
                }
                else
                {
                    // If there are no definitions, we still need to import the module to ensure that the type hints
                    // are available.
                    outT << nl << "import " << moduleName;
                    allImports.insert(moduleName);
                    hasTypingImports = true;
                }
            }
            outT.dec();

            if (hasTypingImports)
            {
                out << outT.str();
            }
        }
    }
}

Slice::Python::CompilationResult
Slice::Python::compile(
    const std::string& programName,
    const std::unique_ptr<DependencyGenerator>& dependencyGenerator,
    PackageVisitor& packageVisitor,
    const vector<string>& files,
    const vector<string>& preprocessorArgs,
    bool sortFragments,
    CompilationKind compilationKind,
    bool debug)
{
    // The import visitor is reused to collect the imports from all generated Python modules, which are later used to
    // compute the order in which Python modules should be evaluated.
    ImportVisitor importVisitor;

    // The list of code fragments generated by the code visitor.
    vector<CodeFragment> fragments;

    int status = EXIT_SUCCESS;

    for (const auto& fileName : files)
    {
        PreprocessorPtr preprocessor;
        UnitPtr unit;
        try
        {
            preprocessor = Preprocessor::create(programName, fileName, preprocessorArgs);
            FILE* preprocessedHandle = preprocessor->preprocess("-D__SLICE2PY__");

            unit = Unit::createUnit("python", debug);
            int parseStatus = unit->parse(fileName, preprocessedHandle, false);

            preprocessor->close();

            if (parseStatus == EXIT_FAILURE)
            {
                status = EXIT_FAILURE;
            }
            else
            {
                if (dependencyGenerator)
                {
                    // Collect the dependencies of the unit.
                    dependencyGenerator->addDependenciesFor(unit);
                }

                parseAllDocComments(unit, Slice::Python::pyLinkFormatter);
                validatePythonMetadata(unit);

                unit->visit(&packageVisitor);
                unit->visit(&importVisitor);

                if (compilationKind == CompilationKind::All || compilationKind == CompilationKind::Module)
                {
                    CodeVisitor codeVisitor{
                        importVisitor.getRuntimeImports(),
                        importVisitor.getTypingImports(),
                        importVisitor.getAllImportNames()};
                    unit->visit(&codeVisitor);

                    const vector<CodeFragment>& newFragments = codeVisitor.codeFragments();
                    fragments.insert(fragments.end(), newFragments.begin(), newFragments.end());
                }
            }
            unit->destroy();
        }
        catch (...)
        {
            if (preprocessor)
            {
                preprocessor->close();
            }

            if (unit)
            {
                unit->destroy();
            }
            throw;
        }
    }

    if (compilationKind == CompilationKind::All || compilationKind == CompilationKind::Module)
    {
        // Write the imports for each code fragment.
        ImportsMap runtimeImports = importVisitor.getRuntimeImports();
        ImportsMap typingImports = importVisitor.getTypingImports();

        for (auto& fragment : fragments)
        {
            Python::BufferedOutput out;
            writeHeader(out);
            writeImports(runtimeImports[fragment.moduleName], typingImports[fragment.moduleName], out);
            out << sp;
            out << fragment.code;

            fragment.code = out.str();
        }

        if (sortFragments)
        {
            vector<string> generatedModules;
            generatedModules.reserve(fragments.size());
            for (const auto& fragment : fragments)
            {
                // Collect the names of all generated modules.
                generatedModules.push_back(fragment.moduleName);
            }

            // List of generated modules in reverse topological order.
            // Each module in this list depends only on modules that appear earlier in the list, or modules that are not
            // part of this compilation.
            vector<CodeFragment> processedFragments;

            while (!fragments.empty())
            {
                size_t fragmentsSize = fragments.size();
                for (auto it = fragments.begin(); it != fragments.end();)
                {
                    CodeFragment fragment = *it;
                    const auto& moduleImports = runtimeImports[fragment.moduleName];

                    bool unseenDependencies = false;
                    for (const auto& m : moduleImports)
                    {
                        // If the current module depends on a module that is being generated but not yet seen we
                        // postpone its compilation.
                        if (find(generatedModules.begin(), generatedModules.end(), m.first) != generatedModules.end() &&
                            find_if(
                                processedFragments.begin(),
                                processedFragments.end(),
                                [&m](const auto& element)
                                { return element.moduleName == m.first; }) == processedFragments.end())
                        {
                            unseenDependencies = true;
                            break;
                        }
                    }

                    if (!unseenDependencies)
                    {
                        processedFragments.push_back(fragment);
                        it = fragments.erase(it);
                    }
                    else
                    {
                        it++;
                    }
                }

                // If we didn't remove any module from the runtime imports, it means that we have a circular dependency
                // between Slice files which is not allowed.
                if (fragments.size() == fragmentsSize)
                {
                    assert(false);
                    throw runtime_error("Circular dependency detected in Slice files.");
                }
            }
            fragments = std::move(processedFragments);
        }
    }

    if (compilationKind == CompilationKind::All || compilationKind == CompilationKind::Index)
    {
        for (const auto& [name, imports] : packageVisitor.imports())
        {
            string packageName = name;
            // The pop_back call removes the last dot from the package name.
            packageName.pop_back();
            BufferedOutput out;
            writePackageIndex(imports, out);
            string fileName = name;
            replace(fileName.begin(), fileName.end(), '.', '/');
            fileName += "/__init__.py";
            fragments.push_back(
                {.sliceFileName = "",
                 .packageName = packageName,
                 .moduleName = packageName,
                 .fileName = fileName,
                 .isPackageIndex = true,
                 .code = out.str()});
        }
    }

    return {status, fragments};
}

string
Slice::Python::pyLinkFormatter(const string& rawLink, const ContainedPtr&, const SyntaxTreeBasePtr& target)
{
    ostringstream result;
    if (target)
    {
        if (auto builtinTarget = dynamic_pointer_cast<Builtin>(target))
        {
            if (builtinTarget->kind() == Builtin::KindValue)
            {
                result << ":class:`Ice.Value`";
            }
            else if (builtinTarget->kind() == Builtin::KindObjectProxy)
            {
                result << ":class:`Ice.ObjectPrx`";
            }
            else
            {
                result << "``" << rawLink << "``";
            }
        }
        else if (auto operationTarget = dynamic_pointer_cast<Operation>(target))
        {
            string targetScoped = operationTarget->interface()->mappedScoped(".");

            // link to the method on the proxy interface
            result << ":meth:`" << targetScoped << "Prx." << operationTarget->mappedName() << "`";
        }
        else if (dynamic_pointer_cast<Sequence>(target) || dynamic_pointer_cast<Dictionary>(target))
        {
            // For sequences and dictionaries there is nothing to link to.
            // TODO use the type hint
            result << "``" << rawLink << "``";
        }
        else
        {
            string targetScoped = dynamic_pointer_cast<Contained>(target)->mappedScoped(".");
            result << ":class:`" << targetScoped;
            if (auto interfaceTarget = dynamic_pointer_cast<InterfaceDecl>(target))
            {
                // link to the proxy interface
                result << "Prx";
            }
            result << "`";
        }
    }
    else
    {
        result << "``";

        auto hashPos = rawLink.find('#');
        if (hashPos != string::npos)
        {
            if (hashPos != 0)
            {
                result << rawLink.substr(0, hashPos) << ".";
            }
            result << rawLink.substr(hashPos + 1);
        }
        else
        {
            result << rawLink;
        }

        result << "``";
    }
    return result.str();
}

void
Slice::Python::validatePythonMetadata(const UnitPtr& unit)
{
    auto pythonArrayTypeValidationFunc = [](const MetadataPtr& m, const SyntaxTreeBasePtr& p) -> optional<string>
    {
        if (auto sequence = dynamic_pointer_cast<Sequence>(p))
        {
            BuiltinPtr builtin = dynamic_pointer_cast<Builtin>(sequence->type());
            if (!builtin || !(builtin->isNumericType() || builtin->kind() == Builtin::KindBool))
            {
                return "the '" + m->directive() +
                       "' metadata can only be applied to sequences of bools, bytes, shorts, ints, longs, floats, "
                       "or doubles";
            }
        }
        return nullopt;
    };

    map<string, MetadataInfo> knownMetadata;

    // "python:<array-type>"
    MetadataInfo arrayTypeInfo = {
        .validOn = {typeid(Sequence)},
        .acceptedArgumentKind = MetadataArgumentKind::NoArguments,
        .acceptedContext = MetadataApplicationContext::DefinitionsAndTypeReferences,
        .extraValidation = pythonArrayTypeValidationFunc,
    };
    knownMetadata.emplace("python:array.array", arrayTypeInfo);
    knownMetadata.emplace("python:numpy.ndarray", std::move(arrayTypeInfo));

    // "python:identifier"
    MetadataInfo identifierInfo = {
        .validOn =
            {typeid(Module),
             typeid(InterfaceDecl),
             typeid(Operation),
             typeid(ClassDecl),
             typeid(Slice::Exception),
             typeid(Struct),
             typeid(Sequence),
             typeid(Dictionary),
             typeid(Enum),
             typeid(Enumerator),
             typeid(Const),
             typeid(Slice::Parameter),
             typeid(DataMember)},
        .acceptedArgumentKind = MetadataArgumentKind::SingleArgument,
    };
    knownMetadata.emplace("python:identifier", std::move(identifierInfo));

    // "python:memoryview"
    MetadataInfo memoryViewInfo = {
        .validOn = {typeid(Sequence)},
        .acceptedArgumentKind = MetadataArgumentKind::RequiredTextArgument,
        .acceptedContext = MetadataApplicationContext::DefinitionsAndTypeReferences,
        .extraValidation = pythonArrayTypeValidationFunc,
    };
    knownMetadata.emplace("python:memoryview", std::move(memoryViewInfo));

    // "python:seq"
    // We support 2 arguments to this metadata: "list", and "tuple".
    MetadataInfo seqInfo = {
        .validOn = {typeid(Sequence)},
        .acceptedArgumentKind = MetadataArgumentKind::SingleArgument,
        .validArgumentValues = {{"list", "tuple"}},
        .acceptedContext = MetadataApplicationContext::DefinitionsAndTypeReferences,
    };
    knownMetadata.emplace("python:seq", std::move(seqInfo));
    MetadataInfo unqualifiedSeqInfo = {
        .validOn = {typeid(Sequence)},
        .acceptedArgumentKind = MetadataArgumentKind::NoArguments,
        .acceptedContext = MetadataApplicationContext::DefinitionsAndTypeReferences,
    };
    knownMetadata.emplace("python:list", unqualifiedSeqInfo);
    knownMetadata.emplace("python:tuple", std::move(unqualifiedSeqInfo));

    // Pass this information off to the parser's metadata validation logic.
    validateMetadata(unit, "python", std::move(knownMetadata));
}

namespace
{
    mutex globalMutex;
    bool interrupted = false;

    void interruptedCallback(int /*signal*/)
    {
        lock_guard lock(globalMutex);

        interrupted = true;
    }

    void usage(const string& n)
    {
        consoleErr << "Usage: " << n << " [options] slice-files...\n";
        consoleErr
            << "Options:\n"
               "-h, --help               Show this message.\n"
               "-v, --version            Display the Ice version.\n"
               "-DNAME                   Define NAME as 1.\n"
               "-DNAME=DEF               Define NAME as DEF.\n"
               "-UNAME                   Remove any definition for NAME.\n"
               "-IDIR                    Put DIR in the include file search path.\n"
               "--output-dir DIR         Create files in the directory DIR.\n"
               "-d, --debug              Print debug messages.\n"
               "--depend                 Generate Makefile dependencies.\n"
               "--depend-xml             Generate dependencies in XML format.\n"
               "--depend-file FILE       Write dependencies to FILE instead of standard output.\n"
               "--no-package             Do not generate Python package hierarchy.\n"
               "--build                  modules|index|all\n"
               "\n"
               "    Controls which types of Python files are generated from the Slice definitions.\n"
               "\n"
               "    modules  Generates only the Python module files for the Slice definitions.\n"
               "    index    Generates only the Python package index files (__init__.py).\n"
               "    all      Generates both module and index files (this is the default if --build is omitted).\n"
               "\n"
               "--list-generated         modules|index|all\n"
               "\n"
               "    Lists the Python files that would be generated for the given Slice definitions, without\n"
               "    producing any output files.\n"
               "\n"
               "    modules  Lists the Python module files generated from the Slice definitions.\n"
               "    index    Lists the Python package index files (__init__.py) that would be created.\n"
               "    all      Lists both module and index files.\n"
               "\n"
               "    All paths are relative to the directory specified with --output-dir.\n"
               "    Each file is listed on a separate line. No duplicates are included.\n";
    }
}

int
Slice::Python::compile(const std::vector<std::string>& args)
{
    const string programName = args[0];

    IceInternal::Options opts;
    opts.addOpt("h", "help");
    opts.addOpt("v", "version");
    opts.addOpt("D", "", IceInternal::Options::NeedArg, "", IceInternal::Options::Repeat);
    opts.addOpt("U", "", IceInternal::Options::NeedArg, "", IceInternal::Options::Repeat);
    opts.addOpt("I", "", IceInternal::Options::NeedArg, "", IceInternal::Options::Repeat);
    opts.addOpt("", "output-dir", IceInternal::Options::NeedArg);
    opts.addOpt("", "depend");
    opts.addOpt("", "depend-xml");
    opts.addOpt("", "depend-file", IceInternal::Options::NeedArg, "");
    opts.addOpt("d", "debug");
    opts.addOpt("", "build", IceInternal::Options::NeedArg, "all");
    opts.addOpt("", "list-generated", IceInternal::Options::NeedArg);

    vector<string> sliceFiles;
    try
    {
        // The non-option arguments are the Slice files.
        sliceFiles = opts.parse(args);
    }
    catch (const IceInternal::BadOptException& e)
    {
        consoleErr << programName << ": error: " << e.what() << endl;
        usage(programName);
        return EXIT_FAILURE;
    }

    if (opts.isSet("help"))
    {
        usage(programName);
        return EXIT_SUCCESS;
    }

    if (opts.isSet("version"))
    {
        consoleErr << ICE_STRING_VERSION << endl;
        return EXIT_SUCCESS;
    }

    vector<string> preprocessorArgs;
    vector<string> optargs = opts.argVec("D");
    preprocessorArgs.reserve(optargs.size());
    for (const auto& arg : optargs)
    {
        preprocessorArgs.push_back("-D" + arg);
    }

    optargs = opts.argVec("U");
    for (const auto& arg : optargs)
    {
        preprocessorArgs.push_back("-U" + arg);
    }

    vector<string> includePaths = opts.argVec("I");
    for (const auto& includePath : includePaths)
    {
        preprocessorArgs.push_back("-I" + Preprocessor::normalizeIncludePath(includePath));
    }

    string outputDir = opts.optArg("output-dir");

    bool depend = opts.isSet("depend");

    bool dependXML = opts.isSet("depend-xml");

    string dependFile = opts.optArg("depend-file");

    bool debug = opts.isSet("debug");

    string buildArg = opts.optArg("build");

    string listArg = opts.optArg("list-generated");

    if (sliceFiles.empty())
    {
        consoleErr << programName << ": error: no input file" << endl;
        usage(programName);
        return EXIT_FAILURE;
    }

    if (depend && dependXML)
    {
        consoleErr << programName << ": error: cannot specify both --depend and --depend-xml" << endl;
        usage(programName);
        return EXIT_FAILURE;
    }

    if (buildArg != "modules" && buildArg != "index" && buildArg != "all")
    {
        consoleErr << programName << ": error: invalid argument for --build: " << buildArg << endl;
        usage(programName);
        return EXIT_FAILURE;
    }

    if (listArg != "modules" && listArg != "index" && listArg != "all" && !listArg.empty())
    {
        consoleErr << programName << ": error: invalid argument for --list-generated: " << listArg << endl;
        usage(programName);
        return EXIT_FAILURE;
    }

    if (!outputDir.empty() && !IceInternal::directoryExists(outputDir))
    {
        consoleErr << programName << ": error: argument for --output-dir does not exist or is not a directory" << endl;
        return EXIT_FAILURE;
    }

    Ice::CtrlCHandler ctrlCHandler;
    ctrlCHandler.setCallback(interruptedCallback);

    auto dependencyGenerator = make_unique<DependencyGenerator>();
    PackageVisitor packageVisitor;

    CompilationKind compilationKind;
    if (!listArg.empty() || depend || dependXML)
    {
        // If we are listing generated files or generating dependencies, we do not generate any Python code.
        compilationKind = CompilationKind::None;
    }
    else if (buildArg == "modules")
    {
        compilationKind = CompilationKind::Module;
    }
    else if (buildArg == "index")
    {
        compilationKind = CompilationKind::Index;
    }
    else
    {
        compilationKind = CompilationKind::All;
    }

    CompilationResult compilationResult = Slice::Python::compile(
        programName,
        dependencyGenerator,
        packageVisitor,
        sliceFiles,
        preprocessorArgs,
        false, // Don't need to sort fragments when generating code with slice2py.
        compilationKind,
        debug);

    if (compilationResult.status == EXIT_FAILURE)
    {
        return compilationResult.status;
    }

    if (depend)
    {
        for (const auto& [source, files] : packageVisitor.generated())
        {
            for (const auto& file : files)
            {
                dependencyGenerator->writeMakefileDependencies(dependFile, source, file);
            }
        }
    }
    else if (dependXML)
    {
        dependencyGenerator->writeXMLDependencies(dependFile);
    }
    else if (!listArg.empty())
    {
        std::set<string> generated;

        for (const auto& [source, files] : packageVisitor.generated())
        {
            for (const auto& file : files)
            {
                bool skip = (listArg == "modules" && file.find("/__init__.py") != string::npos) ||
                            (listArg == "index" && file.find("/__init__.py") == string::npos);

                if (skip)
                {
                    continue;
                }
                generated.insert(file);
            }
        }

        for (const auto& file : generated)
        {
            cout << file << endl;
        }
    }
    else
    {
        // Emit the Python code fragments.
        for (const auto& fragment : compilationResult.fragments)
        {
            createPackagePath(fragment.packageName, outputDir);

            string outputPath = outputDir.empty() ? fragment.fileName : outputDir + "/" + fragment.fileName;

            Output out{outputPath.c_str()};
            if (out.isOpen())
            {
                out << fragment.code;
            }
            else
            {
                ostringstream os;
                os << "cannot open file '" << outputPath << "': " << IceInternal::lastErrorToString();
                throw FileException(os.str());
            }
        }
    }

    {
        lock_guard lock(globalMutex);
        if (interrupted)
        {
            FileTracker::instance()->cleanup();
            return EXIT_FAILURE;
        }
    }

    return compilationResult.status;
}
