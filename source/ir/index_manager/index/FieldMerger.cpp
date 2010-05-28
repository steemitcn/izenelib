#include <ir/index_manager/index/FieldMerger.h>
#include <ir/index_manager/index/Posting.h>
#include <ir/index_manager/index/IndexBarrelWriter.h>
#include <ir/index_manager/index/TermReader.h>
#include <ir/index_manager/index/MultiPostingIterator.h>

#define MEMPOOL_SIZE_FOR_MERGING    50*1024*1024

using namespace izenelib::ir::indexmanager;

FieldMerger::FieldMerger(bool sortingMerge)
        :sortingMerge_(sortingMerge)
        ,pMergeQueue_(NULL)
        ,pPostingMerger_(NULL)
        ,nNumTermCached_(0)
        ,pDirectory_(NULL)
        ,ppFieldInfos_(NULL)
        ,nNumInfos_(0)
        ,termCount_(0)
        ,lastTerm_(0)
        ,lastPOffset_(0)
        ,beginOfVoc_(0)
        ,nMergedTerms_(0)
        ,pDocFilter_(0)
        ,pMemCache_(0)
{
}

FieldMerger::~FieldMerger()
{
    vector<MergeFieldEntry*>::iterator iter = fieldEntries_.begin();
    while (iter != fieldEntries_.end())
    {
        delete (*iter);
        iter++;
    }
    fieldEntries_.clear();

    if (pMergeQueue_)
    {
        delete pMergeQueue_;
        pMergeQueue_ = NULL;
    }
    if (pPostingMerger_)
    {
        delete pPostingMerger_;
        pPostingMerger_ = NULL;
    }

    if (ppFieldInfos_)
    {
        delete[] ppFieldInfos_;
        ppFieldInfos_ = NULL;
        nNumInfos_ = 0;
    }
    pDocFilter_ = 0;
    if(pMemCache_)
    {
        delete pMemCache_;
        pMemCache_ = NULL;
    }
}
void FieldMerger::addField(BarrelInfo* pBarrelInfo,FieldInfo* pFieldInfo)
{
    fieldEntries_.push_back(new MergeFieldEntry(pBarrelInfo,pFieldInfo));
    if (pPostingMerger_ == NULL)
        pPostingMerger_ = new PostingMerger();

}

fileoffset_t FieldMerger::merge(OutputDescriptor* pOutputDescriptor)
{
    if (initQueue() == false)///initialize merge queue
        //return 0; 
        //When collection is empty in a barrel, it still needs to write some information.
        return endMerge(pOutputDescriptor);


    pPostingMerger_->setOutputDescriptor(pOutputDescriptor);

    nMergedTerms_ = 0;
    int64_t mergedTerms = 0;
    int32_t nMatch = 0;
    FieldMergeInfo** match = new FieldMergeInfo*[pMergeQueue_->size()];
    Term* pTerm = NULL;
    FieldMergeInfo* pTop = NULL;
    TermInfo termInfo;
    while (pMergeQueue_->size() > 0)
    {
        nMatch = 0;
        //Pop the first
        match[nMatch++] = pMergeQueue_->pop();
        pTerm = match[0]->pCurTerm_;
        //Get the current top of the queue
        pTop = pMergeQueue_->top();
        while (pTop != NULL && (pTerm->compare(pTop->pCurTerm_)==0) )
        {
            //A match has been found so add the matching FieldMergeInfo to the match array
            match[nMatch++] = pMergeQueue_->pop();
            //Get the next FieldMergeInfo
            pTop = pMergeQueue_->top();
        }

        mergeTerms(match,nMatch,termInfo);

        if (termInfo.docFreq_ > 0)
        {
            ///store merged terms to term info cache
            cachedTermInfos_[nNumTermCached_].term_ = pTerm->getValue();
            cachedTermInfos_[nNumTermCached_].termInfo_.set(termInfo);
            nNumTermCached_++;
            if (nNumTermCached_ >= NUM_CACHEDTERMINFO)///cache is exhausted
            {
                flushTermInfo(pOutputDescriptor, nNumTermCached_);
                nNumTermCached_ = 0;
            }
            mergedTerms++;
        }

        while (nMatch > 0)
        {
            pTop = match[--nMatch];

            //Move to the next term i
            if (pTop->next())
            {
                //There still are some terms so restore it in the queue
                pMergeQueue_->put(pTop);
            }
            else
            {
                //No terms anymore
                delete pTop;
            }
        }
    }
    if (nNumTermCached_ > 0)///flush cache
    {
        flushTermInfo(pOutputDescriptor, nNumTermCached_);
        nNumTermCached_ = 0;
    }
    delete[] match;
    nMergedTerms_ = mergedTerms;
    return endMerge(pOutputDescriptor);///merge end here
}

