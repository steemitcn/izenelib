#include <ir/index_manager/index/Indexer.h>
#include <ir/index_manager/index/IndexMerger.h>
#include <ir/index_manager/index/FieldMerger.h>
#include <ir/index_manager/store/Directory.h>
#include <ir/index_manager/index/IndexWriter.h>
#include <ir/index_manager/index/IndexBarrelWriter.h>
#include <ir/index_manager/utility/StringUtils.h>

#include <util/izene_log.h>

#include <boost/date_time/posix_time/posix_time.hpp>

#include <sstream>
#include <memory>
#include <algorithm>

#define  MAX_BARREL_LEVEL 32


using namespace izenelib::ir::indexmanager;

//////////////////////////////////////////////////////////////////////////
///MergeBarrelEntry
MergeBarrelEntry::MergeBarrelEntry(Directory* pDirectory,BarrelInfo* pBarrelInfo)
        :pDirectory_(pDirectory)
        ,pBarrelInfo_(pBarrelInfo)
        ,pCollectionsInfo_(NULL)
        ,currColID_(-1)
{
}
MergeBarrelEntry::~MergeBarrelEntry()
{
    if (pCollectionsInfo_)
    {
        delete pCollectionsInfo_;
        pCollectionsInfo_ = NULL;
    }

    pBarrelInfo_ = NULL;
}
void MergeBarrelEntry::load()
{
    IndexBarrelWriter* pWriter = pBarrelInfo_->getWriter();
    if (!pCollectionsInfo_)
    {
        if (pWriter)
        {
            pCollectionsInfo_ = new CollectionsInfo(*(pWriter->getCollectionsInfo()));
        }
        else
            pCollectionsInfo_ = new CollectionsInfo();
    }

    if (!pWriter)
    {
        IndexInput* fdiStream = pDirectory_->openInput(pBarrelInfo_->getName() + ".fdi");

        pCollectionsInfo_->read(fdiStream);///read collection info
        delete fdiStream;
    }
}

IndexMerger::IndexMerger(Indexer* pIndexer)
        :pIndexer_(pIndexer)
        ,pDirectory_(pIndexer->getDirectory())
        ,buffer_(NULL)
        ,bufsize_(0)
        ,bBorrowedBuffer_(false)
        ,pMergeBarrels_(NULL)
	,pDocFilter_(NULL)
{
}

IndexMerger::~IndexMerger()
{
    pIndexer_ = 0;

    if (pMergeBarrels_)
    {
        pMergeBarrels_->clear();
        delete pMergeBarrels_;
        pMergeBarrels_ = NULL;
    }
    pDocFilter_ = 0;
}

void IndexMerger::setBuffer(char* buffer,size_t length)
{
    buffer_ = buffer;
    bufsize_ = length;
}

void IndexMerger::merge(BarrelsInfo* pBarrels)
{
    if (!pBarrels || ((pBarrels->getBarrelCount() <= 1)&&!pDocFilter_))
    {
        pendingUpdate(pBarrels);
        return ;
    }

    pBarrelsInfo_ = pBarrels;

    MergeBarrel mb(pBarrels->getBarrelCount());
    ///put all index barrel into mb
    pBarrels->startIterator();
    BarrelInfo* pBaInfo;
    while (pBarrels->hasNext())
    {
        pBaInfo = pBarrels->next();
        mb.put(new MergeBarrelEntry(pDirectory_,pBaInfo));
    }

    while (mb.size() > 0)
    {
        addBarrel(mb.pop());
    }

    endMerge();
    pendingUpdate(pBarrels); ///update barrel name and base doc id
    pBarrelsInfo_ = NULL;

    if (bBorrowedBuffer_)
    {
        ///the buffer is borrowed from indexer, give it back to indexer
        setBuffer(NULL,0);
        bBorrowedBuffer_ = false;
    }
    pBarrels->startIterator();
    docid_t maxDoc = 0;
    while (pBarrels->hasNext())
    {
        pBaInfo = pBarrels->next();
        if(pBaInfo->getMaxDocID() > maxDoc)
            maxDoc = pBaInfo->getMaxDocID();
    }
    if(maxDoc < pBarrels->maxDocId())
        pBarrels->resetMaxDocId(maxDoc);
    pBarrels->write(pDirectory_);
}

