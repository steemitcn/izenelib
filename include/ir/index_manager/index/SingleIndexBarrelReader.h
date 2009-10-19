/**
* @file        SingleIndexBarrelReader.h
* @author     Yingfeng Zhang
* @version     SF1 v5.0
* @brief Single Index Barrel Reader
*/
#ifndef SINGLEINDEXBARRELREADER_H
#define SINGLEINDEXBARRELREADER_H

#include <ir/index_manager/index/IndexBarrelReader.h>
#include <ir/index_manager/index/CollectionInfo.h>

#include <map>

NS_IZENELIB_IR_BEGIN

namespace indexmanager{
class Indexer;
class BarrelInfo;
/**
* @brief Open a certain index barrel and return instance of TermReader
*/

class SingleIndexBarrelReader : public IndexBarrelReader
{
public:
    SingleIndexBarrelReader(Indexer* pIndex, BarrelInfo* pBarrelInfo,DiskIndexOpenMode mode);
    virtual ~SingleIndexBarrelReader(void);
public:
    void open(const char* name,DiskIndexOpenMode mode);

    TermReader* termReader(collectionid_t colID);

    TermReader* termReader(collectionid_t colID,const char* field);

    void deleteDocumentPhysically(IndexerDocument* pDoc);

    CollectionsInfo& getCollectionsInfo()
    {
        return *pCollectionsInfo_;
    }

    void close();
private:
    void delDocField(unsigned int colID, docid_t docId, const char* fieldName, boost::shared_ptr<ForwardIndex>& forwardIndex);

private:
    string name_;

    CollectionsInfo* pCollectionsInfo_;

    BarrelInfo* pBarrelInfo_;

    map<collectionid_t, TermReader*> termReaderMap_;

    MemCache* pMemCache_; ///for deleting index;

};

}

NS_IZENELIB_IR_END

#endif