bool FieldMerger::initQueue()
{
    if (fieldEntries_.size() <= 0)
        return false;
    pMergeQueue_ = new FieldMergeQueue(fieldEntries_.size());
    TermReader* pTermReader = NULL;
    FieldMergeInfo* pMI = NULL;
    MergeFieldEntry* pEntry;

    int32_t order = 0;
    nNumInfos_ = fieldEntries_.size();
    if (nNumInfos_ > 0)
    {
        ppFieldInfos_ = new MergeFieldEntry*[fieldEntries_.size()];
        memset(ppFieldInfos_,0,nNumInfos_ * sizeof(MergeFieldEntry*));
    }
    vector<MergeFieldEntry*>::iterator iter = fieldEntries_.begin();
    while (iter != fieldEntries_.end())
    {
        pEntry = (*iter);
        ppFieldInfos_[order] = pEntry;
        if (pEntry->pBarrelInfo_->getWriter()) ///in-memory index barrel
        {
            pTermReader = pEntry->pBarrelInfo_->getWriter()->getCollectionIndexer(pEntry->pFieldInfo_->getColID())
				->getFieldIndexer(pEntry->pFieldInfo_->getName())->termReader();
        }
        else
        {
            ///on-disk index barrel
            pTermReader = new DiskTermReader(pDirectory_,pEntry->pBarrelInfo_->getName().c_str(),pEntry->pFieldInfo_);
        }
        pMI = new FieldMergeInfo(order,pEntry->pFieldInfo_->getColID(),pEntry->pBarrelInfo_,pTermReader);
        if (pMI->next())	///get first term
        {
            pMergeQueue_->put(pMI);
            order++;
        }
        else
            delete pMI;
        iter++;
    }
    nNumInfos_ = order;


    return (pMergeQueue_->size() > 0);
}

void FieldMerger::flushTermInfo(OutputDescriptor* pOutputDescriptor, int32_t numTermInfos)
{
    IndexOutput* pVocOutput = pOutputDescriptor->getVocOutput();

    if (termCount_ == 0)
        beginOfVoc_ = pVocOutput->getFilePointer();
    termid_t tid;
    for (int32_t i = 0;i < numTermInfos;i++)
    {
        tid = cachedTermInfos_[i].term_;
        pVocOutput->writeInt(tid);
        pVocOutput->writeInt(cachedTermInfos_[i].termInfo_.docFreq_);			///write df
        pVocOutput->writeInt(cachedTermInfos_[i].termInfo_.ctf_);				///write ctf
        pVocOutput->writeInt(cachedTermInfos_[i].termInfo_.lastDocID_);			///write last doc id
        pVocOutput->writeInt(cachedTermInfos_[i].termInfo_.skipLevel_); 			///write skip level
        pVocOutput->writeLong(cachedTermInfos_[i].termInfo_.skipPointer_);		///write document posting offset        
        pVocOutput->writeLong(cachedTermInfos_[i].termInfo_.docPointer_);		///write document posting offset
        pVocOutput->writeInt(cachedTermInfos_[i].termInfo_.docPostingLen_);		///write document posting length (without skiplist)
        pVocOutput->writeLong(cachedTermInfos_[i].termInfo_.positionPointer_);		///write position posting offset
        pVocOutput->writeInt(cachedTermInfos_[i].termInfo_.positionPostingLen_);	///write position posting length
        
        termCount_++;
    }

}

fileoffset_t FieldMerger::endMerge(OutputDescriptor* pOutputDescriptor)
{
    IndexOutput* pVocOutput = pOutputDescriptor->getVocOutput();
    fileoffset_t voffset = pVocOutput->getFilePointer();
    ///begin write vocabulary descriptor
    pVocOutput->writeLong(voffset - beginOfVoc_);
    pVocOutput->writeLong(termCount_);
    ///end write vocabulary descriptor
    return voffset;
}

void FieldMerger::sortingMerge(FieldMergeInfo** ppMergeInfos,int32_t numInfos,TermInfo& ti)
{
    if(!pMemCache_)
        pMemCache_ = new MemCache(MEMPOOL_SIZE_FOR_MERGING);
    MultiPostingIterator postingIterator(numInfos);

    for (int32_t i = 0;i< numInfos;i++)
    {
        TermPositions* pPosition = new TermPositions(
                                                ppMergeInfos[i]->pIterator_->termPosting(),
                                                *(ppMergeInfos[i]->pIterator_->termInfo()));
        if(ppMergeInfos[i]->pBarrelInfo_->hasUpdateDocs)
            postingIterator.addTermPosition(pPosition);
        else
            postingIterator.addTermPosition(pPosition, pDocFilter_);
    }
    InMemoryPosting* newPosting = new InMemoryPosting(pMemCache_);

    docid_t docId = 0;
    while(postingIterator.next())
    {
        docId = postingIterator.doc();
        loc_t pos = postingIterator.nextPosition();
        while (pos != BAD_POSITION)
        {
            newPosting->add(docId, pos);
            pos = postingIterator.nextPosition();
        }
    }

    if(docId !=0)
        newPosting->write(pPostingMerger_->getOutputDescriptor(), ti);

    delete newPosting;
    pMemCache_->flushMem();
}