void IndexMerger::addToMerge(BarrelsInfo* pBarrelsInfo,BarrelInfo* pBarrelInfo)
{
    if (!pMergeBarrels_)
    {
        pMergeBarrels_ = new vector<MergeBarrelEntry*>();
    }

    MergeBarrelEntry* pEntry = NULL;
    pBarrelsInfo_ = pBarrelsInfo;
    pEntry = new MergeBarrelEntry(pDirectory_,pBarrelInfo);
    pMergeBarrels_->push_back(pEntry);
	
    addBarrel(pEntry);

 
    pendingUpdate(pBarrelsInfo); ///update barrel name and base doc id
    pBarrelsInfo_ = NULL;

    if (bBorrowedBuffer_)
    {
        ///the buffer is borrowed from indexer, give it back to indexer
        setBuffer(NULL,0);
        bBorrowedBuffer_ = false;
    }
    pBarrelsInfo->write(pDirectory_);
}

void IndexMerger::pendingUpdate(BarrelsInfo* pBarrelsInfo)
{
    boost::mutex::scoped_lock lock(pIndexer_->mutex_);
    ///sort barrels
    pBarrelsInfo->sort(pDirectory_);
    BarrelInfo* pBaInfo;
    pBarrelsInfo->startIterator();
    while (pBarrelsInfo->hasNext())
    {
        pBaInfo = pBarrelsInfo->next();
        pBaInfo->setWriter(NULL);///clear writer
    }
    ///sleep is necessary because if a query get termreader before this lock,
    ///the query has not been finished even the index file/term dictionary info has been changed
    ///500ms is used to let these queries finish their flow.
    boost::thread::sleep(boost::get_system_time() + boost::posix_time::milliseconds(500));
}

