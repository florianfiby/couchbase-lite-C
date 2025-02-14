//
// CBLTest.cc
//
// Copyright © 2018 Couchbase. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#define CATCH_CONFIG_CONSOLE_WIDTH 120

#include "CBLTest.hh"
#include "CBLTest_Cpp.hh"
#include "CBLPrivate.h"
#include "fleece/slice.hh"
#include <sstream>
#include <fstream>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifndef _MSC_VER
#include <sys/stat.h>
#endif

using namespace std;
using namespace fleece;


static string databaseDir() {
#ifndef WIN32
    string dir = "/tmp/CBL_C_tests";
    if (mkdir(dir.c_str(), 0744) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#else
    string dir = "C:\\tmp\\CBL_C_tests";
    if (_mkdir(dir.c_str()) != 0 && errno != EEXIST)
        FAIL("Can't create temp directory: errno " << errno);
#endif
    return dir;
}


alloc_slice const CBLTest::kDatabaseDir(databaseDir());
slice       const CBLTest::kDatabaseName = "CBLtest";

const CBLDatabaseConfiguration CBLTest::kDatabaseConfiguration = []{
    // One-time setup:
    CBLError_SetCaptureBacktraces(true);
    CBLDatabaseConfiguration config = {kDatabaseDir};
    return config;
}();


CBLTest::CBLTest() {
    // Check that these have been correctly exported
    CHECK(FLValue_GetType(kFLNullValue) == kFLNull);
    CHECK(FLValue_GetType(kFLUndefinedValue) == kFLUndefined);
    CHECK(FLValue_GetType((FLValue)kFLEmptyArray) == kFLArray);
    CHECK(FLValue_GetType((FLValue)kFLEmptyDict) == kFLDict);

    CBLError error;
    if (!CBL_DeleteDatabase(kDatabaseName, kDatabaseConfiguration.directory, &error) && error.code != 0)
        FAIL("Can't delete temp database: " << error.domain << "/" << error.code);
    db = CBLDatabase_Open(kDatabaseName, &kDatabaseConfiguration, &error);
    REQUIRE(db);
}


CBLTest::~CBLTest() {
    if (db) {
        ExpectingExceptions x; // Database might have been closed by the test:
        CBLError error;
        if (!CBLDatabase_Close(db, &error))
            WARN("Failed to close database: " << error.domain << "/" << error.code);
        CBLDatabase_Release(db);
    }
    if (CBL_InstanceCount() > 0)
        CBL_DumpInstances();
    CHECK(CBL_InstanceCount() == 0);
}

#pragma mark - C++ TEST CLASS:

alloc_slice const CBLTest_Cpp::kDatabaseDir = CBLTest::kDatabaseDir;
slice const CBLTest_Cpp::kDatabaseName = CBLTest::kDatabaseName;

CBLTest_Cpp::CBLTest_Cpp()
:db(openEmptyDatabaseNamed(kDatabaseName))
{ }

CBLTest_Cpp::~CBLTest_Cpp() {
    if (db) {
        db.close();
        db = nullptr;
    }

    if (CBL_InstanceCount() > 0) {
        WARN("*** LEAKED OBJECTS: ***");
        CBL_DumpInstances();
    }
    CHECK(CBL_InstanceCount() == 0);
}

cbl::Database CBLTest_Cpp::openEmptyDatabaseNamed(slice name) {
    cbl::Database::deleteDatabase(name, CBLTest::kDatabaseConfiguration.directory);
    cbl::Database emptyDB = cbl::Database(name, CBLTest::kDatabaseConfiguration);
    REQUIRE(emptyDB);
    return emptyDB;
}

cbl::Database CBLTest_Cpp::openDatabaseNamed(fleece::slice name) {
    cbl::Database openedDB = cbl::Database(name, CBLTest::kDatabaseConfiguration);
    REQUIRE(openedDB);
    return openedDB;
}

void CBLTest_Cpp::createNumberedDocs(cbl::Collection& collection, unsigned n, unsigned start) {
    for (unsigned i = 0; i < n; i++) {
        char docID[20];
        sprintf(docID, "doc-%03u", start + i);
        
        char content[100];
        sprintf(content, "This is the document #%03u.", start + i);
        cbl::MutableDocument doc(docID);
        doc["content"] = content;
        collection.saveDocument(doc);
    }
}

void CBLTest_Cpp::createDoc(cbl::Collection& collection, std::string docID, std::string jsonContent) {
    cbl::MutableDocument doc(docID);
    doc.setPropertiesAsJSON(jsonContent);
    collection.saveDocument(doc);
}

void CBLTest_Cpp::createDocs(cbl::Collection& collection, unsigned n, std::string idprefix) {
    for (unsigned i = 0; i < n; i++) {
        string docID = idprefix.append("-").append(to_string(i+1));
        
        char content[100];
        sprintf(content, "This is the document #%03u.", i+1);
        cbl::MutableDocument doc(docID);
        doc["content"] = content;
        collection.saveDocument(doc);
    }
}

#pragma mark - Test Utils :

string GetTestFilePath(const std::string &filename) {
    static string sTestFilesPath;
    if (sTestFilesPath.empty()) {
#ifdef __APPLE__
        auto bundle = CFBundleGetBundleWithIdentifier(CFSTR("com.couchbase.CouchbaseLiteTests"));
        if (bundle) {
            auto url = CFBundleCopyResourcesDirectoryURL(bundle);
            CFAutorelease(url);
            url = CFURLCopyAbsoluteURL(url);
            CFAutorelease(url);
            auto path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
            CFAutorelease(path);
            char pathBuf[1000];
            CFStringGetCString(path, pathBuf, sizeof(pathBuf), kCFStringEncodingUTF8);
            strlcat(pathBuf, "/", sizeof(pathBuf));
            sTestFilesPath = pathBuf;
        } else
#endif
        {
#ifdef WIN32
            sTestFilesPath = "..\\test\\";
#else
            sTestFilesPath = "test/";
#endif
        }
    }
    return sTestFilesPath + filename;
}


bool ReadFileByLines(const string &path, const function<bool(FLSlice)> &callback) {
    INFO("Reading lines from " << path);
    fstream fd(path.c_str(), ios_base::in);
    REQUIRE(fd);
    vector<char> buf(1000000); // The Wikipedia dumps have verrry long lines
    while (fd.good()) {
        fd.getline(buf.data(), buf.size());
        auto len = fd.gcount();
        if (len <= 0)
            break;
        REQUIRE(buf[len-1] == '\0');
        --len;
        if (!callback({buf.data(), (size_t)len}))
            return false;
    }
    REQUIRE(fd.eof());
    return true;
}

unsigned ImportJSONLines(string &&path, CBLDatabase* database) {
    CBLError error {};
    CBLCollection *collection = CBLDatabase_DefaultCollection(database, &error);
    REQUIRE(collection);
    auto result = ImportJSONLines(move(path), collection);
    CBLCollection_Release(collection);
    return result;
}

// Read a file that contains a JSON document per line. Every line becomes a document.
unsigned ImportJSONLines(string &&path, CBLCollection* collection) {
    CBL_Log(kCBLLogDomainDatabase, kCBLLogInfo, "Reading %s ...  ", path.c_str());
    CBLError error {};
    unsigned numDocs = 0;
    
    CBLDatabase* database = CBLCollection_Database(collection);
    REQUIRE(database);
    
    REQUIRE(CBLDatabase_BeginTransaction(database, &error));
    ReadFileByLines(path, [&](FLSlice line) {
        char docID[20];
        sprintf(docID, "%07u", numDocs+1);
        auto doc = CBLDocument_CreateWithID(slice(docID));
        REQUIRE(CBLDocument_SetJSON(doc, line, &error));
        CHECK(CBLCollection_SaveDocumentWithConcurrencyControl(collection, doc, kCBLConcurrencyControlFailOnConflict, &error));
        CBLDocument_Release(doc);
        ++numDocs;
        return true;
    });
    CBL_Log(kCBLLogDomainDatabase, kCBLLogInfo, "Committing %u docs...", numDocs);
    REQUIRE(CBLDatabase_EndTransaction(database, true, &error));
    
    return numDocs;
}

void CheckError(CBLError& error, CBLErrorCode expectedCode, CBLErrorDomain expectedDomain) {
    CHECK(error.domain == expectedDomain);
    CHECK(error.code == expectedCode);
}

void CheckNotOpenError(CBLError& error) {
    CheckError(error, kCBLErrorNotOpen);
}

CBLCollection* CreateCollection(CBLDatabase* database, string collection, string scope) {
    CBLError error {};
    auto col = CBLDatabase_CreateCollection(database, slice(collection), slice(scope), &error);
    REQUIRE(col);
    return col;
}

void CreateDoc(CBLCollection *col, std::string docID, std::string jsonContent) {
    CBLError error {};
    CBLDocument* doc = docID.empty() ? CBLDocument_Create() : CBLDocument_CreateWithID(slice(docID));
    REQUIRE(CBLDocument_SetJSON(doc, slice(jsonContent), &error));
    REQUIRE(CBLCollection_SaveDocumentWithConcurrencyControl(col, doc, kCBLConcurrencyControlFailOnConflict, &error));
    CBLDocument_Release(doc);
}

std::string CollectionPath(CBLCollection* collection) {
   return slice(CBLScope_Name(CBLCollection_Scope(collection))).asString() + "." +
          slice(CBLCollection_Name(collection)).asString();
}

void PurgeAllDocs(CBLCollection* collection) {
    std::stringstream ss;
    ss << "SELECT meta().id FROM ";
    ss << CollectionPath(collection);

    CBLDatabase* database = CBLCollection_Database(collection);
    REQUIRE(database);
    
    auto query = CBLDatabase_CreateQuery(database, kCBLN1QLLanguage, slice(ss.str()), NULL, NULL);
    REQUIRE(query);
    
    auto rs = CBLQuery_Execute(query, NULL);
    REQUIRE(rs);
    
    while (CBLResultSet_Next(rs)) {
        FLString id = FLValue_AsString(CBLResultSet_ValueAtIndex(rs, 0));
        REQUIRE(id.buf);
        REQUIRE(CBLCollection_PurgeDocumentByID(collection, id, NULL));
    }
    
    CBLResultSet_Release(rs);
    CBLQuery_Release(query);
}
