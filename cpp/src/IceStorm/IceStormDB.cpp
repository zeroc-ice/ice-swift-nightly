// Copyright (c) ZeroC, Inc.

#include "../Ice/ConsoleUtil.h"
#include "../Ice/FileUtil.h"
#include "../Ice/Options.h"
#include "../IceDB/IceDB.h"
#include "DBTypes.h"
#include "Ice/Ice.h"
#include "Ice/StringUtil.h"

#include <fstream>
#include <iterator>

using namespace std;
using namespace IceInternal;

int run(const shared_ptr<Ice::Communicator>&, const Ice::StringSeq&);

int
#ifdef _WIN32
wmain(int argc, wchar_t* argv[])
#else
main(int argc, char* argv[])
#endif
{
    int status = 0;

    try
    {
        Ice::CtrlCHandler ctrlCHandler;
        Ice::CommunicatorHolder ich(argc, argv);
        ctrlCHandler.setCallback([communicator = ich.communicator()](int) { communicator->destroy(); });
        status = run(ich.communicator(), Ice::argsToStringSeq(argc, argv));
    }
    catch (const std::exception& ex)
    {
        consoleErr << ex.what() << endl;
        status = 1;
    }

    return status;
}

void
usage(const string& name)
{
    consoleErr << "Usage: " << name << " <options>\n";
    consoleErr << "Options:\n"
                  "-h, --help             Show this message.\n"
                  "-v, --version          Display version.\n"
                  "--import FILE          Import database from FILE.\n"
                  "--export FILE          Export database to FILE.\n"
                  "--dbhome DIR           Source or target database environment.\n"
                  "--dbpath DIR           Source or target database environment.\n"
                  "--mapsize VALUE        Set LMDB map size in MB (optional, import only).\n"
                  "-d, --debug            Print debug messages.\n";
}