void IndexMerger::mergeBarrel(MergeBarrel* pBarrel)
{
    DLOG(INFO)<< "Begin merge: " << endl;
    pBarrel->load();
    string newBarrelName = pBarrel->getIdentifier();
    BarrelInfo* pNewBarrelInfo = new BarrelInfo(newBarrelName,0);

    string name = newBarrelName + ".voc";///the file name of new index barrel
    IndexOutput* pVocStream = pDirectory_->createOutput(name);
    name = newBarrelName + ".dfp";
    IndexOutput* pDStream = pDirectory_->createOutput(name);
    name = newBarrelName + ".pop";
    IndexOutput* pPStream = pDirectory_->createOutput(name);

    OutputDescriptor* pOutputDesc = new OutputDescriptor(pVocStream,pDStream,pPStream,true);

    int32_t nEntry;
    MergeBarrelEntry* pEntry = NULL;
    count_t nNumDocs = 0;
    int32_t nEntryCount = (int32_t)pBarrel->size();
    ///update min doc id of index barrels,let doc id continuous
    map<collectionid_t,docid_t> newBaseDocIDMap;
    docid_t maxDocOfNewBarrel = 0;
    for (nEntry = 0;nEntry < nEntryCount;nEntry++)
    {
        pEntry = pBarrel->getAt(nEntry);

        nNumDocs += pEntry->pBarrelInfo_->getDocCount();
        
        if(pEntry->pBarrelInfo_->getMaxDocID() > maxDocOfNewBarrel)
            maxDocOfNewBarrel = pEntry->pBarrelInfo_->getMaxDocID();
			
        for (map<collectionid_t,docid_t>::iterator iter = pEntry->pBarrelInfo_->baseDocIDMap.begin();
                iter != pEntry->pBarrelInfo_->baseDocIDMap.end(); ++iter)
        {
            if (newBaseDocIDMap.find(iter->first) == newBaseDocIDMap.end())
            {
                newBaseDocIDMap.insert(make_pair(iter->first,iter->second));
            }
            else
            {
                if (newBaseDocIDMap[iter->first] > iter->second)
                {
                    newBaseDocIDMap[iter->first] = iter->second;
                }
            }
        }
    }
	
    pNewBarrelInfo->setDocCount(nNumDocs);
    pNewBarrelInfo->setBaseDocID(newBaseDocIDMap);
    pNewBarrelInfo->updateMaxDoc(maxDocOfNewBarrel);

    FieldsInfo* pFieldsInfo = NULL;
    CollectionsInfo collectionsInfo;
    CollectionInfo* pColInfo = NULL;
    FieldInfo* pFieldInfo = NULL;
    FieldMerger* pFieldMerger = NULL;
    fieldid_t fieldid = 0;
    bool bFinish = false;
    bool bHasPPosting = false;

    fileoffset_t vocOff1,vocOff2,dfiOff1,dfiOff2,ptiOff1 = 0,ptiOff2 = 0;
    fileoffset_t voffset = 0;

    ///collect all colIDs in barrells
    vector<collectionid_t> colIDSet;
    CollectionsInfo* pColsInfo = NULL;

    for (nEntry = 0; nEntry < nEntryCount; nEntry++)
    {
        pColsInfo = pBarrel->getAt(nEntry)->pCollectionsInfo_;
        pColsInfo->startIterator();
        while (pColsInfo->hasNext())
            colIDSet.push_back(pColsInfo->next()->getId());
    }
    sort(colIDSet.begin(), colIDSet.end());
    vector<collectionid_t>::iterator it = unique(colIDSet.begin(),colIDSet.end());
    colIDSet.resize(it - colIDSet.begin());

    for (vector<collectionid_t>::const_iterator p = colIDSet.begin(); p != colIDSet.end(); ++p)
    {
        bFinish = false;
        fieldid = 1;
        pFieldsInfo = NULL;

        for (nEntry = 0;nEntry < nEntryCount;nEntry++)
        {
            pEntry = pBarrel->getAt(nEntry);
            pEntry->setCurrColID(*p);
            pColInfo = pEntry->pCollectionsInfo_->getCollectionInfo(*p);
            pColInfo->getFieldsInfo()->startIterator();
        }

        while (!bFinish)
        {
        
            for (nEntry = 0;nEntry < nEntryCount;nEntry++)
            {
                pEntry = pBarrel->getAt(nEntry);
                pColInfo = pEntry->pCollectionsInfo_->getCollectionInfo(*p);

                if (NULL == pColInfo)
                {
                    bFinish = true;
                    break;
                }

                //if (pColInfo->getFieldsInfo()->numFields() > (fieldid-1))
                if(pColInfo->getFieldsInfo()->hasNext())
                {
                    //pFieldInfo = pColInfo->getFieldsInfo()->getField(fieldid);///get field information
                    pFieldInfo = pColInfo->getFieldsInfo()->next();
                    if (pFieldInfo)
                    {
                        if (pFieldInfo->isIndexed()&&pFieldInfo->isForward())///it's a index field
                        {
                            if (pFieldMerger == NULL)
                            {
                                pFieldMerger = new FieldMerger();
                                pFieldMerger->setDirectory(pDirectory_);
                                if(pDocFilter_)
                                    pFieldMerger->setDocFilter(pDocFilter_);
                            }
                            pFieldInfo->setColID(*p);
                            pFieldMerger->addField(pEntry->pBarrelInfo_,pFieldInfo);///add to field merger
                        }
                    }
                }
            } // for

            if (pFieldInfo)
            {
                if (pFieldMerger && pFieldMerger->numFields() > 0)
                {
                    vocOff1 = pVocStream->getFilePointer();
                    dfiOff1 = pDStream->getFilePointer();
                    if (pOutputDesc->getPPostingOutput())
                        ptiOff1 = pOutputDesc->getPPostingOutput()->getFilePointer();

                    pFieldMerger->setBuffer(buffer_,bufsize_);		///set buffer for field merge
                    voffset = pFieldMerger->merge(pOutputDesc);
                    pFieldInfo->setIndexOffset(voffset);///set offset of this field's index data

                    vocOff2 = pVocStream->getFilePointer();
                    dfiOff2 = pDStream->getFilePointer();
                    ptiOff2 = pOutputDesc->getPPostingOutput()->getFilePointer();
                    pFieldInfo->setDistinctNumTerms(pFieldMerger->numMergedTerms());

                    pFieldInfo->setLength(vocOff2-vocOff1,dfiOff2-dfiOff1,ptiOff2-ptiOff1);
                    if ( (bHasPPosting == false) && ((ptiOff2 - ptiOff1) > 0))
                        bHasPPosting = true;

                    delete pFieldMerger;
                    pFieldMerger = NULL;
                }
                if (pFieldsInfo == NULL)
                    pFieldsInfo = new FieldsInfo();
                pFieldsInfo->addField(pFieldInfo);

                fieldid++;
                pFieldInfo = NULL;
            }
            else
            {
                bFinish = true;
            }
        } // while
        CollectionInfo* pCollectionInfo = new CollectionInfo(*p, pFieldsInfo);
        pCollectionInfo->setOwn(true);
        collectionsInfo.addCollection(pCollectionInfo);
    } // for

    //deleted all merged barrels
    ///TODO:LOCK
    {
    boost::mutex::scoped_lock lock(pIndexer_->mutex_);
    for (nEntry = 0;nEntry < nEntryCount;nEntry++)
    {
        pEntry = pBarrel->getAt(nEntry);
        IndexBarrelWriter* pWriter = pEntry->pBarrelInfo_->getWriter();
        if (pWriter)///clear in-memory index
        {
            pWriter->resetCache(true);
            ///borrow buffer from indexer
            setBuffer((char*)pWriter->getMemCache()->getBegin(),pWriter->getMemCache()->getSize());
            bBorrowedBuffer_ = true;
        }
        pBarrelsInfo_->removeBarrel(pDirectory_,pEntry->pBarrelInfo_->getName());///delete merged barrels
    }
    pBarrelsInfo_->addBarrel(pNewBarrelInfo,false);
    ///sleep is necessary because if a query get termreader before this lock,
    ///the query has not been finished even the index file/term dictionary info has been changed
    ///500ms is used to let these queries finish their flow.
    boost::thread::sleep(boost::get_system_time() + boost::posix_time::milliseconds(500));
    }
    ///TODO:UNLOCK
    if (pMergeBarrels_)
    {
        removeMergedBarrels(pBarrel);
    }
    pBarrel->clear();

    name = newBarrelName + ".fdi";
    IndexOutput* fieldsStream = pDirectory_->createOutput(name);
    //fieldsInfo.write(fieldsStream);//field information
    collectionsInfo.write(fieldsStream);
    fieldsStream->flush();
    delete fieldsStream;

    if (bHasPPosting == false)
    {
        name = newBarrelName + ".pop";
        pDirectory_->deleteFile(name);
    }

    delete pOutputDesc;
    pOutputDesc = NULL;

    //////////////////////////////////////////////////////////////////////////
    DLOG(INFO) << "End merge: " << pNewBarrelInfo->getDocCount() << endl;

    MergeBarrelEntry* pNewEntry = new MergeBarrelEntry(pDirectory_,pNewBarrelInfo);
    addBarrel(pNewEntry);
}

