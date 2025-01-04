


#include "database.h"
#include <unordered_map>
#include <iostream>
#include "HashMap.h"

InMemoryDB::InMemoryDB()
{
    _dbInstance = "InMemory";
}

int InMemoryDB::Open(const std::string)
{
    db = new std::unordered_map<std::string, dbTable>();
//    db =     CTSL::HashMap<std::string, dbTable>();

    activeTable = "table1";

    std::cout << std::endl
              << "In-Memory DB configuration OK" << std::endl;

    return 0;
}

std::string InMemoryDB::Get(const std::string key)
{
    std::string value;

//    if (((*db)[activeTable]).find(key) == ((*db)[activeTable]).end())
    if (   !(((*db)[activeTable]).find(key, value) )  )
    {
        return "0";
    }

    return value;
}

std::string InMemoryDB::Put(const std::string key, const std::string value)
{
//    std::string oldValue = Get(key);


//    (*db)[activeTable][key] = value;
    (*db)[activeTable].insert(key, value);


    return "1";
}


void InMemoryDB::Remove(const std::string key)
{
    (*db)[activeTable].erase(key);

    return;
}





int InMemoryDB::SelectTable(const std::string tableName)
{
    if (tableName == activeTable)
    {
        return 1;
    }
    activeTable = tableName;
    return 0;
}

int InMemoryDB::Close(const std::string)
{
    delete db;
    return 0;
}