int
run(const shared_ptr<Ice::Communicator>& communicator, const Ice::StringSeq& args)
{
    IceInternal::Options opts;
    opts.addOpt("h", "help");
    opts.addOpt("v", "version");
    opts.addOpt("d", "debug");
    opts.addOpt("", "import", IceInternal::Options::NeedArg);
    opts.addOpt("", "export", IceInternal::Options::NeedArg);
    opts.addOpt("", "dbhome", IceInternal::Options::NeedArg);
    opts.addOpt("", "dbpath", IceInternal::Options::NeedArg);
    opts.addOpt("", "mapsize", IceInternal::Options::NeedArg);

    try
    {
        if (!opts.parse(args).empty())
        {
            consoleErr << args[0] << ": too many arguments" << endl;
            usage(args[0]);
            return 1;
        }
    }
    catch (const IceInternal::BadOptException& e)
    {
        consoleErr << args[0] << ": " << e.what() << endl;
        usage(args[0]);
        return 1;
    }

    if (opts.isSet("help"))
    {
        usage(args[0]);
        return 0;
    }

    if (opts.isSet("version"))
    {
        consoleOut << ICE_STRING_VERSION << endl;
        return 0;
    }

    if (!(opts.isSet("import") ^ opts.isSet("export")))
    {
        consoleErr << args[0] << ": either --import or --export must be set" << endl;
        usage(args[0]);
        return 1;
    }

    if (!(opts.isSet("dbhome") ^ opts.isSet("dbpath")))
    {
        consoleErr << args[0] << ": set the database environment directory with either --dbhome or --dbpath" << endl;
        usage(args[0]);
        return 1;
    }

    bool debug = opts.isSet("debug");
    bool import = opts.isSet("import");
    string dbFile = opts.optArg(import ? "import" : "export");
    string dbPath;
    if (opts.isSet("dbhome"))
    {
        dbPath = opts.optArg("dbhome");
    }
    else
    {
        dbPath = opts.optArg("dbpath");
    }

    string mapSizeStr = opts.optArg("mapsize");
    size_t mapSize = IceDB::getMapSize(stoi(mapSizeStr));

    try
    {
        IceStorm::AllData data;

        IceDB::IceContext dbContext = {communicator};

        if (import)
        {
            consoleOut << "Importing database to directory " << dbPath << " from file " << dbFile << endl;

            if (!IceInternal::directoryExists(dbPath))
            {
                consoleErr << args[0] << ": output directory does not exist: " << dbPath << endl;
                return 1;
            }

            if (!IceInternal::isEmptyDirectory(dbPath))
            {
                consoleErr << args[0] << ": output directory is not empty: " << dbPath << endl;
                return 1;
            }

            ifstream fs(IceInternal::streamFilename(dbFile).c_str(), ios::binary);
            if (fs.fail())
            {
                consoleErr << args[0] << ": could not open input file: " << IceInternal::errorToString(errno) << endl;
                return 1;
            }
            fs.unsetf(ios::skipws);

            fs.seekg(0, ios::end);
            streampos fileSize = fs.tellg();

            if (!fileSize)
            {
                fs.close();
                consoleErr << args[0] << ": empty input file" << endl;
                return 1;
            }

            fs.seekg(0, ios::beg);

            vector<byte> buf;
            buf.reserve(static_cast<size_t>(fileSize));
            fs.read(reinterpret_cast<char*>(buf.data()), fileSize);
            fs.close();

            string type;
            int version;

            Ice::InputStream stream(communicator, Ice::currentEncoding, buf);
            stream.read(type);
            if (type != "IceStorm")
            {
                consoleErr << args[0] << ": incorrect input file type: " << type << endl;
                return 1;
            }
            stream.read(version);
            stream.read(data);

            {
                IceDB::Env env(dbPath, 2, mapSize);
                IceDB::ReadWriteTxn txn(env);

                if (debug)
                {
                    consoleOut << "Writing LLU Map:" << endl;
                }

                IceDB::Dbi<string, IceStormElection::LogUpdate, IceDB::IceContext, Ice::OutputStream> lluMap(
                    txn,
                    "llu",
                    dbContext,
                    MDB_CREATE);

                for (const auto& llu : data.llus)
                {
                    if (debug)
                    {
                        consoleOut << "  KEY = " << llu.first << endl;
                    }
                    lluMap.put(txn, llu.first, llu.second);
                }

                if (debug)
                {
                    consoleOut << "Writing Subscriber Map:" << endl;
                }

                IceDB::
                    Dbi<IceStorm::SubscriberRecordKey, IceStorm::SubscriberRecord, IceDB::IceContext, Ice::OutputStream>
                        subscriberMap(txn, "subscribers", dbContext, MDB_CREATE);

                for (const auto& subscriber : data.subscribers)
                {
                    if (debug)
                    {
                        consoleOut << "  KEY = TOPIC(" << communicator->identityToString(subscriber.first.topic)
                                   << ") ID(" << communicator->identityToString(subscriber.first.id) << ")" << endl;
                    }
                    subscriberMap.put(txn, subscriber.first, subscriber.second);
                }

                txn.commit();
                env.close();
            }
        }
        else
        {
            consoleOut << "Exporting database from directory " << dbPath << " to file " << dbFile << endl;

            {
                IceDB::Env env(dbPath, 2);
                IceDB::ReadOnlyTxn txn(env);

                if (debug)
                {
                    consoleOut << "Reading LLU Map:" << endl;
                }

                IceDB::Dbi<string, IceStormElection::LogUpdate, IceDB::IceContext, Ice::OutputStream> lluMap(
                    txn,
                    "llu",
                    dbContext,
                    0);

                string s;
                IceStormElection::LogUpdate llu;
                IceDB::ReadOnlyCursor<string, IceStormElection::LogUpdate, IceDB::IceContext, Ice::OutputStream>
                    lluCursor(lluMap, txn);
                while (lluCursor.get(s, llu, MDB_NEXT))
                {
                    if (debug)
                    {
                        consoleOut << "  KEY = " << s << endl;
                    }
                    data.llus.insert(std::make_pair(s, llu));
                }
                lluCursor.close();

                if (debug)
                {
                    consoleOut << "Reading Subscriber Map:" << endl;
                }

                IceDB::
                    Dbi<IceStorm::SubscriberRecordKey, IceStorm::SubscriberRecord, IceDB::IceContext, Ice::OutputStream>
                        subscriberMap(txn, "subscribers", dbContext, 0);

                IceStorm::SubscriberRecordKey key;
                IceStorm::SubscriberRecord record;
                IceDB::ReadOnlyCursor<
                    IceStorm::SubscriberRecordKey,
                    IceStorm::SubscriberRecord,
                    IceDB::IceContext,
                    Ice::OutputStream>
                    subCursor(subscriberMap, txn);
                while (subCursor.get(key, record, MDB_NEXT))
                {
                    if (debug)
                    {
                        consoleOut << "  KEY = TOPIC(" << communicator->identityToString(key.topic) << ") ID("
                                   << communicator->identityToString(key.id) << ")" << endl;
                    }
                    data.subscribers.insert(std::make_pair(key, record));
                }
                subCursor.close();

                txn.rollback();
                env.close();
            }

            Ice::OutputStream stream{Ice::currentEncoding};
            stream.write("IceStorm");
            stream.write(ICE_INT_VERSION);
            stream.write(data);

            ofstream fs(IceInternal::streamFilename(dbFile).c_str(), ios::binary);
            if (fs.fail())
            {
                consoleErr << args[0] << ": could not open output file: " << IceInternal::errorToString(errno) << endl;
                return 1;
            }
            fs.write(reinterpret_cast<const char*>(stream.b.begin()), static_cast<streamsize>(stream.b.size()));
            fs.close();
        }
    }
    catch (const Ice::Exception& ex)
    {
        consoleErr << args[0] << ": " << (import ? "import" : "export") << " failed:\n" << ex << endl;
        return 1;
    }

    return 0;
}