void IndexMerger::removeMergedBarrels(MergeBarrel * pBarrel)
{
    if (!pMergeBarrels_)
        return;
    vector<MergeBarrelEntry*>::iterator iter = pMergeBarrels_->begin();
    size_t nEntry = 0;
    bool bRemoved = false;
    uint32_t nRemoved = 0;
    while (iter != pMergeBarrels_->end())
    {
        bRemoved = false;
        for (nEntry = 0;nEntry < pBarrel->size();nEntry++)
        {
            if ((*iter) == pBarrel->getAt(nEntry))
            {
                iter = pMergeBarrels_->erase(iter);
                bRemoved = true;
                nRemoved++;
            }
        }
        if (nRemoved == pBarrel->size())
            break;
        if (!bRemoved)
            iter++;
    }
}

void IndexMerger::transferToDisk(const char* pszBarrelName)
{
    if (!pMergeBarrels_)
        return;

    vector<MergeBarrelEntry*>::iterator iter = pMergeBarrels_->begin();
    MergeBarrelEntry* pEntry = NULL;

    while (iter != pMergeBarrels_->end())
    {
        if (!strcmp((*iter)->pBarrelInfo_->getName().c_str(),pszBarrelName))
        {
            pEntry = (*iter);
            pEntry->load();
            break;
        }
        iter++;
    }
}

